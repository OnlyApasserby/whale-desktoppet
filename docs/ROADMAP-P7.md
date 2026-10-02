# ROADMAP · P7 — 插件化智能桌宠与本地 Context API

> 本阶段把 WhalePet 从「桌宠外壳 + 养成 + 小游戏」扩展为**插件化智能桌宠系统**：
> 感知桌面环境 → 判定工作状态（含 Coding / Vibe Coding）→ 驱动桌宠行为，
> 并以**本地 Context API**（MCP stdio + 本地回环）对外开放。
>
> 前置：P0–P6 **交付完成**（`ROADMAP-P6-Fin.md`，版本 0.2.0，12/12 测试通过；
> 其中 `ROADMAP-P1.md` / `ROADMAP-P4.md` 未改签 `-Fin`，原因见 `docs/README.md` §二.3）。
> 架构依据：`PLUGIN-ARCHITECTURE.md`；对外协议：`CONTEXT-API.md`；踩坑：`traps-P7.md`。

---

## 阶段总览

| 阶段 | 目标 | 交付物 | 状态 |
|---|---|---|---|
| **P7.0** | 设计 + 重构骨架 | 设计文档、通用能力总线、感知/状态/Context API 接口与骨架、4 个新测试 | ✅ 已完成（2026-10-02） |
| **P7.1** | 真实桌面感知 | `Win32DesktopObserver`（前台窗口 / 进程名 / 空闲 / 键鼠计数 / 会话状态）、观察者生命周期、`WorkStateRules` 按真实数据回归调参、1 个新测试 | ✅ 已完成（2026-10-02） |
| **P7.2** | 通道启用与 MCP 桥接 | `context_api_enabled` 生效、本机 HTTP/命名管道可访问、`whalepet-mcp.exe` 控制台桥接 | 待实施（HTTP 回环已生效；命名管道未实现、桥接 exe 未交付——见 `P7-REMAINING-INTERFACES-AUDIT.md`） |
| **P7.3** | 动态插件（DLL） | `plugins/` 目录扫描、IID/版本协商、打包三处对应表同步 | 待实施（加载器已实现，**未接入组合根**——见 `P7-REMAINING-INTERFACES-AUDIT.md`） |
| **P7.4** | 外部进程插件（MCP Client） | 子进程生命周期、`tools/list` 能力发现、超时/崩溃隔离 | ✅ 已完成（2026-10-02） |
| **P7.5** | ACP / IDE Agent 集成 | 显式信号源（IDE 扩展 / 文件保存 / diff）与会话桥接的具体协议实现 | ✅ 已完成（2026-10-02） |
| **P7.6** | ACP（Agent Client Protocol）实时状态接入 | `AcpEventMapper` + `AcpClient`（NDJSON over stdio 子进程）+ 组合根装配；真实 dsh 端到端通过 | ✅ 已完成（2026-10-02） |

> **口径更新（2026-10-02）**：`CONTEXT-API.md` §6 的两个接口（`ISignalSource` / `IAgentBridge`）
> 已具备**具体实现**（`src/contextapi/acp/**`），`ProcessPluginLoader` 已由骨架升级为
> **完整 MCP Client**（`src/plugin/process/**`）。仍**未交付**的是 P7.2 的控制台桥接 exe
> （`whalepet-mcp.exe`）与命名管道通道、以及 P7.3 的 DLL 插件产物——它们**不属于「预留接口」**，
> 而是 P7.2 / P7.3 的独立交付物；因此 MCP 的 **Server 侧真实通道**仍未启用
> （`StdioTransport` 自身已实现并可单测）。

---

## P7.0 设计 + 重构骨架

### 阶段目标

给出可实施的技术方案与开发路径，并落地**不改动既有行为**的架构骨架。

### 交付物

1. **设计文档**：`PLUGIN-ARCHITECTURE.md`、`CONTEXT-API.md`、本文件、`traps-P7.md`；
   更新 `docs/README.md`（索引 + 口径）、`ARCHITECTURE.md`（分层 + 依赖）、
   `STATE-MACHINE.md`（工作态通道）、`SETTINGS.md`（新设置项）、`packages.md`（插件目录约定）。
