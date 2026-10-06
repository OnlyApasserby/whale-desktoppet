# ROADMAP · P9 — 渐进式插件化（P9-A 宿主服务注册化 + P9-B 外部进程型深化 + P9-C UI 宿主契约与贡献点）

> **当前状态（2026-10-05）**：**P9-A / P9-B 验收通过**（A1~A6 逐条通过，见 §6.2）；
> **当前任务焦点 = P9-C 编写阶段**（见 §1.1、§2.3、§3.2）。
>
> 环境基线：Qt 6.8.4 + MSVC（VS 18 2026）+ CMake 4.4.2（见 `BUILD.md`）。
> **本阶段不推倒重来**：不改动既有静态库分层形态、不改动 `MiniGameRegistry` 一族的公开 API 与语义，
> 只在既有「通用能力总线 + 三层装载器」上做**注册化收敛**（依据 `ARCHITECTURE.md` 附录 A.2 / A.3）。

## 0. 立项依据与裁决

| 项 | 内容 | 依据 |
|---|---|---|
| 调研依据 | 「完全插件化」可行性调研：4 个缺口（G1 UI 宿主上下文 / G2 贡献点 / G3 生命周期 / G4 ABI 导出层）、ROI 排序 | `ARCHITECTURE.md` 附录 A.2 / A.3 |
| 原裁决 | 2026-10-05：暂不推进 P9-A / P9-B / P9-C | `ARCHITECTURE.md` §A.5 |
| **启动裁决** | 2026-10-05（同会话重新裁决）：**推翻 §A.5，正式启动 P9-A + P9-B；P9-C 暂缓** | `ARCHITECTURE.md` §A.6 |
| **P9-A / P9-B 验收** | 2026-10-05：**A1~A6 逐条通过**（构建 / 测试 / 零回归 / 部署冒烟 / 文档与踩坑 / 阶段目标 / 用户确认），见 §3.1 / §6.2 | 本文件 §6 |
| **P9-C 裁决** | 2026-10-05：验收通过后 **P9-C 转入在范围内并进入编写阶段**（放开 G1 `IPluginUiHost` 与 G2 贡献点协议） | `ARCHITECTURE.md` §A.7 |
| 阶段性质 | **主阶段 P9**（可交付里程碑），须按 A1~A6 逐条验收通过 | `workspace-manage` §3.1 |
| 验收标准来源 | 本文件 §3（由 AI 补写、经用户确认后作为门禁） | 本阶段启动前置裁决 Q3 |

## 1. 范围

### 1.1 在范围内

| 子阶段 | 目标 | 剥离/深化对象 |
|---|---|---|
| **P9-A** | 把**无 UI 依赖**的 Service 经 builtin 层彻底注册化，宿主从对应 `setup*` 中剥离装配 | `setupGrowth` / `setupStomach` / `setupDialogue` / `setupEasterEgg` / `setupRecycleBin`（`ARCHITECTURE.md` A.3 第 1 步） |
| **P9-B** | 外部进程型（`plugin/process/`）继续走深，**宿主只保留注册与状态展示** | `PetWindow::setupProcessPlugins()`（`ARCHITECTURE.md` A.3 第 2 步） |
| **P9-C** | 引入 **`IPluginUiHost`（G1 UI 宿主上下文）与贡献点协议（G2）**，使 **UI 面板型插件**可在**不修改宿主**的前提下注册界面 | 宿主中 UI 面板装配点（设置页 / 状态面板 / 内容面板 / 小游戏窗口 / 对话面板）（`ARCHITECTURE.md` A.3 第 3 步） |

> **范围变更留痕**：P9-C 原为「暂缓」，2026-10-05 经用户确认 **P9-A / P9-B 验收通过后启动**；
> 裁决见 `ARCHITECTURE.md` §A.7（修订 §A.6 中「P9-C 暂缓」的部分）。

### 1.2 不做（明确范围外）

