# 插件化架构与能力总线（PLUGIN-ARCHITECTURE）

> 本文档定义 WhalePet 从「小游戏专用插件机制」泛化为**通用分层插件总线**的模块划分、插件接口、
> 统一 capability 协议、三层装载方式与数据流，是 `CONTEXT-API.md`（对外接口）与 `ROADMAP-P7-Fin.md`
> （实施路径）的架构依据。
>
> 关联文档：`ARCHITECTURE.md`（总体分层）、`MINIGAME-INTERFACE.md`（既有小游戏插件机制）、
> `STATE-MACHINE.md`（状态驱动表现）、`SETTINGS.md`（设置项落位）、`TESTING.md`（测试策略）。

---

## 1. 目标与范围

把桌宠扩展为**插件化智能桌宠系统**，能力全部以插件/能力（capability）形式接入，宿主只按接口驱动：

| 能力 | 归属（可拆可合） |
|---|---|
| 桌面环境感知 | `platform` 层 + 感知插件 |
| 工作状态判定（含 Coding / Vibe Coding） | `core::WorkStateRules`（零 Qt 纯逻辑） |
| 状态驱动桌宠行为 | `PetController` + `core::PetStateMachine` |
| 本地 Context API | `contextapi` 层（能力由内置插件注册） |
| MCP 功能扩展 | 外部进程插件（三层之一） |
| ACP / IDE Agent 集成 | `contextapi` 的 `ISignalSource` / `IAgentBridge`（**P7.5 实现 + P7.6 ACP 客户端**，已落地） |

**架构硬约束**：新增任意能力**不得**要求修改宿主的具体分支——宿主（`PetWindow`）只按
`PluginRegistry` / `CapabilityRegistry` 驱动，这与既有 `MINIGAME-INTERFACE.md` §2.1 的约定同源。

---

## 2. 重构评估结论

**结论：不需要推倒重来，采用「渐进式泛化 + 净增层」。**

| 现有资产 | 处置 | 理由 |
|---|---|---|
| `whalepet_core`（零 Qt 纯逻辑，脱 UI 可测） | **直接复用**，新增 `WorkState*` | 状态判定天然属于零 Qt 层，可注入时钟/采样序列单测 |
| `IMiniGamePlugin` / `MiniGameContext` / `MiniGameInfo`（元数据 + 依赖注入 + 接口） | **泛化为模板**：`IPlugin` / `PluginContext` / `PluginInfo` | 原设计已具备「宿主零分支」的全部要素 |
| `MiniGameRegistry`（注册表 + 唯一注册点） | **保留公开 API 不变**，内部同步注册进通用总线 | 既有菜单/设置页/结算链路与 12 个测试零回归 |
| `viewmodel` Service 编排 + `PetController` 唯一调度点 | **复用**，新增 `EnvironmentService` / `WorkStateService` | 不引入第二套调度 |
| `model` SQLite + `json_ext` 扩展机制 | **复用** | 新设置项不建新列，向后兼容既有库 |
| View 层 Qt Widgets | **复用**，仅组合根加装配 | 表现栈与动效链路完全不改 |

现有代码库中**没有任何**网络 / IPC / 动态加载痕迹（`find_package` 原仅
`Core Gui Widgets Sql Test`），因此 `platform` / `plugin` / `contextapi` 三块属**净增**；
唯一需要改动既有代码的是三处小改：**注册表泛化**、**状态机新增工作态通道**、**组合根装配**。

---

## 3. 分层与依赖方向

依赖**只能自上而下**（箭头 = 依赖方向），禁止反向或跨层直达：

```
                     ┌──────────────────────── WhalePet (exe, app/main.cpp) ─────────────────────┐
                     │                                                                          │
                     ▼                                                                          │
┌──────────────────────────────── whalepet_view (Qt Widgets) ──────────────────────────────────┐│
│ view/        PetWindow(组合根) / PoseView / SpeechBubble / SettingsDialog / *Panel           ││
│ viewmodel/   PetController / PosePresenter / *Service / MiniGameService                       ││
│              >>> 本期新增：EnvironmentService / WorkStateService / PetContextProvider <<<     ││
│ minigame/    IMiniGamePlugin / MiniGameRegistry / MiniGameCompatAdapter / 各小游戏插件         ││
└───┬──────────────┬─────────────────┬───────────────────┬───────────────────────────────────┘│
    │              │                 │                   │                                        │
    ▼              ▼                 ▼                   ▼                                        │
┌─────────┐  ┌───────────┐  ┌──────────────┐  ┌──────────────────────┐                          │
│ model   │  │ platform  │  │ plugin       │  │ contextapi           │                          │
│ (Sql)   │  │ (净增)     │  │ (净增)        │  │ (净增)                │                          │
│ DB/Repo │  │ 感知接口   │  │ 能力协议/注册表│  │ JsonRpc/双通道/ACP  │                          │
│ json_ext│  │ 空实现     │  │ 三层装载器    │  │ IContextProvider      │                          │
└────┬────┘  └─────┬─────┘  └──────┬───────┘  └──────────┬───────────┘                          │
     │             │               │                     │                                        │
     └─────────────┴───────────────┴─────────────────────┘                                        │
                             ▼                                                                     │
              ┌────────────────────────── whalepet_core (零 Qt) ──────────────────────────┐        │
              │ PetStateMachine / PetTypes / WorkState / WorkStateRules / GrowthRules /   │        │
              │ ChatRules / Achievements / Quests / MiniGameTypes / Minesweeper / RobotKitten │      │
              └──────────────────────────────────────────────────────────────────────────────┘        │
```

### 3.1 为什么 `contextapi` 不依赖 `view`