2. **通用能力总线**（`whalepet_plugin`）：`Capability` / `PluginInterface` / `PluginRegistry` /
   `BuiltinPluginLoader` / `DllPluginLoader`（**已实现，但未接入组合根 —— P7.3 待办**）/
   `ProcessPluginLoader`。
3. **感知接口与空实现**（`whalepet_platform`）：`IEnvironmentObserver` + `EmptyDesktopObserver`
   （恒返回 unknown，**不采集**）。
4. **工作状态纯逻辑**（`whalepet_core`）：`WorkState` / `EnvSample` / `WorkStateSample` /
   `WorkStateRules`（判据 + 置信度 + 滞回）。
5. **状态驱动链路**（`viewmodel`）：`EnvironmentService` / `WorkStateService` /
   `PetContextProvider`；`PetController` 新增 `handleWorkState` 通道；
   `PetStateMachine` 新增 `EventType::WorkStateChanged`。
6. **Context API 骨架**（`whalepet_contextapi`）：`ContextSnapshot` / `JsonRpcDispatcher` /
   `ContextApiService` / `StdioTransport` / `LocalHttpTransport` / `ISignalSource` / `IAgentBridge`。
7. **兼容泛化**：`MiniGameRegistry` 公开 API 不变，内部叠加通用总线（`MiniGameCompatAdapter`）。
8. **测试**：`test_plugin_registry` / `test_platform_skeleton` / `test_work_state` /
   `test_context_dispatch`（4 个新目标，CTest 由 12 → 16）。

### 验收标准

- [x] 既有 12 个测试目标 Debug / Release **零回归**（不删断言、不放宽条件、不注释用例）。
- [x] 新增 4 个测试目标通过（CTest 12 → 16，Debug / Release 各 **16/16**）。
- [x] 默认配置（感知关、Context API 关）下**运行行为与 0.2.0 一致**：
      无端口监听、无采样、无新增主动台词；`WorkState::Unknown` 时状态机行为与 P6 完全相同
      （由 `test_work_state::unknownWorkStateKeepsLegacyBehavior` 守卫）。
- [x] `MiniGameRegistry` 公开 API 与 `registerBuiltinMiniGames()` 语义未变
      （实际上**该文件一行未改**，兼容由 `MiniGameCompatAdapter` 在外层完成）；
      「小游戏…」子菜单结构断言（`test_smoke`）继续通过。
- [x] `capabilities.list` 能看到 `minigame.minesweeper` / `minigame.kitten`（证明泛化生效）。
- [x] 依赖口径更新已写入 `docs/README.md` §5.1 与 `ARCHITECTURE.md` §2
      （零第三方依赖，允许 Qt 官方模块）。

### 不做（P7.0 范围外）

- 真实 Win32 采集（P7.1）；真实 MCP 桥接 exe 与命名管道（P7.2）；
  DLL 实际产物与打包（P7.3）；外部进程插件真实拉起（P7.4）；ACP 协议（P7.5）。

### 验证记录（2026-10-02）

环境：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，生成器 `Visual Studio 18 2026`。

| 步骤 | 命令 | 结果 |
|---|---|---|
| configure | `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"` | 成功（WebP / SQLite 插件探测通过；`Qt6::Network` 可用） |
| build Debug | `cmake --build build --config Debug --parallel` | 成功（新增 3 个静态库 + 4 个测试目标 + WhalePet.exe） |
| test Debug | `ctest --test-dir build -C Debug --output-on-failure --timeout 120` | **16/16 passed** |
| build Release | `cmake --build build --config Release --parallel` | 成功（`WhalePet.exe` 落在 `deploy-release/`） |
| test Release | `ctest --test-dir build -C Release --output-on-failure --timeout 120` | **16/16 passed** |

> 测试统一在 `QT_QPA_PLATFORM=offscreen` 下运行；未删除任何断言、未注释失败用例、未放宽比较条件。
> 本阶段**未出现崩溃**（无异常退出 / 访问违例），故无需按 `docs/README.md` §六 交回调试。
> 实际踩坑 5 条（3 条编译期 / 1 条 moc / 1 条由单测发现的契约缺陷）见 `traps-P7.md`。

