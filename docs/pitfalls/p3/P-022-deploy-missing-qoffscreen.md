# 部署目录缺 `qoffscreen.dll`，offscreen 冒烟会「假存活」

> **原编号**：`TRAP-P3-005`　**阶段**：P3　**来源**：原按阶段聚合的 `traps-P3.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：环境 / 验证方法 ｜ **影响**：冒烟结论**假阳性/假阴性**——进程活着却什么都没跑，极易被当成「通过」

### 现象

把 Release 产物统一到 `deploy-release/` 后，用原有 offscreen 方式冒烟：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$p = Start-Process -FilePath .\deploy-release\WhalePet.exe -PassThru
Start-Sleep -Seconds 6
$p.HasExited      # → False（看起来「启动成功」）
Test-Path .\deploy-release\data    # → False（但数据库根本没建）
```

开启 stderr 后才看到真正原因：

```
qt.qpa.plugin: Could not find the Qt platform plugin "offscreen" in ""
```

`windeployqt` **只部署 `platforms/qwindows.dll`**，不会带上 `qoffscreen.dll`；
于是 `QApplication` 构造阶段就卡住（不进入 `main()` 之后的任何逻辑），
`PetWindow` 从未被构造，`Database` 自然也没打开——进程只是「挂在初始化阶段」。

（对照：`build\Release\WhalePet.exe` 同参数下能跑，因为它的 `Qt6Core.dll` 来自
`D:\Qt-debug\bin`，插件路径按 Qt 前缀解析，能命中 `D:/Qt-debug/plugins/platforms/qoffscreen.dll`。）

### 根因

Qt 的平台插件按「exe 同级 `platforms/`」查找。部署目录里没有 offscreen 插件，
`QT_QPA_PLATFORM=offscreen` 就无解；错误只落在一行 `qWarning` 上，
GUI 子系统（`WIN32`）进程没有控制台，默认看不到，于是表现为「静默假存活」。

### 解决 / 规避

- **验证部署目录时不要用 `offscreen` 平台**，改用默认（`qwindows`）跑真实桌面冒烟：
  ```powershell
  Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue
  $p = Start-Process -FilePath .\deploy-release\WhalePet.exe -PassThru
  Start-Sleep -Seconds 8
  Get-Process -Id $p.Id | Select-Object Threads,WorkingSet64
  Test-Path .\deploy-release\data     # 权威判据：库建出来了才算真跑起来
  Stop-Process -Id $p.Id -Force
  ```
- 判据必须是**副作用**（`data/whalepet.db` 生成、线程数 ≈ 30、WS ≈ 100MB），
  不能只用 `HasExited == $false`。
- 若确实需要无桌面 offscreen 冒烟：把 `D:\Qt-debug\plugins\platforms\qoffscreen.dll`
  一并拷入部署目录 `platforms/`（**属验证辅助，不随正式发布**）。
- 排障时用 `$env:QT_FORCE_STDERR_LOGGING='1'` + `-RedirectStandardError` 才能看到这类 `qWarning`。

### 影响与关联文档

- `BUILD.md` §3/§9（部署后最小化验证）、`ROADMAP-P3-Fin.md` 启动冒烟步骤、`TRAP-P2-008`（同为验证方法类）。
- **教训**：任何「进程还活着」的冒烟结论都要配一条产物断言，否则等于没验证。