`ContextApiService` 需要的运行期数据（工作状态、养成状态、环境快照）通过**接口**
`contextapi::IContextProvider` 获取，实现类 `viewmodel::PetContextProvider` 落在
`whalepet_view` 内。这样 `whalepet_contextapi` 可独立编译与单测（测试注入假 provider），
也避免 `whalepet_view ↔ whalepet_contextapi` 的循环依赖。

### 3.2 CMake 目标与依赖表

| 目标 | 类型 | 链接 | 说明 |
|---|---|---|---|
| `whalepet_core` | STATIC | —（仅 C++17） | 既有 + `WorkState*` |
| `whalepet_platform` | STATIC | `Qt6::Core`、`whalepet_core`（Windows 另加 `user32`） | 感知接口 + 空实现 + P7.1 真实 Win32 采集，净增 |
| `whalepet_model` | STATIC | `Qt6::Core`、`Qt6::Sql`、`whalepet_core` | 既有（`SettingsRepo` 扩展键） |
| `whalepet_plugin` | STATIC | `Qt6::Core`、`whalepet_core` | 能力协议 / 注册表 / 三层装载器，净增 |
| `whalepet_contextapi` | STATIC | `Qt6::Core`、`Qt6::Network`、`whalepet_core`、`whalepet_plugin` | JSON-RPC / 三通道（HTTP + stdio + 命名管道）/ ACP 实现，净增 |
| `whalepet_view` | STATIC | 上述全部 + `Qt6::Gui`、`Qt6::Widgets`、`user32` | 既有 + 三者装配 |
| `WhalePet` | WIN32 exe | `whalepet_view` | 既有 |
| `whalepet-mcp` | 控制台 exe（**非 WIN32**） | `whalepet_contextapi` | P7.2：stdio ↔ 命名管道桥接进程，与 `WhalePet.exe` 同目录、随包分发 |

> **依赖口径变更**：`ARCHITECTURE.md` §2 原写「零新依赖」。本期收敛为
> **「零第三方依赖，允许 Qt 官方模块」**——新增 `Qt6::Network`（`QTcpServer` / `QLocalServer`），
> 仍不引入任何第三方库，JSON 序列化用 `Qt6::Core` 的 `QJsonDocument`。

---

## 4. 插件三层接入（同一套 capability 协议）

| 层 | 装载器 | 形态 | 隔离性 | 适用 |
|---|---|---|---|---|
| **内置** | `BuiltinPluginLoader` | 随主程序编译（进程内静态注册） | 无隔离，零开销 | 核心能力（minigame / context） |
| **动态** | `DllPluginLoader` | `QPluginLoader` 加载独立 `.dll` | 进程内，崩溃即连带 | 第三方独立发布 |
| **外部进程** | `ProcessPluginLoader` | 独立进程（MCP Server，stdio JSON-RPC） | **进程隔离**，语言无关 | 高风险 / 多语言扩展 |

三层对宿主**不可区分**：三者都向同一个 `CapabilityRegistry` 注册 `ICapability`，
宿主与传输通道只按 capability id 查找。差异只在 `CapabilityDescriptor::origin`
（`Builtin` / `Dll` / `Process`），用于诊断与优先级仲裁。

**冲突仲裁**：同一 capability id 被重复注册时，按 `Builtin > Dll > Process` 优先保留，
其余记 `qWarning` 并丢弃（**不静默**，与 `main.cpp` / `PetWindow` 的既有日志风格一致）。

### 4.1 动态插件（DLL）的 ABI 边界

- DLL 侧实现 `IPluginFactory`（`src/plugin/dll/IPluginFactory.h`），经
  `Q_DECLARE_INTERFACE(whalepet::plugin::IPluginFactory, "ai.whalepet.PluginFactory/1.0")` 暴露；
  **IID 内的 `/1.0` 即 ABI 版本**，破坏性变更必须提升版本号，宿主按 IID 前缀做版本协商。
- DLL 内嵌 `Q_PLUGIN_METADATA(... FILE "metadata.json")` 元数据（id / 显示名 / `apiVersion`）。
- 加载失败（缺符号 / 版本不匹配 / `apiVersion` 高于宿主）**只记日志并跳过**，
  绝不 `Fatal`、绝不影响主进程与其它插件。
- 部署位置（**已落地**）：`<安装目录>/plugins/`（启动时按目录扫描）；打包侧已同步
  `scripts/package-release.ps1`、`scripts/installer.nsi` 与 `docs/packages.md` §2/§5/§8 清单。
  ✅ **当前状态（P7.3 已交付）**：`DllPluginLoader` **已接入组合根**——`PetWindow::setupDllPlugins()`
  以 `QCoreApplication::applicationDirPath() + "/plugins"` 构造加载器并 `loadAll(m_plugins)`，
  且在**构建菜单之前**执行（菜单项由已装载插件动态生成）。因此**放合法 DLL 进 `plugins/` 即生效**；
  不兼容 / 非法 DLL 被跳过并记日志，主程序照常启动。示例插件 `ext_hello` / `ext_badabi`
  与测试 `test_dll_plugin` 见 `docs/ROADMAP-P7-Fin.md` P7.3。

### 4.2 外部进程插件（MCP Client）

`ProcessPluginLoader` 作为 **MCP Client** 拉起外部进程：

```
启动子进程(stdio) → initialize(握手/协议版本) → tools/list(能力发现)
   → 每个 tool 映射为一个 ICapability（id 前缀 "ext.<pluginId>."）
   → tools/call 转发调用；超时(默认 2s) / 心跳 / 退出检测
```

- 调用**异步**：`ICapability::invoke` 返回 `false` 表示「已受理、结果经 `InvokeContext` 回投」，
  禁止阻塞 GUI 线程。