**未做项（如实说明）**：未实现真实桌面采集、未实现 MCP 桥接 exe、未产出 DLL 插件、
未做长时运行下的性能采样（Phase 1 无新增轮询热点：采样仅在开启感知时启动，1s 一次）。

---

## P7.1 真实桌面感知（本期）

### 阶段目标

把 `platform` 层从「空实现（恒无数据）」升级为**真实 Win32 采集**，并据真实输入量级回归
`WorkStateRules` 判据；**默认行为完全不变**（不采样、不安装任何系统钩子），
只有用户勾选「工作状态感知」后才开始采集。

### 交付物

| # | 交付项 | 代码位置 |
|---|---|---|
| 1 | 前台窗口采样器：标题（`GetForegroundWindow` + `GetWindowTextW`）、进程名（`GetWindowThreadProcessId` + `QueryFullProcessImageNameW`，只暴露进程名不给路径） | `src/platform/Win32DesktopObserver.{h,cpp}` |
| 2 | 输入采样器：空闲时长（`GetLastInputInfo`）+ 键鼠事件计数（**首选低层钩子** `WH_KEYBOARD_LL` / `WH_MOUSE_LL`；安装失败**降级**为 `GetLastInputInfo` 差分 + 1s 定时采样） | 同上 |
| 3 | 系统状态采样器：会话锁定（`OpenInputDesktop` 失败 = 安全桌面）/ 屏保（`SPI_GETSCREENSAVERRUNNING`）→ `systemPaused` | 同上 |
| 4 | 文本工具：UTF-16 → UTF-8、路径取文件名（**纯函数**，可脱系统单测） | `src/platform/Win32TextUtil.{h,cpp}` |
| 5 | 观察者生命周期 `setObserving(bool)`：钩子**只在采样期间**安装，`stop()` / 析构双保险卸载 | `src/platform/DesktopObserver.{h,cpp}`、`src/viewmodel/EnvironmentService.{h,cpp}` |
| 6 | 判定回归调参：`systemPaused` 优先于「无数据」；Coding 增加「采样窗口内高强度单应用输入」判据 | `src/core/WorkStateRules.{h,cpp}` |
| 7 | 组合根装配：Windows 注入真实观察者，其它平台回落空实现 | `src/view/PetWindow.cpp` |
| 8 | 新测试目标 `test_win32_observer`（12 个用例，CTest 16 → 17）；`test_work_state` 增加真实输入画像与「锁屏优先」用例 | `tests/test_win32_observer.cpp`、`tests/test_work_state.cpp` |

### 关键约束与取舍（可核验）

- **只计数不读内容**：钩子回调只做一次原子自增，不记录键码、不读文本、不读编辑区；
  鼠标**只计按键与滚轮、不计移动**（`WM_MOUSEMOVE` 量级极大，计入会让「输入爆发」失真）。
- **只取前台窗口**：不做全窗口枚举、不截图；`env.appId` 只暴露进程名（不泄露安装路径）。
- **钩子优先、失败降级**：`SetWindowsHookExW` 失败（杀软拦截 / 策略限制）只记 `qWarning`
  并本周期改用差分；**降级可观测**（`inputSourceName()` + 启动日志打印当前计数来源）。
- **默认关闭 = 零系统资源**：装配阶段**不安装**钩子；`EnvironmentService::start()` 才
  `setObserving(true)`，`stop()` / `Win32DesktopObserver` 析构都会 `UnhookWindowsHookEx`。
- **不伪造**：任一读数失败（无前台窗口 / `GetLastInputInfo` 失败 / `OpenProcess` 无权限）
  一律返回 `false`，由组合层清空对应字段——宁可 `unknown`，也不猜。
- **单测安全**：注入替身读数时**永不**安装系统钩子（`test_win32_observer` 全用例可在 CI/headless 稳定运行）。

### 验收标准

- [x] 前台应用切换在一个采样周期内反映：`test_win32_observer::observerAggregatesInjectedReadings`
      以 `1000ms → 2000ms` 两次采样验证 `appId` / `category` / `appSwitches` / `dwellMs` 同步变化。
