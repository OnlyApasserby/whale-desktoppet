# ROADMAP · P7 — 插件化智能桌宠与本地 Context API

> 本阶段把 WhalePet 从「桌宠外壳 + 养成 + 小游戏」扩展为**插件化智能桌宠系统**：
> 感知桌面环境 → 判定工作状态（含 Coding / Vibe Coding）→ 驱动桌宠行为，
> 并以**本地 Context API**（MCP stdio + 本地回环）对外开放。
>
> 前置：P0–P6 全部完成（`ROADMAP-P6-Fin.md`，版本 0.2.0，12/12 测试通过）。
> 架构依据：`PLUGIN-ARCHITECTURE.md`；对外协议：`CONTEXT-API.md`；踩坑：`traps-P7.md`。

---

## 阶段总览

| 阶段 | 目标 | 交付物 | 状态 |
|---|---|---|---|
| **P7.0** | 设计 + 重构骨架 | 设计文档、通用能力总线、感知/状态/Context API 接口与骨架、4 个新测试 | ✅ 已完成（2026-10-02） |
| **P7.1** | 真实桌面感知 | `Win32DesktopObserver`（前台窗口 / 进程名 / 空闲 / 键鼠计数 / 会话状态）、观察者生命周期、`WorkStateRules` 按真实数据回归调参、1 个新测试 | ✅ 已完成（2026-10-02） |
| **P7.2** | 通道启用与 MCP 桥接 | `context_api_enabled` 生效、本机 HTTP/命名管道可访问、`whalepet-mcp.exe` 控制台桥接 | 待实施 |
| **P7.3** | 动态插件（DLL） | `plugins/` 目录扫描、IID/版本协商、打包三处对应表同步 | 待实施 |
| **P7.4** | 外部进程插件（MCP Client） | 子进程生命周期、`tools/list` 能力发现、超时/心跳/崩溃隔离 | 待实施 |
| **P7.5** | ACP / IDE Agent 集成 | 显式信号源（IDE 扩展 / 文件保存 / diff）与会话桥接的具体协议实现 | 待实施 |

> **P7.1 口径**：MCP / ACP 在本阶段仍是**预留接口**（`CONTEXT-API.md` §6 的两个纯虚接口，
> 不注册能力、不建立会话、不落协议实现）；真实通道与桥接 exe 属 P7.2，ACP 属 P7.5。

---

## P7.0 设计 + 重构骨架

### 阶段目标

给出可实施的技术方案与开发路径，并落地**不改动既有行为**的架构骨架。

### 交付物

1. **设计文档**：`PLUGIN-ARCHITECTURE.md`、`CONTEXT-API.md`、本文件、`traps-P7.md`；
   更新 `docs/README.md`（索引 + 口径）、`ARCHITECTURE.md`（分层 + 依赖）、
   `STATE-MACHINE.md`（工作态通道）、`SETTINGS.md`（新设置项）、`packages.md`（插件目录约定）。
2. **通用能力总线**（`whalepet_plugin`）：`Capability` / `PluginInterface` / `PluginRegistry` /
   `BuiltinPluginLoader` / `DllPluginLoader` / `ProcessPluginLoader`。
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

## P7.4 外部进程插件（MCP Client）

| 项 | 内容 |
|---|---|
| 交付物 | `ProcessPluginLoader` 完整实现：拉起/握手/心跳/超时/重启/退出清理；`ext.<pluginId>.*` 能力映射 |
| 验收 | 外部插件进程被强杀后，主程序不退出且相关能力标记为不可用；其余能力不受影响 |
| 风险 | 超时与重试策略需可观测（日志 + `session.stats`） |

## P7.5 ACP / IDE Agent 集成

| 项 | 内容 |
|---|---|
| 交付物 | `ISignalSource` / `IAgentBridge` 的具体实现（IDE 扩展、文件保存 / diff 事件、ACP 会话） |
| 验收 | 显式信号可覆盖推断结果（例如 IDE 直接上报「正在与 Agent 快速迭代」→ `vibe-coding`） |
| 风险 | 协议仍在演进，实现前需重新评估范围，避免过度设计 |

---

## 阶段依赖

```
P7.0（骨架）✅
 ├─► P7.1（真实感知）✅ ◄── 本期完成
 ├─► P7.2（通道 + MCP 桥接）◄── 依赖 P7.1 才有真实上下文可查
 ├─► P7.3（DLL 插件）
 └─► P7.4（外部进程插件）◄── 依赖 P7.2 的 MCP Client 基础
        └─► P7.5（ACP / IDE 集成）
```

> MCP / ACP 两个方向在本阶段（P7.0 / P7.1）只保留**接口**：
> `StdioTransport` 与 `LocalHttpTransport` 已实现并可单测，但没有真实通道启用与桥接 exe；
> `ISignalSource` / `IAgentBridge` 只定义抽象类，**不注册任何能力、不建立任何会话**。

## 完成标记

P7.0 全部验收通过后，按 `docs/README.md` §二 约定在本文件与索引中更新状态；
本阶段整体（P7.0–P7.5）完成后，本文件重命名为 `ROADMAP-P7-Fin.md`。
