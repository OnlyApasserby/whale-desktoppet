# Release 部署版偶发 `0xC0000409`（**未定位，暂缓观察**）

> **原编号**：`TRAP-P2-007`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：运行期崩溃 ｜ **状态**：**未定位** —— 用户调试**未复现**，按 `README.md` §六 暂缓

### 现象（已复现的事实，非推测）

在「部署版 + 干净 PATH + offscreen + 隐藏窗口 + 不重定向 stdio」条件下自动化观测时，
约 14 次运行中出现 2 次异常（另有一次为启动 3s 内退出、退出码 1，未确认是否同类）：

| 项 | 值 |
|---|---|
| 程序 | `deploy-release\WhalePet.exe`（Release） |
| 环境 | 干净 PATH、`QT_QPA_PLATFORM=offscreen`、`Start-Process -WindowStyle Hidden`、不重定向 stdio |
| 复现位置 | 启动后约 20s（立绘预载完成，WS 19.6MB→86.3MB、线程 4→17）再约 5s |
| 退出码 | `-1073740791` = **`0xC0000409`**（STATUS_STACK_BUFFER_OVERRUN / MSVC `__fastfail`） |
| 复现率 | 约 **2/14** |

### 已排除项（已验证）

- 单测层面无问题：`test_state_machine` / `test_line_table` / `test_smoke` 在 Debug 与 Release 均通过。
- 干净 PATH + offscreen 下启动、以及预载早期（WS ≈ 19.6MB 阶段）均稳定，可稳定运行 5s 以上。
- **Release 构建未生成 PDB**，无法直接符号化；Debug 构建有 PDB（`build\Debug\WhalePet.pdb`）。

### 用户调试结论

按上述步骤由用户执行：

- **Qt Creator（Debug 配置）**：未复现，无任何报错输出；
- **WinDbg**：未复现，无崩溃输出。

即：**当前无法稳定复现，根因未定位**。按约定，AI 不得继续自行插桩/试探定位，故本条**暂记为未定位**。

### 暂缓处理

- 不修改任何代码去「消除」一个无法复现的现象（禁止盲改）。
- 若后续在 Release 部署版**再次复现**，按 `README.md` §六 取证后再定位，建议：
  1. 配置 WER `LocalDumps`（或 `procdump -ma -e`）在崩溃时留存**完整转储**；
  2. 用**带符号的发布构建**（Release + `/Zi` + PDB）或 `RelWithDebInfo` 保证可符号化；
  3. 把转储 + PDB + `WhalePet.exe` 时间戳一并交回。
- 已知的**高风险观察点**（仅作为后续取证方向，**不是已确认根因**）：预载收尾 / `PoseLibrary::poseReady`
  回调链 / `idle→waiting` 切换附近；以及 offscreen 平台下的窗口（气泡）显示路径。

### 影响与关联文档

- 关联：`README.md` §六（崩溃一律交回用户调试）、`docs/BUILD.md`（构建配置与符号）、`ROADMAP-P2-Fin.md`（验收需真人桌面环境）。
- 结论：该现象**不阻塞** P2 的代码推进，但**在真实桌面上长时间挂机验收时应继续观察**。

### 复验记录（2026-09-30，用户侧真实桌面）

- P2 人工复验**全部通过**，其中空闲占用实测：**空闲 0.0%–0.1% CPU**、**拖动 0.3%–0.4% CPU**（远低于「空闲 < 5%」判据）。
- 挂机观察期间**仍未观测到** `0xC0000409`（与首次结论一致：AI 侧 offscreen 自动化场景偶发，用户侧不可复现）。
- 取证方式不变（`README.md` §六）：一旦复现，按「配 `LocalDumps` 留存完整转储 + 带 PDB 的 Release 产物 + exe 时间戳」交回用户调试。
- 该条**不再阻塞** P2 收尾（`ROADMAP-P2-Fin.md`），但作为**长期观察项**保留，不标记为「已解决」。

---