- [x] 长时间无输入正确进入 `afk`：`idleMs` 取真实 `GetLastInputInfo`；
      `test_work_state::realDesktopProfilesMapToExpectedStates`（画像 5）与
      `test_win32_observer::lockedSessionWithoutForegroundIsAfk`（锁屏 → `afk`）守卫生效。
- [x] `WorkStateRules` 按真实数据回归调参并补单测：新增「采样窗口内高强度单应用输入 → Coding」
      判据（专注打字实测约 5~10 次/秒），并把「锁屏优先于无数据」的顺序缺陷固化为用例
      （`pausedSessionWinsOverMissingData` / `lockedSessionWithoutForegroundIsAfk`）。
- [x] 既有 16 个测试目标 **零回归**（不删断言、不放宽条件、不注释用例），新增 1 个目标 → CTest **17**。
- [x] 默认配置（`work_aware_enabled = false`）下**不采样、不装钩子、无新增主动台词**，
      `WorkState::Unknown` 时行为与 P6 一致。
- [x] 本阶段**未出现崩溃**（无异常退出 / 访问违例），无需按 `docs/README.md` §六 交回调试。

### 不做（P7.1 范围外）

- MCP 桥接 exe / 命名管道 / 通道真实启用（P7.2）——**MCP 与 ACP 仍是预留接口**（只定义不接入）；
  *（该句为 **P7.1 当时**的状态描述；P7.4–P7.6 已把两套接口补为实现，见本文件后文与
  `docs/P7-REMAINING-INTERFACES-AUDIT.md`。）*
- DLL 插件（P7.3）、外部进程插件（P7.4）、ACP / IDE Agent 协议实现（P7.5）；
- 全屏独占检测：`systemPaused` 目前只覆盖「会话锁定 / 屏保」（游戏态已由 `AppCategory::Game` 覆盖）；
- 未做长时（≥1 天）运行的内存 / GDI 句柄 / 钩子句柄曲线采样。

### 验证记录（2026-10-02）

环境：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，生成器 `Visual Studio 18 2026`。

| 步骤 | 命令 | 结果 |
|---|---|---|
| configure | `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"` | 成功 |
| build Debug | `cmake --build build --config Debug --parallel` | 成功（新增 `Win32DesktopObserver.cpp` / `Win32TextUtil.cpp` 与 `test_win32_observer`） |
| test Debug | `ctest --test-dir build -C Debug --output-on-failure --timeout 120` | **17/17 passed** |
| build Release | `cmake --build build --config Release --parallel` | 成功（`WhalePet.exe` 落在 `deploy-release/`） |
| test Release | `ctest --test-dir build -C Release --output-on-failure --timeout 120` | **17/17 passed** |

> 测试统一在 `QT_QPA_PLATFORM=offscreen` 下运行；未删除任何断言、未注释失败用例、未放宽比较条件。
> 新目标用例逐条核验：`test_win32_observer` **12 个用例全 PASS**（Totals: 14 passed incl. init/cleanup），
> `test_work_state` **18 个用例全 PASS**（Totals: 20 passed incl. init/cleanup）。
> 实际踩坑 **2 条**：TRAP-P7-006（判定顺序缺陷，真实数据接线后暴露）、
> TRAP-P7-007（默认装配误关低层钩子，由新单测发现）——均按规范复现留证，见 `traps-P7.md`。

**人工目视项（待用户复验，自动化无法替代）**：

1. 勾选「工作状态感知」后，在 VS Code / 终端 / 浏览器之间切换并保持输入，
   桌宠立绘与台词应在一个采样周期（1s）内跟随工作状态变化；
2. 停止输入 20s 应转为「空闲」、3 分钟应转为「离开」；锁屏（`Win + L`）应立即转为「离开」，解锁后恢复；
3. 勾选后本地 `POST http://127.0.0.1:<port>/rpc`（需同时打开「本地 Context API」）
   的 `context.snapshot` 中 `env.appId` / `env.windowTitle` / `env.idleMs` / `env.inputEvents` 有真实值；
4. 取消勾选后：日志出现钩子卸载（若曾安装），且不再产生采样。

**未做项（如实说明）**：未在真实多显示器 / 高 DPI 环境下长时运行；未做杀软误报的实测
（降级路径已实现并有单测，但「真实被杀软拦截」的场景需用户环境验证）；未做性能采样
（新增开销仅为 1s 一次的系统调用 + 钩子回调里的一次原子自增）。