- **P9-C 之后不再扩张**（2026-10-05 修订）：P9-C 已转入在范围内（见 §1.1、§2.3），
  但**仍不含**导出宏与稳定 ABI 子集（G4），也不新增 DLL 形态业务插件（`whalepet_ext_*` 示例维持原状）。
- 不引入任何第三方依赖（允许 Qt 官方模块，见 `README.md` §5.1）。
- **不改动** `MiniGameRegistry` / `IMiniGamePlugin` / `MiniGameView` / `MiniGameContext` /
  `MiniGameInfo` / `registerBuiltinMiniGames()` 的公开 API 与语义（`PLUGIN-ARCHITECTURE.md` §7 零回归红线）。
- 不改变既有默认运行口径（默认不监听端口 / 管道、不扫描 `plugins/`、`plugins.json` 缺失即不启动外部进程）。

## 2. 交付物

### P9-A · 宿主服务注册化

新增（归属 `whalepet_view` 目标；`whalepet_plugin` 保持**不依赖 view / model**，注册回调仍由宿主侧提供）：

| 项 | 位置 |
|---|---|
| 服务插件句柄与统一注册入口 | `src/viewmodel/builtin/BuiltinServicePlugins.h/.cpp` |
| 养成服务插件 | `src/viewmodel/builtin/GrowthServicePlugin.h/.cpp` |
| 胃袋服务插件 | `src/viewmodel/builtin/StomachServicePlugin.h/.cpp` |
| 预设对话服务插件 | `src/viewmodel/builtin/DialogueServicePlugin.h/.cpp` |
| 代码彩蛋服务插件 | `src/viewmodel/builtin/EasterEggServicePlugin.h/.cpp` |
| 回收站提醒服务插件 | `src/viewmodel/builtin/RecycleBinServicePlugin.h/.cpp` |
| 只读状态能力（各 1 个） | `service.growth` / `service.stomach` / `service.dialogue` / `service.easterEgg` / `service.recycleBin` |
| 宿主改造 | `PetWindow` 新增 `setupDatabase()`（数据库留宿主）与 `setupBuiltinServices()`（注册 + 构造期启动）；五个 `setup*` 不再 `new` 任何 Service，改由 `BuiltinPluginLoader` 注册后从 `BuiltinServiceHandles` 取用。**实现期修正**：`DialogueService` 仍由 `PetController` 构造，插件只做「配置注入」（数据库 / 好感度来源 / 静息门槛），故**无需**新增 `PetController::setDialogueService()` |
| 单测 | `tests/test_service_plugins.cpp`（注册 / 能力 / 生命周期 / 剥离断言） |

**职责边界（P9-A 内固定，不得上浮到通用 UI 契约）**：

- 插件负责：Service 的 **创建 / 启动 / 停止 / 向 controller 注入 / 向总线注册能力**，
  以及**逻辑型**信号接线（如 `controller->interactionOccurred → EasterEggService::poke`）。
- 宿主保留：**UI 反应**（`GrowthService::stateChanged → syncStatusPanel`、
  `DialogueService::optionsOffered → DialoguePanel`、回收站提醒的托盘气泡）。
  这属 `ARCHITECTURE.md` §3「表现与逻辑分离」的既有原则，不构成新的 UI 宿主契约。

### P9-B · 外部进程型深化

| 项 | 位置 |
|---|---|
| `plugins.json` 解析与校验下沉（纯逻辑，可脱 UI 单测） | `src/plugin/process/ProcessPluginConfig.h/.cpp` |
| 会话状态只读快照 | `plugin::ProcessPluginStatus` + `ProcessPluginLoader::sessionStates()` |
| 宿主简化为「注册 + 状态展示」 | `PetWindow::setupProcessPlugins()` 只做加载 / 注册；状态在设置页以只读列表展示 |
| 单测 | `tests/test_process_plugin.cpp` 扩展（解析 / 状态快照 / 崩溃后状态） |

### P9-C · UI 宿主契约与贡献点协议（🟡 编写中）

> 依据 `ARCHITECTURE.md` §A.2 缺口 G1 / G2 与 §A.3 第 3 步；**下列交付物为初版草案，待用户确认后固化**。