- 子进程退出 / 崩溃 → 仅把该来源的能力标记为不可用（`CapabilityRegistry::setAvailable`），
  主进程不受影响；这是选「外部进程」层的**唯一理由**（崩溃隔离）。
- 退出时统一 `stop()` 并等待子进程结束（避免孤儿进程）。

**实现（P7.4，已落地）**——三个类各司其职：

| 类 | 职责 |
|---|---|
| `McpStdioClient` | `QProcess` + `Content-Length` 分帧 JSON-RPC 客户端：启动 / 分帧收发 / 请求应答配对 / **同步（握手）与异步（tools/call）双通道** / 异步超时 |
| `McpPluginSession` | 一个子进程的会话：`initialize` 握手 → `notifications/initialized` → `tools/list` 发现 → 注册 `ext.<pluginId>.<tool>` 能力 → `tools/call` 异步转发；进程退出时 `failAllPending` + 广播能力不可用 |
| `ProcessPluginLoader`（`QObject`） | 编排：配置校验 → 逐个 `start()` → 会话生命周期与能力可用性联动 → `stop()` 清理 |

关键取舍：

- **`ext.<pluginId>.<tool>` 命名**：`pluginId` 由配置给出，tool 名来自 `tools/list`，因此
  外部插件无需知道宿主能力表；能力 `origin = Process`（`readOnly = false`）。
- **同步只在启动期**：握手 / 能力发现用带超时的同步请求（阻塞可接受且失败可诊断）；
  `tools/call` 一律异步，结果经 `InvokeContext` 回投——**不阻塞 GUI 线程**。
- **超时可观测**：单次调用超时（`ProcessServerSpec::timeoutMs`，默认 2000ms）触发
  `requestFailed(kRpcErrorCapabilityFailed)`，**不静默挂起**。
- **崩溃隔离可核验**：子进程异常退出（`QProcess::CrashExit`）时，pending 调用以
  `kRpcErrorCapabilityUnavailable` 回投、该来源全部能力被标记为不可用，**其余能力与主进程不受影响**。
- **配置来源**：组合根从 `<数据目录>/plugins.json`（JSON 数组）读取
  `{pluginId, program, arguments, timeoutMs}`；文件不存在时**不启动任何外部进程**（零开销）。

---

## 5. 关键接口

```cpp
// src/plugin/Capability.h —— 三层共用的唯一能力协议
enum class PluginOrigin { Builtin, Dll, Process };

struct CapabilityDescriptor {
    QString id;             // 稳定标识："minigame.minesweeper" / "context.snapshot" / "ext.foo.bar"
    QString version;        // 语义化版本（协商用）
    QString displayName;
    QString description;
    PluginOrigin origin = PluginOrigin::Builtin;
    bool readOnly = true;   // context.* 只读；工具类可写
    QString paramsSchema;   // 轻量参数描述（JSON 字符串）；本期不做完整 JSON Schema
};

class ICapability {
public:
    virtual ~ICapability() = default;
    virtual CapabilityDescriptor descriptor() const = 0;
    // 返回 true：同步完成，out 已填充。
    // 返回 false：已受理（异步），结果经 ctx 回投；必须填 error 或由 ctx 回投结果。
    // 失败：填 error（code / message），**禁止异常穿越边界**。
    virtual bool invoke(const QJsonObject &in, InvokeContext &ctx,
                        QJsonObject &out, QJsonObject &error) = 0;
};

// src/plugin/PluginInterface.h —— 泛化自 MiniGamePlugin.h 的既有语义
struct PluginContext {              // 对应 MiniGameContext
    viewmodel::PetController *controller = nullptr;   // 表现播报（可空）
    model::Database *db = nullptr;                    // 持久化（可空 = 内存态）
    CapabilityRegistry *capabilities = nullptr;        // 插件间互相发现（可空）
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual PluginInfo info() const = 0;                  // 元数据驱动菜单/设置页
    virtual void registerCapabilities(CapabilityRegistry &registry) = 0;
    virtual bool start(PluginContext &ctx) { Q_UNUSED(ctx); return true; }
    virtual void stop() {}
};
```

**同步 / 异步语义（易错点，见 `docs/pitfalls/` TRAP-P7-005）**：

| `ICapability::invoke` 返回 | 含义 | 分发侧行为 |
|---|---|---|
| `true` + `out` 已填 | **同步成功** | 立即回 `result` |
| `true` + `error` 已填 | **同步失败** | 立即回 `error`（code 由能力自报） |
| `false` + 已 `takeResponder()` | **异步已受理** | 不阻塞，等回调 |
| `false` + 未取走回调 | **契约违反** | 回 `-32603`（不静默挂起） |

因此 `SimpleCapability`（同步便捷基类）内部**不**把 `call()` 的布尔值透传，而是固定返回 `true`
——`call()` 的返回值只表示「成功 / 失败」，失败信息放在 `error` 里。

> 既有 `IMiniGamePlugin` / `MiniGameView` / `MiniGameContext` / `MiniGameInfo`
> **定义与语义保持不变**（`docs/MINIGAME-INTERFACE.md` 仍然是它们的唯一规范）。

---

## 6. 数据流与状态流转

### 6.1 感知 → 状态 → 表现（主链路）