## P7.2 通道启用与 MCP 桥接

| 项 | 内容 |
|---|---|
| 交付物 | 设置项真实生效（HTTP 回环监听 + 命名管道）；`whalepet-mcp.exe` 控制台桥接（stdio ↔ 本地通道）；`initialize` / `tools/list` / `tools/call` 全链路 |
| 验收 | 任一 MCP 客户端可列举并调用 `context.snapshot`；关闭总开关后端口与管道均不监听；令牌开启时非法请求被拒 |
| 风险 | 子进程/通道鉴权与生命周期；打包需新增 exe 与管道命名约定（同步 `packages.md`） |

## P7.3 动态插件（DLL）

| 项 | 内容 |
|---|---|
| 交付物 | `plugins/` 目录扫描装载、IID/`apiVersion` 协商、失败降级；示例插件（如 `ext.hello`） |
| 验收 | 放入合法 DLL 后 `capabilities.list` 出现其能力；版本不匹配的 DLL 被跳过且主程序正常启动 |
| 风险 | ABI 稳定性；`docs/packages.md` §2/§5 安装/卸载/权限三处对应表必须同步 |

## P7.4 外部进程插件（MCP Client）✅ 已完成（2026-10-02）

### 交付物

| # | 交付项 | 代码位置 |
|---|---|---|
| 1 | 配置值类型（pluginId / program / arguments / timeoutMs） | `src/plugin/process/ProcessServerSpec.h` |
| 2 | stdio JSON-RPC 客户端：启动 / `Content-Length` 分帧收发 / 请求应答配对 / **同步（握手）+ 异步（调用）双通道** / 异步超时 | `src/plugin/process/McpStdioClient.{h,cpp}` |
| 3 | 单进程会话：`initialize` 握手 → `notifications/initialized` → `tools/list` 发现 → 注册 `ext.<pluginId>.<tool>` → `tools/call` 异步转发 → 崩溃隔离 | `src/plugin/process/McpPluginSession.{h,cpp}` |
| 4 | 编排（`QObject`）：配置校验 → 逐个 `start()` → 能力可用性联动 → `stop()` 清理 | `src/plugin/process/ProcessPluginLoader.{h,cpp}` |
| 5 | 组合根：按 `<数据目录>/plugins.json` 拉起（文件不存在则零开销） | `PetWindow::setupProcessPlugins` |
| 6 | 测试：真实子进程端到端（`mcp_test_server`） | `tests/test_process_plugin.cpp`、`tests/mcp_test_server.cpp` |

### 验收标准

- [x] 配置校验沿用既有语义（空 `pluginId` / 空 `program` / 非法 `timeoutMs` / 重复 `pluginId`）且不静默。
- [x] 拉起 + 握手 + 发现成功：`ext.<pluginId>.echo / sleep / fail / crash` 出现在能力表，`origin = Process`。
- [x] `tools/call` **异步**转发（返回 `false` + 取走回调），结果经 `InvokeContext` 回投。
- [x] 工具自报错误码**原样透传**（`-32001`）。
- [x] 调用超时（默认 2s）→ `requestFailed(kRpcErrorCapabilityFailed)`，**不静默挂起**。
- [x] **崩溃隔离**：子进程异常退出后 pending 调用回投 `-32002`、该来源全部能力标记为不可用、
      `sessionExited` 广播、**其余能力与主进程不受影响**。
- [x] 既有 18 个测试目标零回归（CTest 18 → 19）。

### 不做（P7.4 范围外）

- **心跳**与**自动重启**：本期为「退出即标记不可用」（不自动拉起），与文档「只把该来源能力标记
  为不可用」一致；重启策略需先明确退避与抖动，留待后续。
- 真实第三方插件生态：`plugins.json` 的具体部署方式仍属 P7.2/P7.3 的打包范畴。

## P7.5 ACP / IDE Agent 集成 ✅ 已完成（2026-10-02）

### 交付物