| 项 | 位置（拟） |
|---|---|
| UI 宿主上下文接口 `IPluginUiHost`（G1：宿主窗口句柄 / 父 `QWidget` / 生命周期回调） | `src/plugin/ui/IPluginUiHost.h` |
| 贡献点协议（G2：右键菜单项 / 托盘项 / 设置页注册） | `src/plugin/ui/PluginContribution.h` 等 |
| 宿主侧贡献点收集与分发 | `PetWindow`（`setupContextMenu` / `setupTray` / `setupSettings`）由硬编码改为「读取贡献点 + 注册」 |
| 首个 UI 面板型插件迁移（试点，具体子项待确认） | 待定（候选：设置页 / 状态面板 / 内容面板 / 小游戏窗口 / 对话面板） |
| 单测 | `tests/test_ui_plugin_host.cpp`（接口契约 / 贡献点注册 / 生命周期） |

**不变约束**：UI 型插件继续走**进程内 builtin 层**（不引入 G4 导出宏 / 稳定 ABI 子集）；
`MiniGameRegistry` 一族零改动。

## 3. 验收（A1~A6，逐条通过才可推进）

> **P9-A / P9-B**：A1~A6 已于 2026-10-05 **逐条通过**（验收结论见 §3.1 / §6.2）。
> **P9-C**：验收标准见 §3.2（初版草案，待用户确认）。

| # | 验收项 | 证据 |
|---|---|---|
| **A1** | **构建通过**：Debug 与 Release 均成功 | `cmake --build build --config Debug --parallel`、`--config Release`，退出码 0；复用既有 `build/` 目录 |
| **A2** | **测试通过**：Debug 全量 CTest 通过，目标数 ≥ 36（新增 `test_service_plugins`） | `ctest --test-dir build -C Debug --output-on-failure --timeout 120`；`test_win32_observer` / `test_dll_plugin` 仍按 `WIN32` 条件注册 |
| **A2'** | **零回归**：既有 35 个目标全绿，尤其 `test_growth` / `test_recyclebin` / `test_preset_dialogue` / `test_code_easter_egg` / `test_plugin_registry` / `test_process_plugin` / `test_smoke` | CTest 输出逐项 Passed；**不删除断言、不放宽条件、不注释用例** |
| **A3** | **产物可运行 / 可部署**：Debug 与 Release 部署目录在干净 PATH 下 offscreen 冒烟退出码 0 | `windeployqt` 配对部署 + `$env:QT_QPA_PLATFORM='offscreen'` 运行并检查退出码 |
| **A4** | **文档与踩坑已刷新**：技术文档 + 统一入口 + 踩坑（或显式说明无踩坑） | `docs/README.md`、`ARCHITECTURE.md`、`PLUGIN-ARCHITECTURE.md`、`CONTEXT-API.md`（新增 `service.*` 能力）、`docs/pitfalls/index.md` |
| **A5** | **阶段目标达成**（逐条可核验） | ① 五个 `setup*` 中**不再出现 `new viewmodel::*Service`**；② 五个服务均以 `IPlugin` 形式出现在 `PluginRegistry`；③ `capabilities.list` 含 5 个 `service.*` 只读能力；④ `process/` 提供 `sessionStates()`，宿主不再内联解析 `plugins.json` |
| **A6** | **用户确认** | 用户对本文件 §3 与本轮交付的显式确认（2026-10-05 ✅ 通过） |

### 3.1 P9-A / P9-B 验收结论（2026-10-05）

