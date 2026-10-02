# 本地 Context API 与 MCP 集成（CONTEXT-API）

> 本文档定义 WhalePet 对外暴露的**本地上下文接口**：数据模型、方法表、JSON-RPC 约定、
> 双通道（MCP stdio + 本地 HTTP）、访问控制与隐私边界，以及 ACP / IDE Agent 的预留接口。
>
> 架构依据：`PLUGIN-ARCHITECTURE.md`（能力总线与三层插件）；
> 实施顺序与阶段验收：`ROADMAP-P7.md`。

---

## 1. 目标与范围

| 目标 | 说明 |
|---|---|
| 为 AI Agent 提供本地上下文 | Agent 可查询「用户此刻的桌面环境 / 工作状态 / 桌宠状态 / 会话统计」 |
| 工具调用 | 能力（capability）统一以工具形式暴露，含三层插件注册的全部能力 |
| MCP 协议扩展 | 既作为 **MCP Server** 暴露上下文与工具，也作为 **MCP Client** 接入外部进程插件 |
| ACP / IDE Agent 预留 | 只定义接口（显式信号源 + 会话桥接），**本期不实现协议** |
| 隐私优先 | 仅本机可访问；不读取输入内容；默认关闭，需用户显式开启 |

**本期（P7.0）交付的是接口与可运行骨架**：传输与分发核心已实现并可单测，
但默认关闭（`context_api_enabled = false`），正常运行不监听任何端口。

---

## 2. 数据模型 `ContextSnapshot`

`src/contextapi/ContextSnapshot.h`。所有字段可空（能力缺失时降级为 `null` / `unknown`），
**不抛错、不静默伪造数据**。

| 分组 | 字段 | 来源 |
|---|---|---|
| 版本 | `apiVersion`（`"1.0"`）、`appVersion`、`generatedAtMs` | 常量 / `QCoreApplication` |
| 环境 | `env.appId`、`env.windowTitle`、`env.category`、`env.idleMs`、`env.inputEvents`、`env.appSwitches`、`env.dwellMs` | `platform` 采样；P7.1 起为真实 Win32 读数（未启用感知时为空 / 0） |
| 工作状态 | `work.state`、`work.confidence`、`work.sinceMs` | `WorkStateService` |
| 桌宠 | `pet.level`、`pet.exp`、`pet.mood`、`pet.affinity`、`pet.satiety`、`pet.bondLevel`、`pet.companionMs` | `GrowthService` |
| 会话 | `session.uptimeMs`、`session.samples`、`session.interactions`、`session.workStateChanges` | 服务内部计数 |

`category` 取值（`core::AppCategory`）：`unknown` / `editor` / `terminal` / `browser` /
`meeting` / `game` / `office` / `other`。

`work.state` 取值（`core::WorkState`）：`unknown` / `idle` / `reading` / `coding` /
`vibe-coding` / `debugging` / `browsing` / `meeting` / `game` / `afk`。

---

## 3. 方法表

`JsonRpcDispatcher` 是**唯一分发核心**，方法表固定在内置方法里，能力调用统一走
`capability.invoke` 转发（因此新增能力**无需**改分发核心）。

| method | 参数 | 结果 | 说明 |
|---|---|---|---|
| `ping` | — | `{ "pong": true, "nowMs": <int> }` | 存活探测（通道与心跳用） |
| `context.snapshot` | — | `ContextSnapshot` | 全量上下文快照 |
| `context.environment` | — | `env` 分组 | 桌面环境（前台应用 / 空闲 / 输入活跃度） |
| `context.workState` | — | `work` 分组 | 当前工作状态 + 置信度 + 进入时间 |
| `pet.status` | — | `pet` 分组 | 养成状态 |
| `session.stats` | — | `session` 分组 | 会话统计 |
| `capabilities.list` | — | `{ "capabilities": [CapabilityDescriptor…] }` | 三层插件注册的全部能力 |
| `capability.invoke` | `{ "id": <string>, "params": <object> }` | 能力自定义 | 统一调用入口 |