| # | 交付项 | 代码位置 |
|---|---|---|
| 1 | `ISignalSource` 实现：JSONL 信号文件增量读取（顺序 / 截断重置 / 未换行尾部 / 非法行忽略） | `src/contextapi/acp/AcpSignalSource.{h,cpp}` |
| 2 | `IAgentBridge` 实现：会话生命周期（幂等）+ 事件推送落盘 | `src/contextapi/acp/AcpAgentBridge.{h,cpp}` |
| 3 | 信号 → 工作态纯映射（kind 表 + `payload.state` 显式定态 + confidence/holdMs 覆盖） | `src/contextapi/acp/AcpSignalRules.{h,cpp}` |
| 4 | 编排：1s 级轮询 → 广播覆盖性工作态 | `src/viewmodel/AcpSignalService.{h,cpp}` |
| 5 | `WorkStateService` **显式信号覆盖窗口**（窗口内优先于推断，过期回落） | `src/viewmodel/WorkStateService.{h,cpp}` |
| 6 | 组合根：右键菜单「ACP / IDE 信号」勾选项（默认关） | `PetWindow::setupAcp` / `setAcpEnabled` |
| 7 | 测试 | `tests/test_acp.cpp` |

### 验收标准

- [x] 显式信号**可覆盖推断结果**：`agent.turn` → `vibe-coding`，且 `WorkStateService` 在
      `holdMs` 窗口内不被 `EnvSample` 推断改写（`test_acp::workStateServiceOverrideWinsUntilExpiry`）。
- [x] 窗口过期后**回到推断**（IDE 关闭不会让桌宠停在旧状态）。
- [x] 未识别信号**不映射、不覆盖**（不猜）；缺 `kind` / 非法 JSON 的行被忽略并计数。
- [x] 默认**关**（`acp_enabled = false`）时不轮询信号文件（零开销）。
- [x] 既有 19 个测试目标零回归（CTest 19 → 20）。

### 关键取舍

- **传输选 JSONL 文件**而非 socket / 管道：零新依赖、与主进程彻底解耦（IDE 崩溃不影响桌宠），
  且符合「只固定谁在什么时候告诉我什么，不固定传输」的既定原则；换传输只需换实现类。
- **协议仍在演进**：本期只固定「信号 → 覆盖性工作态」这条最小链路，未引入 ACP 的完整报文层，
  避免过度设计。

---

## P7.6 ACP（Agent Client Protocol）实时状态接入 ✅ 已完成（2026-10-02）

> 准入评估见 **`docs/ACP-EVAL.md`**。目标即本阶段总目标：让桌宠**实时**获取 Vibe Coding
> 状态（思考 / 编写 / 调试 / 报错）并做出反馈。

### 已交付

| # | 交付项 | 代码位置 |
|---|---|---|
| 1 | `session/update` → `CoreSignal` **纯映射** | `src/contextapi/acp/AcpEventMapper.{h,cpp}` |
| 2 | ACP 事件 → 工作态映射（`AcpSignalRules` 新增 13 条 kind） | `src/contextapi/acp/AcpSignalRules.cpp` |
| 3 | **真实 dsh 报文**夹具（原始采集，非手写） | `tests/fixtures/acp-real-events.json` |
| 4 | 真实报文驱动的测试（兼「事件形状漂移」回归守卫） | `tests/test_acp_event_mapper.cpp`（CTest 20 → 21） |
| 5 | **ACP 客户端**：NDJSON 分帧 + JSON-RPC + `QProcess` 子进程 + `initialize` / `session/new` / `session/list` / `session/resume` / `session/prompt` / `session/cancel` + 权限自动应答 + 崩溃隔离 | `src/contextapi/acp/AcpClient.{h,cpp}` |
| 6 | 信号投喂入口（ACP 事件与文件轮询**共用**映射/广播路径） | `AcpSignalService::submitSignal` |
| 7 | 设置项与组合根装配（启停子进程、建/接管会话） | `SettingsData.h` / `SettingsRepo.cpp`（`acp_dsh_path` / `acp_profile` / `acp_workspace`）、`PetWindow::startAcpClient` / `attachAcpSession` |
| 8 | 假 ACP Agent 子进程 + 客户端端到端测试 | `tests/acp_test_agent.cpp`、`tests/test_acp_client.cpp`（CTest 21 → 22） |

