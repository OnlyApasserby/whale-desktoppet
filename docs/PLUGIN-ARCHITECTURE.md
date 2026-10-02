# 插件化架构与能力总线（PLUGIN-ARCHITECTURE）

> 本文档定义 WhalePet 从「小游戏专用插件机制」泛化为**通用分层插件总线**的模块划分、插件接口、
> 统一 capability 协议、三层装载方式与数据流，是 `CONTEXT-API.md`（对外接口）与 `ROADMAP-P7.md`
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
| ACP / IDE Agent 集成 | `contextapi` 的预留接口（仅接口） |

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
│ DB/Repo │  │ 感知接口   │  │ 能力协议/注册表│  │ JsonRpc/双通道/预留   │                          │
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
| `whalepet_contextapi` | STATIC | `Qt6::Core`、`Qt6::Network`、`whalepet_core`、`whalepet_plugin` | JSON-RPC / 双通道 / 预留接口，净增 |
| `whalepet_view` | STATIC | 上述全部 + `Qt6::Gui`、`Qt6::Widgets`、`user32` | 既有 + 三者装配 |
| `WhalePet` | WIN32 exe | `whalepet_view` | 既有 |

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
- 部署位置：`<安装目录>/plugins/`（**运行期按目录扫描**，因此必须同步
  `packaging/make-package.ps1`、`packaging/whalepet.nsi` 与 `docs/packages.md` §2/§5 清单）。
  本期无 DLL 产物，先在 `packages.md` 落约定。

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

**同步 / 异步语义（易错点，见 `traps-P7.md` TRAP-P7-005）**：

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
  （见 `traps-P7.md` TRAP-P7-006），故 `systemPaused` 视为「有数据」并直接判 `Afk`。
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
  与其余 15 个测试目标必须继续通过；不删除任何断言、不放宽任何条件（`docs/TESTING.md`）。

---

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
              process/ProcessPluginLoader.{h,cpp}[新]
  contextapi/ IContextProvider.h[新] ContextSnapshot.{h,cpp}[新]
              JsonRpcDispatcher.{h,cpp}[新] ContextApiService.{h,cpp}[新]
              builtin/ContextCapabilities.{h,cpp}[新]
              transport/StdioTransport.{h,cpp}[新] transport/LocalHttpTransport.{h,cpp}[新]
              ISignalSource.h[新 P7.1：仍是预留接口，不实现协议] IAgentBridge.h[同]
  minigame/   MiniGameRegistry.{h,cpp}[**不改**] MiniGameCompatAdapter.{h,cpp}[新]
  viewmodel/  EnvironmentService.{h,cpp}[新 P7.1 改：start/stop 下发 setObserving]
              WorkStateService.{h,cpp}[新]
              PetContextProvider.{h,cpp}[新] PetController.{h,cpp}[改 仅新增工作态通道]
  view/       PetWindow.{h,cpp}[改 组合根装配与菜单门控；P7.1 注入 Win32 观察者]
  model/      SettingsData.h[改] SettingsRepo.cpp[改]
assets/lines/work.txt[新]  assets/assets.qrc[改]
tests/        test_plugin_registry.cpp[新] test_platform_skeleton.cpp[新]
              test_work_state.cpp[新] test_context_dispatch.cpp[新]
              test_win32_observer.cpp[P7.1 新]
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
- **本阶段实际踩坑 5 条**（含 1 条由新单测发现的契约缺陷）见 `traps-P7.md`。

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
  （见 `ROADMAP-P7.md` P7.1 验证记录），不在自动化断言范围内——如实标注，不假装覆盖。
- P7.1 实际踩坑 2 条：TRAP-P7-006（判定顺序缺陷）、TRAP-P7-007（默认装配误关钩子，
  由新单测发现），均已按规范复现并留证。

---

## 10. 变更记录

- **P7.1（真实桌面感知）**：`platform` 层由「空实现」升级为真实 Win32 采集
  （`Win32DesktopObserver` / `Win32TextUtil`）；`IEnvironmentObserver` 及三个子采样器接口新增
  **可选生命周期 `setObserving(bool)`**（默认 no-op，零回归），由 `EnvironmentService::start/stop`
  下发，Win32 实现据此在采样期间安装低层输入钩子并在停止/析构时卸载；
  `core::WorkStateRules` 按真实数据回归调参（`systemPaused` 优先于 `isEmpty`；
  新增「采样窗口内高强度单应用输入 → Coding」判据）；新增 `test_win32_observer`（CTest 16 → 17），
  Debug / Release 各 17/17。`ISignalSource` / `IAgentBridge` **仍为预留接口**（MCP / ACP 不接入）。
- **P7.0（骨架）**：新增 `platform` / `plugin` / `contextapi` 三个静态库目标与
  `core::WorkState*`、`viewmodel::EnvironmentService` / `WorkStateService` / `PetContextProvider`；
  **`MiniGameRegistry` 零改动**（由 `MiniGameCompatAdapter` 在外层适配并注册进通用总线）；
  `PetStateMachine` 新增 `EventType::WorkStateChanged` 工作态通道（默认 `Unknown`，零回归）；
  依赖口径由「零新依赖」改为「零第三方依赖，允许 Qt 官方模块」（新增 `Qt6::Network`）；
  新增 4 个测试目标（CTest 12 → 16），Debug / Release 各 16/16。