> **「能力别名」路由（实现要点）**：上表中 `context.*` / `pet.status` / `session.stats`
> **本身就是能力 id**，由内置插件 `ContextCapabilities` 注册（见 §7）。分发核心只内置
> `ping` / `capabilities.list` / `capability.invoke` 三个方法，其余规则是：
> **方法名与某个已注册能力 id 相同时，等价于 `capability.invoke` + `{ id: 方法名, params }`**。
> 因此新增能力**无需修改分发核心**，MCP `tools/list` 也直接由能力清单生成。

### 3.1 标准方法（JSON-RPC 2.0 语义）

| 形状 | 行为 |
|---|---|
| 有 `id` + `method` | 返回 `result` 或 `error` |
| 只有 `method`（无 `id`） | **通知**（notification）：执行但不返回 |
| `id` 非法 / 缺 `method` | `-32600 Invalid Request` |
| `method` 未注册 | `-32601 Method Not Found` |
| 参数不合法 | `-32602 Invalid Parameters` |
| 能力内部失败 | 由**能力自报**的错误码（约定用 `-32001 Capacity Failed`），`error.data` 可带补充信息 |
| 能力不可用（通道关闭 / 外部进程已退出） | `-32002 Capability Unavailable` |
| 未授权（token 缺失或不匹配） | `-32003 Unauthorized` |
| 能力违反调用契约（异步未取走回调） | `-32603 Internal Error` |

错误码常量集中在 `src/plugin/Capability.h`（`kRpcErrorXxx`），**禁止在别处再写字面量**。
能力自身返回的 `error` 会被**原样透传**（分发核心不改写 code），便于插件定义细分错误。

### 3.2 MCP 映射（`StdioTransport` 通道）

| MCP 方法 | 本实现 |
|---|---|
| `initialize` | 返回 `protocolVersion` / `serverInfo` / `capabilities` |
| `tools/list` | 由 `capabilities.list` 映射（`inputSchema` 取自 `CapabilityDescriptor::paramsSchema`） |
| `tools/call` | `{ "name": <capabilityId>, "arguments": <object> }` → 转发 `capability.invoke` |
| `notifications/initialized` | 通知，忽略 |

> **stdio 通道必须由独立的控制台子进程承载。** 主程序是 `WIN32` GUI 子系统
> （`qt_add_executable(WhalePet WIN32 …)`），没有可用的 stdin/stdout 流，无法直接做
> MCP stdio Server。因此架构为：
>
> ```
> AI Agent ──stdio JSON-RPC──► whalepet-mcp.exe（控制台桥接，P7.2 交付）
>                                   │  本地通道（命名管道 / 127.0.0.1）
>                                   ▼
>                             WhalePet.exe（GUI，唯一分发核心）
> ```
>
> `StdioTransport` 本身与「是不是控制台」无关：它以两个 `QIODevice*`（读 / 写）构造，
> 因此既可用于未来的桥接 exe，也可在单测中用内存设备驱动（`test_context_dispatch`）。

---

## 4. 双通道

| 通道 | 类 | 面向 | 传输 | 本期状态 |
|---|---|---|---|---|
| MCP stdio | `transport/StdioTransport` | AI Agent（经桥接 exe） | `Content-Length` 分帧 JSON-RPC（MCP 标准） | 已实现 + 单测；**绑定任意读写 `QIODevice`**，桥接 exe 待 P7.2 |
| 本地 HTTP | `transport/LocalHttpTransport` | 工具 / 调试 UI / 其它本地程序 | `POST /rpc`，`application/json`，**仅绑定 `127.0.0.1`** | 已实现 + 单测；**默认不启动** |

两条通道都只做「读帧 → `JsonRpcDispatcher` → 写帧」，**不包含任何业务分支**。

实现取舍（刻意的极简，不为调试通道引入额外 Qt 模块）：

- 本地 HTTP 用 `QTcpServer` **手写最小 HTTP/1.1**（不支持 keep-alive / chunked / 压缩，`Connection: close`），
  每个请求走**同步分发**（`handleSync`）——异步能力不适用于本通道，会明确报错而不是挂起。
- MCP stdio 通道**与「是不是控制台」解耦**：以两个 `QIODevice`（读 / 写）构造，
  故未来桥接 exe 传 stdin/stdout，单测传内存设备对即可（见 §8）。