```
[platform] IForegroundSampler / IActivitySampler / ISystemStatusSampler
     │  P7.0：EmptyDesktopObserver（恒「无数据」）
     │  P7.1：Win32DesktopObserver（前台窗口 / 进程名 / 空闲 / 键鼠计数 / 会话状态）
     │  1s 级采样（复用既有 QTimer 机制，不新增轮询热点）
     │  生命周期：EnvironmentService::start/stop → setObserving(bool)
     │            （Win32 低层输入钩子只在采样期间安装，stop / 析构即卸载）
     ▼
core::EnvSample              （POD：appId / windowTitle / category / hasInput / idleMs /
     │                        inputEvents / appSwitches / dwellMs / nowMs）
     ▼
viewmodel::EnvironmentService  ──sampleReady(EnvSample)──►
     ▼
core::WorkStateRules::evaluate(sample, prev)   ← 纯函数 + 滞回（最短驻留）
     ▼
core::WorkStateSample { state, confidence, sinceMs }
     ▼
viewmodel::WorkStateService  ──workStateChanged(state, conf, since)──►
     ▼
PetController::handleWorkState(state)   （状态未变则直接返回，天然去抖）
     ▼
core::PetStateMachine (EventType::WorkStateChanged)
     ▼
core::PoseResult  →  PosePresenter  →  PoseView / SpeechBubble
```

### 6.2 状态机优先级（本期新增「工作态」一档）

```
一次性事件（点击 / 升级 / 成就 / 投喂 / 小游戏播报）
  > 工作态（Coding / VibeCoding / Debugging / Meeting / …）
    > 时段态（夜 → sleep）
      > 挂机态（afk / thinking / waiting）
        > 默认（idle-cute）
```

- **默认 `WorkState::Unknown`（无常感器 / 未启用）时行为与 P6 完全一致** —— 这是零回归的关键。
- **会话锁定 / 屏保（`systemPaused`）优先于「无数据」**：锁屏时前台窗口读不到，
  数据形状与「未启用感知」完全相同；若先判「无数据」会把「主人确定离开」误降级为 `Unknown`
  （见 `docs/pitfalls/` TRAP-P7-006），故 `systemPaused` 视为「有数据」并直接判 `Afk`。
- **不打断规则继续成立**：拖拽中 / 小游戏或设置面板打开（`setSuppressed`）时抑制主动表现。
- **专注态主动静默**：`workStateIsFocus(state)`（Coding / Debugging / Meeting）为真时，
  主动台词一律不说（`makeLine` 内统一把关），**唯一豁免是 `work.*` 场景本身**——
  即「状态显著变化时播报一句」。这样既满足「专注编码期间不打扰」，又满足
  「状态显著变化时出现」。

### 6.3 对外链路

```
CapabilityRegistry（三层插件注册的能力）
     │
ContextApiService ──► JsonRpcDispatcher（唯一分发核心：method 表 + 统一错误码 + 能力转发）
     │                        ▲                        ▲
     │                        │                        │
     │              StdioTransport(MCP)      LocalHttpTransport(127.0.0.1)
     ▼
IContextProvider（view 侧实现 PetContextProvider）→ ContextSnapshot
```

两条通道**共用同一 dispatcher 与同一 capability 注册表**：新增能力无需为通道写任何代码。

---

## 7. 小游戏插件的兼容策略（零回归红线）

**`MiniGameRegistry` / `IMiniGamePlugin` / `MiniGameView` / `MiniGameContext` / `MiniGameInfo`
以及 `registerBuiltinMiniGames()` 一行都不改**——泛化在**外层**完成，
由 `src/minigame/MiniGameCompatAdapter.{h,cpp}` 提供的

```cpp
int registerMiniGamePlugins(const MiniGameRegistry &minigames, plugin::PluginRegistry &registry);
```

把已注册的每个 `IMiniGamePlugin` 适配为 `IPlugin` 并注册进通用总线（能力 id = `minigame.<pluginId>`）。

- 采用「外层适配」而非「改 Registry 内部」的理由：既有小游戏链路（菜单 / 设置页 / 结算 / 成就）
  的调用点与断言**完全不受影响**，把回归面压到最小；代价是宿主组合根多一次显式调用
  （`PetWindow::setupMiniGames` 经 `BuiltinPluginLoader` 完成）。
- 适配器**不拥有** `IMiniGamePlugin`（所有权仍归 `MiniGameRegistry`）；`MiniGameRegistry`
  内部以 `std::vector<std::unique_ptr<...>>` 持有，对象地址稳定，不存在悬垂。
- 小游戏能力自动出现在 `capabilities.list` / MCP `tools/list` 中，
  **而小游戏插件本身完全不知道 capability 的存在**。
- 能力语义边界：`minigame.<id>` 只提供**元数据查询**（`readOnly`）；
  「打开游戏窗口」需要宿主界面上下文，仍由 `PetWindow::showMiniGame` 驱动（P7.3 起可再加宿主能力）。
- 红线：`test_smoke::miniGameMenuIsHoverSubmenu`（「小游戏…」子菜单文案 / 挂载方式 / 首项文案）
  与其余 **23 个**测试目标必须继续通过；不删除任何断言、不放宽任何条件（`docs/TESTING.md`）。

---

## 7.1 P9：宿主服务注册化 + 外部进程型深化（P9-A / P9-B，2026-10-05，✅ 验收通过）

依据 `docs/ROADMAP-P9-Fin.md`（立项裁决见 `ARCHITECTURE.md` §A.6，推翻 §A.5 的「暂不推进」），
在**不改动** §3 分层与 §7 红线的前提下净增：

**P9-A — 宿主服务经 builtin 层注册化**

- 5 个**零界面依赖**的服务（`GrowthService` / `StomachService` / `DialogueService` /
  `EasterEggService` / `RecycleBinService`）改为以 `IPlugin` 形式经 `BuiltinPluginLoader`
  注册进能力总线；宿主 `PetWindow` 从对应 5 个 `setup*` 中剥离装配。
- 落位 `src/viewmodel/builtin/`（属 `whalepet_view`，与 `MiniGameCompatAdapter` 同构），
  因此 `whalepet_plugin` **仍不反向依赖** view / model；注册回调仍由宿主提供
  （`registerBuiltinServicePlugins`）。
