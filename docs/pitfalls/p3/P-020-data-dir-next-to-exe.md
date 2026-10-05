# 养成数据目录会出现在 exe 同级 `data/`

> **原编号**：`TRAP-P3-003`　**阶段**：P3　**来源**：原按阶段聚合的 `traps-P3.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：环境 / 产物 ｜ **影响**：无功能影响；仅影响「哪些文件算运行期产物」的认知

### 现象

按 `ROADMAP-P3-Fin.md` 的启动冒烟步骤运行后：

```powershell
$p = Start-Process -FilePath .\build\Release\WhalePet.exe -PassThru
Start-Sleep -Seconds 6
Stop-Process -Id $p.Id -Force
Get-ChildItem .\build\Release\data
```

输出：

```
whalepet.db      # 53248 字节
```

即**构建目录** `build/Release/` 下长出了一个 `data/` 目录；同理，部署目录
`deploy-release/` 下首次运行也会长出 `data/`。

### 根因

这是 `DATA-MODEL.md` §1 明确的设计：首选位置就是 **`<安装目录同级>/data/whalepet.db`**。
`DataPaths::resolve()` 取 `QCoreApplication::applicationDirPath() + "/data"`，
`Database::openAt()` 会用 `mkpath` 自动建目录——所以「跑一次就多一个 `data/`」是**预期行为**，
不是路径拼接错误。

### 解决 / 规避

- 无需改代码。需要注意的只是产物归类：
  - `build/**/data/` 属构建产物；
  - `deploy-release/data/` 属**用户数据**，覆盖部署（重新解压 / 重跑 `windeployqt`）时不应删除，
    否则养成进度与窗口位置会丢。
- 仓库已纳入版本管理并有 `.gitignore`：`build/`、`deploy-release/` 已整体忽略，
  其下的 `data/` 不会误提交。（若后续把数据目录改到仓库内非忽略位置，需补 `**/data/` 规则。）

### 验证方法（确认落盘位置正确）

`StatusPanel` 底部会显示实际生效的存储位置与模式（`PetWindow::storageInfo()`），
正常应为「存储：安装目录」+ `<路径>/data/whalepet.db`；若显示「用户目录（已降级）」或
「内存模式（不落盘）」，说明走了 `DATA-MODEL.md` §1 的降级分支。

### 影响与关联文档

- `DATA-MODEL.md` §1（存储位置）、`BUILD.md`（部署产物）、`ROADMAP-P3-Fin.md` §5/§实现状态。
- 构建期已加 `sqldrivers/qsqlite` 插件检查（缺插件直接 `FATAL_ERROR`），
  避免「插件缺失 → 静默降级到内存库 → 数据不落盘却不报错」这种「能跑但错」。

---