- 命名管道（`QLocalServer`）作为本地通道的后续形态复用同一 dispatcher，见 `ROADMAP-P7.md` P7.2。

---

## 5. 访问控制与隐私边界

| 项 | 约定 | 设置键（`json_ext`） |
|---|---|---|
| 总开关 | 默认**关**；关闭时**不启动任何通道**、不注册 Context 能力 | `context_api_enabled`（默认 `false`） |
| 监听地址 | 仅 `127.0.0.1`（回环），**不监听** `0.0.0.0`；端口 `0` 表示由系统分配 | `context_api_port`（默认 `0`） |
| 令牌 | 非空时要求 `X-WhalePet-Token` 头（HTTP）或 `initialize.params.token`（MCP）；空 = 不校验 | `context_api_token`（默认空串） |
| 感知开关 | 默认**关**；关闭时不采样（等价于本次启动无感知数据） | `work_aware_enabled`（默认 `false`） |
| 输入内容 | **只统计键鼠事件数与空闲时长**，不记录按键、不读取文本、不读编辑区 | — |
| 输入采集实现 | 计数只在低层钩子回调里做一次原子自增（不携带键码 / 坐标）；鼠标**只计按键与滚轮，不计移动**；钩子**仅在采样期间安装**，`stop()` 或析构即卸载 | — |
| 窗口信息 | 仅前台窗口标题与进程名；不做全窗口枚举、不做截图 | — |

> P7.1 起 `env.*` 由 `Win32DesktopObserver` 真实采集：
> 前台窗口标题（`GetWindowTextW`）、进程名（`QueryFullProcessImageNameW`，对外只暴露文件名）、
> 空闲时长与增量事件数（低层钩子优先，安装失败降级 `GetLastInputInfo` 差分）、
> 会话锁定 / 屏保（`OpenInputDesktop` / `SPI_GETSCREENSAVERRUNNING`）。
> 任一读数失败一律返回「无数据」并置 `envAvailable = false`，**不伪造**。

以上键一律写入 `settings.json_ext`（**不新建列**，缺省即默认值，未知键保留），
由 `SettingsRepo` 统一读写（`docs/SETTINGS.md` §3）。

---

## 6. ACP / IDE Agent 预留接口

本期**只定义接口，不实现协议**（避免过度设计）：

```cpp
// src/contextapi/ISignalSource.h
// 外部显式信号源：IDE 扩展 / Agent 会话 / 文件保存与差异事件。
// 与「推断」的关系：显式信号优先于 platform 层推断（可覆盖 WorkState）。
class ISignalSource {
public:
    virtual ~ISignalSource() = default;
    virtual QString id() const = 0;              // "vscode" / "acp" / "git" …
    virtual bool available() const = 0;
    // 返回 false 表示该源当前无信号（不产生假状态）
    virtual bool poll(qint64 nowMs, CoreSignal &out) = 0;
};

// src/contextapi/IAgentBridge.h
// Agent 会话桥接：会话生命周期 + 事件推送（ACP / IDE Agent 集成入口）。
class IAgentBridge {
public:
    virtual ~IAgentBridge() = default;
    virtual bool start(const QString &agentId) = 0;   // 建立会话
    virtual void stop(const QString &agentId) = 0;    // 结束会话
    virtual bool push(const QString &agentId, const QJsonObject &event) = 0; // 事件推送
};
```

- 两个接口均为**可选注入**：未注入时相关能力不注册，`ContextSnapshot` 对应分组保持空。
- 预留原则：**只固定「谁在什么时候告诉我什么」**，不固定传输与协议细节；
  ACP 落地时新增 `AcpSignalSource` / `AcpAgentBridge` 实现即可，不改总线与分发核心。

---

## 7. 实现落位