- 职责边界：插件负责「创建 / 启动 / 停止 / 向 controller 注入 / 注册能力 + **逻辑型**接线」；
  宿主保留「**UI 反应**」（状态面板刷新 / 对话面板 / 托盘气泡）——延续既有「表现与逻辑分离」原则。
- 新增 5 个**只读**状态能力：`service.growth` / `service.stomach` / `service.dialogue` /
  `service.easterEgg` / `service.recycleBin`（`origin = Builtin`，`readOnly = true`；
  服务未就绪返回 `-32002`，不伪造数据）。
- 宿主注入的唯一「窄回调」是预设对话的**静息门槛**
  （`BuiltinServiceHooks::dialogueCanAsk`）——**不构成** UI 宿主契约。
  （**2026-10-05 修订**：P9-A / P9-B 验收通过后，P9-C 已启动，将正式引入 G1 `IPluginUiHost`
  与 G2 贡献点协议，见 `ARCHITECTURE.md` §A.7 与 `docs/ROADMAP-P9-Fin.md` §2.3 / §3.2。）
- `Database` 仍由宿主创建（共享基础设施），经 `PluginContext.db` 传给插件。

**P9-B — 外部进程型深化**

- `plugins.json` 的解析从宿主下沉为 `plugin::ProcessPluginConfig`（纯逻辑、可脱 UI 单测）；
  宿主只保留「定位配置 → 加载 → 注册 → 启动」编排。
- 新增会话状态只读快照 `ProcessPluginLoader::sessionStates()`
  （`ProcessPluginStatus`：`pluginId` / `program` / `valid` / `running` / `toolCount` / `reason`），
  并在设置页新增 **「外部插件」只读列表**（查询不启动进程、不改变任何可用性）。

**验证**：新增 `test_service_plugins`、扩展 `test_process_plugin`；
Debug / Release CTest 均 **36/36**；`deploy-release/` 干净 PATH + offscreen 冒烟通过。
踩坑见 `docs/pitfalls/p9/`（`P-081` … `P-083`）。

**验收（2026-10-05）**：A1~A6 **逐条通过**（构建 / 测试 / 零回归 / 部署冒烟 / 文档与踩坑 / 阶段目标 / 用户确认），
详见 `docs/ROADMAP-P9-Fin.md` §3.1 / §6。**P9-A / P9-B 状态：✅ 验收通过。**

## 7.2 P9-C：UI 宿主契约与贡献点协议（✅ 2026-10-06 验收通过）

依据 `ARCHITECTURE.md` §A.7（修订 §A.6 中「P9-C 暂缓」的部分）：P9-A / P9-B 验收通过后启动 P9-C，
净增 **G1 `IPluginUiHost`**（宿主窗口句柄 / 父 `QWidget` / 生命周期回调）与 **G2 贡献点协议**
（右键菜单项 / 托盘项 / 设置页注册），使 UI 面板型插件可在**不修改宿主**的前提下注册 UI。

- **方案边界**：UI 型插件走**进程内 builtin 层**，**不引入 G4 导出宏 / 稳定 ABI 子集**；
  `MiniGameRegistry` 一族零改动；贡献点只「新增」，不改既有菜单 / 托盘语义。
- **交付物与验收**：见 `docs/ROADMAP-P9-Fin.md` §2.3 / §3.2（2026-10-06 经用户确认作为门禁固化；
  **C1~C6 逐条通过**）。
- **实现结果（2026-10-06）**：
  - 契约（`whalepet_plugin`，仅 `Qt6::Core`；`QWidget` 仅**前向声明**，不引入 Widgets）：
    `src/plugin/ui/IPluginUiHost.h`（G1：父窗口 / 原生句柄 / 生命周期 / 布局刷新 / 面板展示）；
    `src/plugin/ui/PluginContribution.h`（G2：右键菜单 / 托盘 / 设置页三类贡献点）；
    `IPlugin::contributions()` 默认空；`PluginRegistry::collectContributions()`（order 升序、同序保持注册顺序、去重）。
  - 宿主侧（`whalepet_view`）：`src/view/ui/UiContributionHost.*` 实现 `IPluginUiHost` 并按 kind 分发；
    `src/view/ui/StatusPanelUiPlugin.*`（试点 `builtin.statusPanel`，拥有 `StatusPanel` 视图，
    贡献右键 + 托盘「状态」，展示前经窄回调刷新并回传签到）；`PetWindow` 删除硬编码「状态」项，
    新增 `setupUiPlugins()` / `setupUiContributions()`。
  - 单测：`tests/test_ui_plugin_host`（9 用例，CTest **36 → 37**）。
  - 验证：Debug / Release 构建退出码 0；CTest 各 36/37（唯一失败为既有偶发 `P-064`，复跑通过）；
    `deploy-release/` offscreen 冒烟存活。
- **踩坑**：`P-084`（测试替身固定字段与断言期望不一致）；后续接续 `P-085` 起。

## 7.3 EX4：小游戏陪玩的「插件侧自描述 + 陪玩侧通用聚合」（2026-10-09，已完成）

目标：**新增小游戏时，陪玩代码零改动**。做法是把「状态折算」从陪玩侧**下沉到插件侧**：