### 环境事实（本机实测）

- `DSH_HOME = C:\Users\19117\.dsh`；dsh 本体 `@deepseek-ai/dsh@0.1.5-rc.3`（npm 全局安装）；
- **ACP 服务** = `dsh --profile acp` → **stdio NDJSON JSON-RPC**（`acp` profile 首用自动初始化）；
- `@deepseek-ai/dsh-acp` 为 **MIT**，依赖 `@agentclientprotocol/sdk 1.4.0`（Apache-2.0）；
- ⚠️ **`npx @deepseek-ai/dsh web`（127.0.0.1:3080）≠ ACP**：web profile 是 HTTP 服务，
  其 bundles 中**不含** `dsh-acp`，不能作为 ACP 数据源。

### 验收标准

- [x] 真实 `session/update` 序列（thought / tool_call / tool_call_update / message / usage）逐条正确映射；
- [x] 工具细分依据 **`title`（工具名）**——实测 dsh 的 `kind` 恒为 `other`，不可作分类依据；
- [x] 未知变体 / 畸形输入：不产生信号、不崩溃（不猜测、不误判）；
- [x] 事件 → 工作态：`agent.thought`→`vibe-coding`、`tool.command`/`tool.error`→`debugging`，
      而 `tool.done` / `agent.usage` **不改写**当前状态；
- [x] `AcpClient` 端到端（假 Agent 驱动）：握手 / `session/new` / `session/list` + `resume` /
      prompt 事件映射 / 权限自动应答 / **崩溃隔离**（`test_acp_client`）；
- [x] **真实 DeepSeek Harness 端到端通过**：`dsh --profile acp` →
      `initialize`（agent = `deepseek-harness-acp`）→ `session/new` → `session/prompt` →
      `agent.message` 信号 → `stopReason = end_turn`；
- [x] 既有 21 个测试目标零回归（CTest 21 → 22），Debug / Release 各 **22/22**。

### 人工目视项（自动化无法替代，待用户复验）

1. 填入 `acp_dsh_path` 并勾选「ACP / IDE 信号」后，日志应出现
   `[AcpClient] 握手完成：agent = "deepseek-harness-acp"` 与「已接管 / 已新建 ACP 会话」；
2. dsh 侧发起一次带工具的工作后，桌宠立绘 / 台词应随事件切换
   （`agent.thought` → vibe-coding、`tool.command` → debugging、`tool.error` → debugging）；
3. 取消勾选后子进程被回收（日志 `[AcpClient] Agent 进程退出`），且不再产生覆盖。

### 不做（P7.6 范围外，留待后续）

- ACP v2（Draft）；
- 权限交互 UI（当前策略为「自动允许一次」，可经 `AcpClient::setAutoApprovePermissions(false)` 关闭）；
- 由桌宠主动发起 `session/prompt`（桌宠定位是「陪伴者」，默认只旁观；主动驱动属后续阶段）；
- 引入官方 SDK（官方**无 C++ 绑定**，见 `ACP-EVAL.md` §5）。

---

## 阶段依赖

```
P7.0（骨架）✅
 ├─► P7.1（真实感知）✅
 ├─► P7.2（通道 + MCP 桥接）── 待实施（桥接 exe / 命名管道）
 ├─► P7.3（DLL 插件）── 待实施（加载器已实现，未接线）
 ├─► P7.4（外部进程插件 / MCP Client）✅
 ├─► P7.5（ACP / IDE 集成）✅
 └─► P7.6（ACP 实时状态接入）✅
```

> **口径（2026-10-02 更新）**：MCP / ACP 的**预留接口**均已落地为具体实现（P7.4 / P7.5 / P7.6）。
> 仍未交付的是 P7.2 的控制台桥接 exe 与命名管道通道、P7.3 的 DLL 插件接线与产物——它们是独立交付物，
> 不属于「预留接口」；因此 `StdioTransport`（MCP Server 侧）本身虽已实现并可单测，
> 但**尚无真实进程中转**，MCP Server 通道在运行期仍未启用。
> 逐项配置核查（实现 / 接线 / 打包 / 测试）见 **`docs/P7-REMAINING-INTERFACES-AUDIT.md`**。

