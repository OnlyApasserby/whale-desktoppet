# 构建目录缺 `qoffscreend.dll`，`test_smoke` 异常退出

> **原编号**：`TRAP-P4-001`　**阶段**：P4　**来源**：原按阶段聚合的 `traps-P4.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：环境 / 崩溃 ｜ **影响**：`ctest -C Debug` 里 `test_smoke` 判失败，
且**没有任何测试输出**（连 `-o` 指定的日志文件都不生成），极易误判为「P4 改动引入的崩溃」

### 现象

`ctest` 输出原文：

```
1/6 Test #1: test_smoke .......................***Failed   53.01 sec
...
50% tests passed, 3 tests failed out of 6
```

（同批 `test_growth` / `test_content` 另有失败，属独立问题，见 TRAP-P4-003 与「附」）

直接运行可拿到退出码，但**拿不到任何日志**：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
& .\build\Debug\test_smoke.exe -o smoke_out.txt,txt
"exit=$LASTEXITCODE"          # → exit=-2147483645
Test-Path .\smoke_out.txt     # → False（日志文件根本没生成）
```

`-2147483645` = `0x80000003`（`STATUS_BREAKPOINT`），即进程**异常终止**。

### 根因

**由用户在 Qt Creator 中调试确认**，两条环境原因叠加：

1. 系统环境变量里配置的 Qt 目录指向**已被删除的 Qt**（已改回正确目录）；
2. `build/Debug/platforms/` 下**缺 `qoffscreend.dll`**：

```powershell
Get-ChildItem .\build\Debug\platforms | Select-Object Name
# qwindowsd.dll        ← 只有这一个
```

机制（与 `TRAP-P3-005` 同源）：`windeployqt` **只部署 `platforms/qwindowsd.dll`**，
不会带上 offscreen 插件；而一旦 exe 同级出现 `platforms/` 目录，Qt 就把**该目录**当作插件目录，
**不再回退**到 Qt 前缀 `D:/Qt-debug/plugins/platforms/` 去找。
于是 `QT_QPA_PLATFORM=offscreen` 无插件可用，`QApplication` 构造阶段即终止——

这正好解释了「日志文件为空」：`test_smoke` 的 `-o` 日志、`qWarning()`
都发生在 `QApplication` 之后，而进程在**进入 `main()` 有效逻辑之前**就死了。

> `windeployqt` 是本次 P4 验证时**新执行**的动作（P1~P3 未在 `build/` 下跑过），
> 所以这个坑此前没暴露：以前 exe 同级没有 `platforms/`，Qt 按前缀解析能命中 `D:/Qt-debug/plugins`。

### 解决 / 规避

补齐 debug 版 offscreen 插件（**验证辅助，不随正式发布**）：

```powershell
Copy-Item "D:/Qt-debug/plugins/platforms/qoffscreend.dll" "build/Debug/platforms/" -Force
```

验证结果：`test_smoke` **2.02 sec Passed**，`ctest -C Debug` **6/6 通过**。

### 与 `TRAP-P3-005` 的差异

| | 目录 | 插件文件名 |
|---|---|---|
| `TRAP-P3-005` | 部署目录 `deploy-release/`（Release） | `qoffscreen.dll` |
| `TRAP-P4-001` | 构建目录 `build/Debug/`（Debug） | `qoffscreend.dll` |

**共同规律**：插件名要按构建配置带/不带 `d` 后缀；且**只要 exe 同级有 `platforms/`，
就必须把该目录当成完整插件目录来补齐**，不能指望它回退到 Qt 前缀。

### 影响与关联文档

- `docs/pitfalls/` `TRAP-P3-005`（同源问题的 Release 版本）、`BUILD.md`、`TESTING.md`。
- **教训**：`ctest` 报某个测试「失败且耗时异常（几十秒）」又拿不到输出时，
  第一件事是**直接跑该 exe 并取退出码**——`0x80000003` / 无日志文件 = 初始化阶段就死了，
  与业务代码无关。

---