- **插件侧（唯一接触点）**：`src/minigame/MiniGameCompanionSource.h` 的**可选**接口
`IMiniGameCompanionSource` —— 小游戏视图把自身状态折算为中立的 `core::GameSnapshot`。
**不改** `IMiniGamePlugin` / `MiniGameView` / `MiniGameRegistry` 任何签名（§7 红线保持）。
- **中立契约与判定（零 Qt）**：`core::GameSnapshot` + `core::MiniGameCompanion`。
- **陪玩侧通用聚合**：`viewmodel::MiniGameCompanionSource` 遍历宿主给出的可见小游戏自描述源，
取首个可用者 —— **该文件与 `core/MiniGameCompanion` 均不含具体玩法分支**。
- **最小侵入接入**：`IGameCompanionSource` 增加**默认实现**的可选出口 `readSnapshot()`
（默认 `false` → 回落既有 `read()`），故既有数据源与测试替身**无需改动**；
`GameCompanionService::tick()` 优先中立快照通道。既有 RPG 判定与其用例**零回归**。
- **宿主接线**：`PetWindow::setupGameCompanion()` 装配通用聚合源；`syncGameCompanion()`
随小游戏窗口显隐启停采样（无小游戏 → 零开销）。
- **验证**：新增 `test_minigame_companion`（含「一个全新游戏只需实现 `IMiniGameCompanionSource`
即被接入」的验证）；Debug / Release CTest 各 **33/33**。
- **边界声明（重要）**：小游戏**自身**仍是**内置层静态注册**插件（非 DLL）；
「陪玩自描述」是插件**能力**的扩展点，不是新的插件装载层。详见 `ARCHITECTURE.md` 附录 B.3 / B.6。

## 8. 目录结构（本期新增/改动）

```
src/
  core/       WorkState.h[新] WorkStateRules.{h,cpp}[新] PetTypes.h[改] PetStateMachine.{h,cpp}[改]
              WorkStateRules.cpp[P7.1 改：systemPaused 优先 + 实时输入 Coding 判据]
  platform/   DesktopObserver.h[新 P7.1 改：新增 setObserving 生命周期]
              EmptyDesktopObserver.{h,cpp}[新]
              Win32DesktopObserver.{h,cpp}[P7.1 新：三个真实采样器 + 低层输入钩子]
              Win32TextUtil.{h,cpp}[P7.1 新：UTF-16→UTF-8 / 路径取文件名]
  plugin/     Capability.{h,cpp}[新] PluginInterface.h[新] PluginRegistry.{h,cpp}[新]
              builtin/BuiltinPluginLoader.{h,cpp}[新]
              dll/IPluginFactory.h[新] dll/DllPluginLoader.{h,cpp}[新]
              process/ProcessServerSpec.h[P7.4 新]
              process/McpStdioClient.{h,cpp}[P7.4 新：stdio JSON-RPC 客户端]
              process/McpPluginSession.{h,cpp}[P7.4 新：握手/发现/转发/隔离]
              process/ProcessPluginLoader.{h,cpp}[P7.4 改为完整实现]
  contextapi/ IContextProvider.h[新] ContextSnapshot.{h,cpp}[新]
              JsonRpcDispatcher.{h,cpp}[新] ContextApiService.{h,cpp}[新]
              builtin/ContextCapabilities.{h,cpp}[新]
              transport/StdioTransport.{h,cpp}[新] transport/LocalHttpTransport.{h,cpp}[新]
              ISignalSource.h[新，接口不变] IAgentBridge.h[同]
              acp/AcpSignalSource.{h,cpp}[P7.5 新：JSONL 显式信号源]
              acp/AcpAgentBridge.{h,cpp}[P7.5 新：会话桥接]
              acp/AcpSignalRules.{h,cpp}[P7.5 新：信号→工作态映射]
              acp/AcpEventMapper.{h,cpp}[P7.6 新：ACP session/update → CoreSignal]
              acp/AcpClient.{h,cpp}[P7.6 新：NDJSON over stdio 子进程客户端]
  minigame/   MiniGameRegistry.{h,cpp}[**不改**] MiniGameCompatAdapter.{h,cpp}[新]
  viewmodel/  EnvironmentService.{h,cpp}[新 P7.1 改：start/stop 下发 setObserving]
              WorkStateService.{h,cpp}[新；P7.5 改：新增显式信号覆盖窗口]
              AcpSignalService.{h,cpp}[P7.5 新：轮询信号源 → 覆盖性工作态；
              P7.6 增 submitSignal 供 ACP 客户端投递]
              PetContextProvider.{h,cpp}[新] PetController.{h,cpp}[改 仅新增工作态通道]
  view/       PetWindow.{h,cpp}[改 组合根装配与菜单门控；P7.1 注入 Win32 观察者；
              P7.4 setupProcessPlugins；P7.5 setupAcp；P7.6 startAcpClient / attachAcpSession]
  model/      SettingsData.h[改] SettingsRepo.cpp[改]
assets/lines/work.txt[新]  assets/assets.qrc[改]
tests/        test_plugin_registry.cpp[新] test_platform_skeleton.cpp[新]
              test_work_state.cpp[新] test_context_dispatch.cpp[新]
              test_win32_observer.cpp[P7.1 新]
              test_acp.cpp[P7.5 新] test_process_plugin.cpp[P7.4 新]
              mcp_test_server.cpp[P7.4 新：测试用外部 MCP server 子进程]
              test_acp_event_mapper.cpp[P7.6 新] test_acp_client.cpp[P7.6 新]
              acp_test_agent.cpp[P7.6 新：测试用假 ACP Agent 子进程]
              fixtures/acp-real-events.json[P7.6 新：真实 dsh 报文夹具]
```

---

## 9. 验证

- 新增 4 个测试目标，与既有 12 个**并存**（CTest 由 12 → 16），沿用
  `-o -,txt` / `SKIP_RETURN_CODE 77` / `QT_QPA_PLATFORM=offscreen` 既有约定（`docs/TESTING.md`）。