| # | 验收项 | 结论 | 证据 |
|---|---|---|---|
| A1 | 构建通过（Debug / Release） | ✅ 通过 | `cmake --build build --config Debug` / `--config Release --parallel`，退出码 0 |
| A2 | 测试通过（Debug 全量 CTest，目标数 ≥ 36） | ✅ 通过 | **36/36 passed**（新增 `test_service_plugins`；`-j1` 顺序执行口径） |
| A2' | 零回归（既有 35 目标全绿） | ✅ 通过 | 逐项 Passed；未删除断言 / 未放宽条件 / 未注释用例 |
| A3 | 产物可运行 / 可部署（offscreen 冒烟退出码 0） | ✅ 通过 | `deploy-release/` 干净 PATH + `QT_QPA_PLATFORM=offscreen`，启动 4s 存活 |
| A4 | 文档与踩坑已刷新 | ✅ 通过 | `README.md` / `ARCHITECTURE.md` / `PLUGIN-ARCHITECTURE.md` / `CONTEXT-API.md` / `docs/pitfalls/p9/` |
| A5 | 阶段目标达成（①~④） | ✅ 通过 | 见 §6.1 |
| A6 | 用户确认 | ✅ 通过 | 2026-10-05 用户确认 P9-A / P9-B 验收通过 |
| — | 踩坑登记 | ✅ 完成 | `P-081` / `P-082` / `P-083`（`docs/pitfalls/p9/`） |

### 3.2 P9-C 验收（初版草案，待用户确认）

| # | 验收项 | 证据（拟） |
|---|---|---|
| **C1** | 构建通过：Debug 与 Release 均成功 | 复用既有 `build/`，退出码 0 |
| **C2** | 测试通过：Debug 全量 CTest 通过（目标数 ≥ 37，新增 `test_ui_plugin_host`） | `ctest --test-dir build -C Debug -j1 --output-on-failure --timeout 120` |
| **C2'** | 零回归：既有 36 个目标全绿 | 不删除断言、不放宽条件、不注释用例 |
| **C3** | 产物可运行 / 可部署：offscreen 冒烟退出码 0 | `windeployqt` 配对部署 + offscreen 运行 |
| **C4** | 文档与踩坑已刷新 | 相关技术文档 + `docs/pitfalls/p9/`（或显式说明无踩坑） |
| **C5** | 阶段目标达成：① `IPluginUiHost` 与贡献点协议落地；② 至少 1 个 UI 面板型插件经贡献点注册（不修改宿主业务分支）；③ 宿主贡献点收集与分发可核验 | 源码 + 单测 + 冒烟 |
| **C6** | 用户确认 | 用户对 §3.2 与本轮交付的显式确认 |

## 4. 零回归约束（与 A2' 对应）

1. `core::WorkState::Unknown` 默认行为、深夜 / 贴边 / 工作态立绘切换、预设对话五选一配额、
   彩蛋 5% 概率、回收站 5–10 分钟轮询与开关持久化 —— **行为与 P8 一致**。
2. 拖拽投喂（`dropEvent → StomachService::ingest`）、状态面板 / 设置面板对服务的读取路径**保持可用**。
3. `MiniGameRegistry` 一族零改动；`test_smoke` 的小游戏菜单断言继续通过。
4. 真实运行时新出现的能力仅**新增**，不删改既有 6 个 `context.*` 与 3 个 `minigame.*`。

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| `whalepet_plugin` 若为注册服务而依赖 `viewmodel`，将形成反向依赖 | 服务插件落在 `whalepet_view` 内（与 `MiniGameCompatAdapter` 同构），总线侧只接收宿主注入的 `std::function` |
| `DialogueService` 原由 `PetController` 构造，插件化将改动其归属 | 新增 `PetController::setDialogueService()`（与既有 `setGrowthService` 同构），仅在「服务为空」时接管；测试覆盖注入前后行为一致 |
| 新增 `service.*` 能力改变对外能力清单 | 在 `CONTEXT-API.md` 方法表登记；单测 harness 不含宿主服务插件，故既有 `capabilities.list` 断言不受影响（已核实 `test_context_dispatch.cpp:265`、`test_context_pipe.cpp:371`） |
| 剥离装配时漏接 UI 反应（如托盘气泡） | A5 的「剥离断言」+ `test_smoke` 冒烟共同把关；剥离后逐一核对宿主 UI 连接点 |
| P9-C 引入 UI 宿主契约后，UI 面板型插件回归面大（宿主 22 个装配点 + 全量测试） | 试点先迁移 **1 个**面板；零回归红线（`MiniGameRegistry` 一行不改）+ 全量 CTest 把关；贡献点只「新增」，不改既有菜单 / 托盘语义 |
| P9-C 若为 UI 契约引入 DLL 导出层（G4），成本与约束陡增 | 明确 **UI 型插件走进程内 builtin 层**，不引入 G4 导出宏 / 稳定 ABI 子集 |