## 验证记录（2026-10-02，P7.4 / P7.5 实现）

环境：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，生成器 `Visual Studio 18 2026`。

| 步骤 | 命令 | 结果 |
|---|---|---|
| configure | `cmake --preset vs-debug` | 成功 |
| build Debug | `cmake --build build --config Debug --parallel` | 成功（零警告） |
| test Debug | `ctest --test-dir build -C Debug --output-on-failure --timeout 120` | **20/20 passed** |
| build Release | `cmake --build build --config Release --parallel` | 成功 |
| test Release | `ctest --test-dir build -C Release --output-on-failure --timeout 120` | **20/20 passed** |

新增测试目标：

- `test_acp`（9 个用例）：`AcpSignalSource` 增量/顺序/非法行/未换行尾部/截断重置/`setFilePath`；
  `AcpAgentBridge` 会话幂等与事件落盘；`AcpSignalRules` kind 映射与 `payload` 显式覆盖；
  `AcpSignalService` 轮询→映射→广播；`WorkStateService` 覆盖窗口与过期回落。
- `test_process_plugin`（6 个用例）：以真实子进程（`mcp_test_server`）端到端验证
  配置校验 / 握手与能力发现 / `tools/call` 异步转发 / 错误透传 / 调用超时 / 崩溃隔离。

> 未删除任何断言、未注释失败用例、未放宽比较条件。
> 本阶段**未出现崩溃**（无异常退出 / 访问违例），无需按 `docs/README.md` §六 交回调试。
> 实际踩坑 1 条（测试桩 server 用 `QFile(FILE*)` 读 stdin 导致子进程不可用，
> 改为标准 C stdio 后解决）见 `traps-P7.md` TRAP-P7-008。

## 验证记录（2026-10-02，P7.6 实现）

环境同上（Qt **6.8.4** + MSVC VS 18 2026 + CMake **4.4.2**）。

| 步骤 | 命令 | 结果 |
|---|---|---|
| test Debug | `ctest --test-dir build -C Debug --output-on-failure --timeout 120` | **22/22 passed** |
| test Release | `ctest --test-dir build -C Release --output-on-failure --timeout 120` | **22/22 passed** |
| 真实 dsh 端到端 | `dsh --profile acp` → `initialize` / `session/new` / `session/prompt` | 通过（agent = `deepseek-harness-acp`，`stopReason = end_turn`） |

新增测试目标（CTest **20 → 22**）：

- `test_acp_event_mapper`：以**真实 dsh 报文夹具** `tests/fixtures/acp-real-events.json`
  驱动 `AcpEventMapper`（ACP `session/update` → `CoreSignal`）与 `AcpSignalRules`（→ 工作态），
  兼作「协议形状漂移」回归守卫。
- `test_acp_client`：以假 Agent 子进程 `tests/acp_test_agent` 端到端验证启动 + `initialize` 握手 /
  `session/new` / `session/list` + `session/resume` / `session/prompt` 事件映射 /
  权限自动应答 / **Agent 崩溃隔离**；含可选 `realDshSmokeOrSkip`（设 `WHALEPET_ACP_REAL_DSH` 时
  用真实 DeepSeek Harness 跑）。

> 实际踩坑 1 条（TRAP-P7-009：`signals` 是 Qt 关键字宏，用作变量名导致大量「语法错误: public」）
> 见 `traps-P7.md`。

> **当前测试总量（静态核对）**：`CMakeLists.txt` 注册 **22 个测试目标**
> （Windows 下；`test_win32_observer` 为 `WIN32` 条件目标）。本轮文档整理**未复跑 CTest**，
> 上述 22/22 取自 P7.6 交付时的实测记录。

## 完成标记

P7.0 全部验收通过后，按 `docs/README.md` §二 约定在本文件与索引中更新状态；
本阶段整体完成后，本文件重命名为 `ROADMAP-P7-Fin.md`。
⚠️ **当前尚不具备改名条件**：**P7.2（命名管道 + `whalepet-mcp.exe` 桥接）与
P7.3（`plugins/` DLL 插件接线与产物）仍待实施**——逐项核查见
`docs/P7-REMAINING-INTERFACES-AUDIT.md`。