- 覆盖点：
  - `test_plugin_registry`：注册/查找/重复 id 冲突仲裁（Builtin > Dll > Process）/ 能力分发与错误码 /
    同步失败与异步受理的契约区分 / 生命周期容错 / 内置层装载器 /
    小游戏兼容适配（`MiniGameRegistry` 既有 API 语义 + `minigame.*` 能力可见）。
  - `test_platform_skeleton`：空实现恒返回 `unknown`（不采集）、组合观察者的类别/切换/停留/滚动窗口、
    采集失败不伪造数据、接口可注入替身。
  - `test_work_state`：应用类别归一化、各状态判据、Coding vs Vibe Coding 的区分、
    置信度阈值与最短驻留滞回、「无数据 → Unknown 立即生效」、
    状态机工作态通道的优先级与「专注态主动静默 / `work.*` 豁免 / 不打断一次性表现 / Unknown 零回归」。
  - `test_context_dispatch`：JSON-RPC 2.0 请求/通知/错误码、「能力别名」路由、能力不存在与不可用、
    门控（默认不监听 / 关闭后能力标记不可用）、
    双通道共用同一 dispatcher（stdio 用内存设备对；HTTP 走 127.0.0.1 回环 + 端口 0）。
- **实测结论（2026-10-02，P7.0）**：Debug / Release `ctest` 各 **16/16 通过**（既有 12 项零回归，
  未删除断言、未放宽条件、未注释用例）。
- **本阶段实际踩坑 5 条**（含 1 条由新单测发现的契约缺陷）见 `docs/pitfalls/`。

### 9.1 P7.1 追加（真实感知）

- 新增 `test_win32_observer`（CTest **16 → 17**）；`test_work_state` 补真实输入画像与
  「锁屏优先于无数据」用例。**Debug / Release `ctest` 各 17/17 通过**。
- `test_win32_observer` 的 11 个用例**全部用注入替身读数驱动**（`Win32ForegroundReader` /
  `Win32ActivityReader` / `Win32SystemReader`）：不安装任何系统钩子、不依赖真实前台窗口，
  因此可在无桌面 / CI 环境稳定运行，且能确定性覆盖「读数失败不伪造」「差分降级每次至多计 1」
  「注入替身绝不装钩子」等分支（真实 API 无法在单测中稳定制造这些条件）。
- 「默认装配**倾向**低层钩子」由 `hooksPreferred()` 断言（只查配置意图、不安装钩子），
  确保差分降级只发生在「钩子安装失败」这一真实降级路径上，而不是被配置错误悄悄变成常态
  （TRAP-P7-007）；**真实钩子安装/卸载与真实锁屏**仍属人工目视项
  （见 `ROADMAP-P7-Fin.md` P7.1 验证记录），不在自动化断言范围内——如实标注，不假装覆盖。
- P7.1 实际踩坑 2 条：TRAP-P7-006（判定顺序缺陷）、TRAP-P7-007（默认装配误关钩子，
  由新单测发现），均已按规范复现并留证。

### 9.2 P7.4 / P7.5 / P7.6 追加

- **P7.4**：新增 `test_process_plugin`（子进程 `mcp_test_server`），CTest **18 → 19**。
- **P7.5**：新增 `test_acp`，CTest **19 → 20**。
- **P7.6**：新增 `test_acp_event_mapper`（真实 dsh 夹具）与 `test_acp_client`
  （子进程 `acp_test_agent`），CTest **20 → 22**。
- **当前总量**：构建配置（顶层 `CMakeLists.txt` + `cmake/Tests.cmake`）注册 **31 个测试目标**（Windows；
  `test_win32_observer` 为 `WIN32` 条件目标），**Debug / Release 各 31/31 通过**
  （最新总量见 `docs/README.md`；分阶段增量见 `ROADMAP-P7-Fin.md` P7.2 / P7.3 验证记录）。
- **P7.3 追加**：`test_dll_plugin`（以真实 DLL `ext_hello` / `ext_badabi` 验证装载 / ABI 协商 /
  降级），CTest **23 → 24**；P7.2 追加 `test_context_pipe`，CTest **22 → 23**。

---

## 10. 变更记录

- **P9-A（宿主服务注册化）+ P9-B（外部进程型深化）**：详见 §7.1 与 `docs/ROADMAP-P9-Fin.md`。
  P9-A 把 5 个零界面依赖的服务（养成 / 胃袋 / 对话 / 彩蛋 / 回收站）经 `BuiltinPluginLoader`
  注册为 builtin 插件（落 `src/viewmodel/builtin/`），宿主 5 个 `setup*` 剥离装配、改从
  `BuiltinServiceHandles` 取用，并新增 5 个只读状态能力 `service.*`；
  P9-B 把 `plugins.json` 解析下沉为 `plugin::ProcessPluginConfig`，新增
  `ProcessPluginLoader::sessionStates()` 与设置页「外部插件」只读列表。
  新增 `test_service_plugins`、扩展 `test_process_plugin`，**CTest 35 → 36，
  Debug / Release 各 36/36**；踩坑 `P-081` … `P-083`。**2026-10-05 验收通过（A1~A6 逐条）。**
- **P9-C（UI 宿主契约与贡献点协议）**：2026-10-05 启动（裁决见 `ARCHITECTURE.md` §A.7，修订 §A.6 的
  「P9-C 暂缓」）。详见 §7.2 与 `docs/ROADMAP-P9-Fin.md` §2.3 / §3.2；2026-10-06 用户确认范围（按既有调研定义）
  与验收（C1~C6）并固化为门禁；**同日本轮交付 C1~C6 逐条通过，P9 阶段（A / B / C）全部完成
  （`docs/ROADMAP-P9-Fin.md`）**。