| 交付项 | 代码位置 |
|---|---|
| Context 数据模型 | `src/contextapi/ContextSnapshot.{h,cpp}` |
| 分发核心（method 表 / 错误码 / 能力转发） | `src/contextapi/JsonRpcDispatcher.{h,cpp}` |
| 服务装配（dispatcher + 注册表 + 通道 + 门控） | `src/contextapi/ContextApiService.{h,cpp}` |
| 上下文能力插件（`context.*` / `pet.status` / `session.stats`） | `src/contextapi/builtin/ContextCapabilities.{h,cpp}` |
| MCP stdio 通道 | `src/contextapi/transport/StdioTransport.{h,cpp}` |
| 本地 HTTP 通道（回环） | `src/contextapi/transport/LocalHttpTransport.{h,cpp}` |
| 感知实现（P7.1：真实 Win32 采集） | `src/platform/Win32DesktopObserver.{h,cpp}`、`src/platform/Win32TextUtil.{h,cpp}` |
| 感知生命周期（钩子安装/卸载） | `src/platform/DesktopObserver.h`（`setObserving`）、`src/viewmodel/EnvironmentService.cpp` |
| 数据提供者接口 | `src/contextapi/IContextProvider.h` |
| view 侧数据提供者实现 | `src/viewmodel/PetContextProvider.{h,cpp}` |
| ACP / IDE 预留接口 | `src/contextapi/ISignalSource.h`、`src/contextapi/IAgentBridge.h` |
| 组合根装配与菜单门控 | `src/view/PetWindow.{h,cpp}` |
| 设置项落位 | `src/model/SettingsData.h`、`src/model/SettingsRepo.cpp`（`json_ext`） |
| 单测 | `tests/test_context_dispatch.cpp` |

---

## 8. 验证

`test_context_dispatch`（10 个用例）实际覆盖：

- **JSON-RPC 2.0**：`result` / `error` / 通知（无 `id`）不产生响应 / 缺 `method` / 版本不符 /
  `params` 非对象 / 未知方法（`-32601`）。
- **能力别名路由**：直接以 `context.workState` / `context.environment` / `pet.status` /
  `session.stats` / `context.snapshot` 作为方法名调用，结果与 `capability.invoke` 一致。
- **错误码**：能力不存在 → `-32601`；能力被标记不可用 → `-32002`。
- **门控**：默认不监听、不注册能力；`start()` 后监听回环且能力可用；`stop()` 后能力标记为不可用。
  该用例刻意使用**未被预注册**的干净注册表，以真正验证「服务自己按需注册」的行为。
- **token**：`X-WhalePet-Token` 缺失 → HTTP 401 + `-32003`；正确 → 200。
- **双通道共用**：`LocalHttpTransport`（127.0.0.1 + 端口 0 回环）与 `StdioTransport`
  （内存 `QIODevice` 对）对同一 dispatcher 的响应**逐字段一致**；
  MCP 侧额外验证 `initialize`（含 token 鉴权与 serverInfo）/ `tools/list`（映射能力清单）/
  `tools/call`（映射 `capability.invoke`）/ 未握手前一律拒绝。

**实测结论（2026-10-02，P7.1 收尾）**：Debug / Release `ctest` 各 **17/17 通过**
（新增 `test_win32_observer`；`test_work_state` 补真实输入画像与锁屏优先用例）。

`test_win32_observer` 额外覆盖（全部用**注入替身读数**驱动，不安装任何系统钩子，
无桌面 / CI 环境也可稳定运行）：

- UTF-16 → UTF-8 中文标题不乱码；路径取进程名（识别 `\` 与 `/`，结尾分隔符返回空）；
- 前台采样器：填充 `appId`（只给文件名）/ `windowTitle`；读不到前台窗口时**不伪造**；
- 输入采样器：差分降级每次至多计 1、空闲时长计算与时钟回绕保护、注入替身**绝不安装钩子**；
- 系统状态采样器：锁屏 / 屏保 → `systemPaused`；
- 端到端：锁屏导致前台窗口读不到时仍判 `afk`（`docs/traps-P7.md` TRAP-P7-006 的回归守卫）。

**人工目视项（待用户复验）**：

1. 右键菜单勾选「本地 Context API」后，本地 `POST http://127.0.0.1:<port>/rpc` 能取到快照；
   取消勾选后端口不再监听。（端口由系统分配，日志中以
   `[PetWindow] 本地 Context API 已启动，端口 = N` 打印。）
2. 同时勾选「工作状态感知」后，`context.snapshot` 的 `env.appId` / `env.windowTitle` /
   `env.idleMs` / `env.inputEvents` 应随前台切换在 1 个采样周期内变化；
   取消勾选后 `envAvailable` 应为 `false`（不再有真实数据）。