## 6. 验证记录

> 2026-10-05 回填。构建目录复用既有 `build/`（VS 18 2026，多配置），未新建 / 未删除构建目录。

| 步骤 | 命令 | 结果 |
|---|---|---|
| Configure | `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"` | 退出码 0 |
| 构建 Debug | `cmake --build build --config Debug --parallel` | 退出码 0 |
| 构建 Release | `cmake --build build --config Release --parallel` | 退出码 0 |
| 测试 Debug | `ctest --test-dir build -C Debug -j1 --output-on-failure --timeout 120` | **36/36 passed**（新增 `test_service_plugins`） |
| 测试 Release | `ctest --test-dir build -C Release -j1 --output-on-failure --timeout 120` | **36/36 passed** |
| 部署冒烟（A3） | `deploy-release/WhalePet.exe`，干净 PATH + `QT_QPA_PLATFORM=offscreen` | 启动后 4s 存活，未崩溃 |
| 零回归（A2'） | 既有 35 个目标 | 全绿。首次全量时 `test_context_http_security` 偶发失败（9.33s），与既有记录 **`P-064`（TRAP-P8-006）** 现象一致（非 P9 回归）：单独复跑 4.26s 通过、显式顺序全量 36/36 通过 |

### 6.1 阶段目标达成（A5）证据

| 子项 | 证据 |
|---|---|
| ① 五个 `setup*` 不再 `new` Service | `PetWindow::setupGrowth` / `setupStomach` / `setupDialogue` / `setupEasterEgg` / `setupRecycleBin` 均改为从 `m_serviceHandles` 取指针；服务创建移入 `src/viewmodel/builtin/` |
| ② 五个服务均以 `IPlugin` 出现 | `registerBuiltinServicePlugins()` 注册 5 个插件（`builtin.growth` / `builtin.stomach` / `builtin.dialogue` / `builtin.easterEgg` / `builtin.recycleBin`）；`test_service_plugins::registersFivePlugins` |
| ③ `capabilities.list` 含 5 个 `service.*` | `builtinServiceCapabilityIds()` ≡ 注册表清单；`test_service_plugins::capabilitiesMatchDeclaredIds` |
| ④ `process/` 提供 `sessionStates()`，宿主不再内联解析 `plugins.json` | `ProcessPluginLoader::sessionStates()` + `plugin::ProcessPluginConfig`；设置页「外部插件」只读列表 |

### 6.2 P9-A / P9-B 验收结论（2026-10-05）

> 用户于 2026-10-05 确认 **P9-A / P9-B 验收通过**：A1~A6 逐条达成（逐项证据见 §3.1、§6、§6.1）。

| 项 | 结论 |
|---|---|
| 验收状态 | ✅ **P9-A / P9-B 验收通过**（A1~A6 逐条通过） |
| 测试口径 | Debug / Release CTest 各 **36/36**（`-j1` 顺序执行）；零回归 35 目标全绿 |
| 产物 | `deploy-release/` 干净 PATH + `QT_QPA_PLATFORM=offscreen` 冒烟退出码 0 |
| 踩坑 | `P-081` / `P-082` / `P-083`（`docs/pitfalls/p9/`） |
| **当前任务焦点** | 🟡 **P9-C 编写阶段**（`IPluginUiHost` G1 + 贡献点协议 G2；见 §2.3 / §3.2） |

> 后续：P9-C 的交付物与验收标准（§2.3 / §3.2）为**初版草案**，待用户确认后固化，再进入实现与门禁验收。