- **P7.3（动态插件 DLL）+ P7.2（命名管道 + MCP 桥接）**：DLL 层**接入组合根**——
  `PetWindow::setupDllPlugins()` 以 `<applicationDirPath>/plugins` 构造 `DllPluginLoader` 并
  `loadAll`（菜单构建之前）；新增示例插件 `ext_hello`（合法）与 `ext_badabi`（ABI 负例）及
  `test_dll_plugin`。P7.2 新增 `contextapi/transport/LocalPipeTransport`（`QLocalServer`，
  每连接复用 `StdioTransport`）、控制台桥接 `whalepet-mcp`（`src/app/mcp_bridge_main.cpp`，
  stdio ↔ 管道 `Content-Length` 分帧转发）与 `test_context_pipe`；`ContextApiService::start()`
  一并启停 HTTP 与命名管道（失败回滚）；打包脚本 / NSIS 三处对应表纳入 `whalepet-mcp.exe`、
  排除 `plugins/`。**CTest 22 → 24，Debug / Release 各 24/24**。
- **P7.6（ACP 实时状态接入）**：新增 `contextapi/acp/AcpEventMapper`（ACP `session/update`
  → `CoreSignal` 纯映射，工具细分依据 `title`）与 `contextapi/acp/AcpClient`（NDJSON over stdio +
  `QProcess` 子进程 + `initialize` / `session/new` / `session/list` / `session/resume` /
  `session/prompt` / `session/cancel` + 权限自动应答 + 崩溃隔离）；`AcpSignalService::submitSignal`
  让 ACP 事件与文件轮询**共用同一条下游映射/覆盖链路**；组合根新增
  `acp_dsh_path` / `acp_profile` / `acp_workspace` 设置项与 `PetWindow::startAcpClient` /
  `attachAcpSession`；新增 `test_acp_event_mapper`（真实 dsh 报文夹具）与 `test_acp_client`
  （`acp_test_agent` 假 Agent 端到端），**CTest 20 → 22，Debug / Release 各 22/22**。
- **P7.5（ACP / IDE Agent 集成）**：`ISignalSource` / `IAgentBridge` 两个接口**保持不变**，
  新增具体实现 `contextapi/acp/AcpSignalSource`（JSONL 增量信号源）、`AcpAgentBridge`
  （会话生命周期 + 事件落盘）与 `acp/AcpSignalRules`（信号→工作态纯映射）；
  新增 `viewmodel::AcpSignalService`（1s 级轮询 → 广播覆盖性工作态）与
  `WorkStateService` 的**显式信号覆盖窗口**（`applyExternalState` / `clearExternalState`）；
  组合根加「ACP / IDE 信号」菜单项（默认关），新增设置项 `acp_enabled` / `acp_signal_path`；
  新增 `test_acp`（CTest 19 → 20）。
- **P7.4（外部进程插件 / MCP Client）**：`ProcessPluginLoader` 由「配置与校验骨架」升级为
  完整实现，拆出 `ProcessServerSpec`（值类型）、`McpStdioClient`（stdio 分帧 JSON-RPC 客户端，
  同步握手 + 异步调用 + 超时）与 `McpPluginSession`（握手 / `tools/list` 发现 / `tools/call`
  异步转发 / 崩溃隔离）；能力 id = `ext.<pluginId>.<tool>`（`origin = Process`）；
  模拟第三方崩溃仅把该来源能力标记为不可用，pending 调用回投 `-32002`；
  配置来源 `<数据目录>/plugins.json`（不存在则零开销）；新增 `test_process_plugin`
  与测试用 `mcp_test_server`（CTest 18 → 19）。

- **P7.1（真实桌面感知）**：`platform` 层由「空实现」升级为真实 Win32 采集
  （`Win32DesktopObserver` / `Win32TextUtil`）；`IEnvironmentObserver` 及三个子采样器接口新增
  **可选生命周期 `setObserving(bool)`**（默认 no-op，零回归），由 `EnvironmentService::start/stop`
  下发，Win32 实现据此在采样期间安装低层输入钩子并在停止/析构时卸载；
  `core::WorkStateRules` 按真实数据回归调参（`systemPaused` 优先于 `isEmpty`；
  新增「采样窗口内高强度单应用输入 → Coding」判据）；新增 `test_win32_observer`（CTest 16 → 17），
  Debug / Release 各 17/17。（该句「仍为预留接口」描述的是 **P7.1 当时的**状态；
  P7.4–P7.6 已把两套接口补为实现，见本节的 P7.4 / P7.5 / P7.6 条目。）
- **P7.0（骨架）**：新增 `platform` / `plugin` / `contextapi` 三个静态库目标与
  `core::WorkState*`、`viewmodel::EnvironmentService` / `WorkStateService` / `PetContextProvider`；
  **`MiniGameRegistry` 零改动**（由 `MiniGameCompatAdapter` 在外层适配并注册进通用总线）；
  `PetStateMachine` 新增 `EventType::WorkStateChanged` 工作态通道（默认 `Unknown`，零回归）；
  依赖口径由「零新依赖」改为「零第三方依赖，允许 Qt 官方模块」（新增 `Qt6::Network`）；
  新增 4 个测试目标（CTest 12 → 16），Debug / Release 各 16/16。
- **EX4（小游戏陪玩：插件侧自描述 + 陪玩侧通用聚合）**：见 §7.3 与 `ARCHITECTURE.md` 附录 B.3 / B.6。
  新增可选接口 `IMiniGameCompanionSource`、中立契约 `core::GameSnapshot` 与中立判定
  `core::MiniGameCompanion`、陪玩侧通用聚合 `viewmodel::MiniGameCompanionSource`；
  `IGameCompanionSource` 增加默认 `readSnapshot()`（既有实现零改动）；三款小游戏各实现一次
  `companionSnapshot()`。**`IMiniGamePlugin` 一族与 `MiniGameRegistry` 仍未改动**（§7 红线保持）。
  新增 `test_minigame_companion`，**Debug / Release CTest 各 33/33**；踩坑 `P-094` … `P-096`。
