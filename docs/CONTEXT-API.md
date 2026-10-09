# 本地 Context API 与 MCP 集成（CONTEXT-API）

> 本文档定义 WhalePet 对外暴露的**本地上下文接口**：数据模型、方法表、JSON-RPC 约定、
> 三通道（MCP stdio + 本地 HTTP + 本地命名管道）、访问控制与隐私边界，以及 ACP / IDE Agent
> 集成（`ISignalSource` / `IAgentBridge` + P7.6 的 ACP 客户端）。
> **MCP Server 侧的命名管道通道与桥接 exe `whalepet-mcp.exe` 已于 P7.2 交付**，见 §3.2 / §4。
>
> 架构依据：`PLUGIN-ARCHITECTURE.md`（能力总线与三层插件）；
> 实施顺序与阶段验收：`ROADMAP-P7-Fin.md`。

---

## 1. 目标与范围

| 目标 | 说明 |
|---|---|
| 为 AI Agent 提供本地上下文 | Agent 可查询「用户此刻的桌面环境 / 工作状态 / 桌宠状态 / 会话统计」 |
| 工具调用 | 能力（capability）统一以工具形式暴露，含三层插件注册的全部能力 |
| MCP 协议扩展 | 既作为 **MCP Server** 暴露上下文与工具（`StdioTransport`），也作为 **MCP Client** 接入外部进程插件（P7.4 已实现，见 §7） |
| ACP / IDE Agent | 显式信号源（IDE / Agent 上报）与会话桥接，已落地为 `src/contextapi/acp/**`（见 §6） |
| 隐私优先 | 仅本机可访问；不读取输入内容；默认关闭，需用户显式开启 |

**交付状态**：传输与分发核心、本地 HTTP 回环通道、MCP stdio 通道（`StdioTransport`，绑定任意
`QIODevice`）、**本地命名管道通道（`LocalPipeTransport`，P7.2）**、**控制台桥接 exe
`whalepet-mcp.exe`（P7.2）**、Context 能力、以及 ACP 显式信号 / ACP 客户端（P7.5 / P7.6）
**均已实现并可单测**；默认关闭（`context_api_enabled = false`），正常运行不监听任何端口 / 管道。
运行期 **MCP Server 侧已有真实进程中转**（MCP 客户端 → 桥接 exe → 命名管道 → 宿主），
详见 §3.2 / §4 与 `docs/P7-REMAINING-INTERFACES-AUDIT.md`。

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
| `service.growth` | — | 养成服务状态 | **P9-A**（builtin 服务插件注册）：等级 / 经验 / 心情 / 好感 / 饱食 / 羁绊 / 陪伴时长 |
| `service.stomach` | — | 胃袋状态 | **P9-A**：stomach 目录路径与轮询运行状态 |
| `service.dialogue` | — | 预设对话状态 | **P9-A**：语料可用性 / 敏感题配额 / 天气题可用性 / 运行状态 |
| `service.easterEgg` | — | 代码彩蛋状态 | **P9-A**：是否启用 / 目标工作区 / 最近藏话的文件 |
| `service.recycleBin` | — | 回收站状态 | **P9-A**：最近查询是否可用 / 条目数 / 占用字节 / 是否运行 |
| `capabilities.list` | — | `{ "capabilities": [CapabilityDescriptor…] }` | 三层插件注册的全部能力 |
| `capability.invoke` | `{ "id": <string>, "params": <object> }` | 能力自定义 | 统一调用入口 |

> **「能力别名」路由（实现要点）**：上表中 `context.*` / `pet.status` / `session.stats`
> **本身就是能力 id**，由内置插件 `ContextCapabilities` 注册（见 §7）。分发核心只内置
> `ping` / `capabilities.list` / `capability.invoke` 三个方法，其余规则是：
> **方法名与某个已注册能力 id 相同时，等价于 `capability.invoke` + `{ id: 方法名, params }`**。
> 因此新增能力**无需修改分发核心**，MCP `tools/list` 也直接由能力清单生成。
>
> **P9-A 补充**：`service.*` 五个能力由**宿主服务插件**注册（`src/viewmodel/builtin/`，
> 见 `PLUGIN-ARCHITECTURE.md` §7.1），同样是 builtin 层能力；服务未就绪（未启动 / 已停止）
> 时返回 `-32002`（不伪造数据）。它们只在**装配了宿主服务插件**的进程中可见——
> 单测 harness（`test_context_dispatch` / `test_context_pipe` / `test_context_http_security`）
> 使用独立注册表，故其 `capabilities.list` 断言仍只含 `contextCapabilityIds()`。

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
> 因此既被控制台桥接 exe（经命名管道，P7.2）复用，也可在单测中用内存设备驱动
> （`test_context_dispatch`）。
>
> **P7.2 起**：宿主侧由 `LocalPipeTransport`（`QLocalServer`）监听命名管道
> `\\.\pipe\whalepet-context-v1`，**每条连接直接复用 `StdioTransport`**——分帧（`Content-Length`）、
> MCP 方法映射与 token 门控与 stdio 通道**完全同源**，无需第二套协议实现。

---

## 4. 三通道

| 通道 | 类 | 面向 | 传输 | 本期状态 |
|---|---|---|---|---|
| MCP stdio | `transport/StdioTransport` | AI Agent（经桥接 exe） | `Content-Length` 分帧 JSON-RPC（MCP 标准） | 已实现 + 单测；**绑定任意读写 `QIODevice`**；由 `whalepet-mcp.exe`（P7.2）经命名管道承载 |
| 本地 HTTP | `transport/LocalHttpTransport` | 工具 / 调试 UI / 其它本地程序 | `POST /rpc`，`application/json`，**仅绑定 `127.0.0.1`** | 已实现 + 单测；**默认不启动** |
| 本地命名管道 | `transport/LocalPipeTransport` | `whalepet-mcp.exe` 桥接进程 | `QLocalServer` 命名管道，**每连接复用 `StdioTransport`**（同一分帧 / MCP 映射 / token 门控） | ✅ P7.2 已实现 + 单测；**与 HTTP 同受总开关控制** |

三条通道都只做「读帧 → `JsonRpcDispatcher` → 写帧」，**不包含任何业务分支**，
共用**同一** `JsonRpcDispatcher` 与**同一**能力表。

实现取舍（刻意的极简，不为调试通道引入额外 Qt 模块）：

- 本地 HTTP 用 `QTcpServer` **手写最小 HTTP/1.1**（不支持 keep-alive / chunked / 压缩，`Connection: close`），
  每个请求走**同步分发**（`handleSync`）——异步能力不适用于本通道，会明确报错而不是挂起。
- MCP stdio 通道**与「是不是控制台」解耦**：以两个 `QIODevice`（读 / 写）构造，
  故桥接 exe 侧传 stdin/stdout、命名管道侧传 socket 对、单测传内存设备对即可（见 §8）。
- 命名管道**复用 `StdioTransport`** 而非另写一套协议：`LocalPipeTransport::onNewConnection` 对每条
  连接 `new StdioTransport` 并 `setToken` + `bind(socket, socket)`，天然满足「两通道共用同一
  dispatcher 与能力表」（见 `ROADMAP-P7-Fin.md` P7.2）。
- **总开关原子性**：`ContextApiService::start()` 一并启动 HTTP 与命名管道，任一同失败即
  回滚已启动的一方并返回 `false`（要么都听，要么都不听）；`stop()` 一并停。
  **`start()` 幂等**：重复调用（例如从持久化设置恢复时，菜单 `setChecked` 先触发一次 `toggled`、
  随后又显式应用一次）会先收拢既有通道再统一启动，**不会**因命名管道「已在监听」而回滚 HTTP
  （见 `docs/pitfalls/ex1/P-089`；回归守卫 `test_context_pipe::serviceStartIsIdempotent`）。

### 4.1 命名管道命名约定

| 项 | 约定 |
|---|---|
| 默认名 | `whalepet-context-v1`（Windows 下即 `\\.\pipe\whalepet-context-v1`） |
| 唯一约定源 | `src/contextapi/transport/LocalPipeTransport.h` 的 `kDefaultContextPipeName` |
| 覆盖方式 | 桥接侧 `whalepet-mcp --pipe <name>`；宿主侧 `ContextApiService::setPipeName()` |
| 修改流程 | 改常量必须同步 `docs/packages.md` §2.1、本表与桥接用法文本，否则两侧不一致（桥接退出码 2） |

---

## 5. 访问控制与隐私边界

| 项 | 约定 | 设置键（`json_ext`） |
|---|---|---|
| 总开关 | 默认**关**；关闭时**不启动任何通道**、不注册 Context 能力 | `context_api_enabled`（默认 `false`） |
| 监听地址 | 仅 `127.0.0.1`（回环），**不监听** `0.0.0.0`；端口 `0` 表示由系统分配 | `context_api_port`（默认 `0`） |
| 令牌 | **HTTP 通道强制非空**（为空则不监听该端口）；MCP 通道非空时要求 `initialize.params.token` | `context_api_token`（默认空 ⇒ **仅启用命名管道**；组合根在用户启用时自动生成并落盘） |
| HTTP 来源校验 | `Content-Type` 必须 `application/json`；`Origin` 必须同源同端口；响应**从不**带 CORS 头 | — |
| HTTP 资源上限 | 请求头 16 KiB / 正文 1 MiB / 并发连接 32 / 单请求等待 10s，超限即关闭连接 | — |
| 感知开关 | 默认**关**；关闭时不采样（等价于本次启动无感知数据） | `work_aware_enabled`（默认 `false`） |
| 输入内容 | **只统计键鼠事件数与空闲时长**，不记录按键、不读取文本、不读编辑区 | — |
| 输入采集实现 | 计数只在低层钩子回调里做一次原子自增（不携带键码 / 坐标）；鼠标**只计按键与滚轮，不计移动**；钩子**仅在采样期间安装**，`stop()` 或析构即卸载 | — |
| 窗口信息 | 仅前台窗口标题与进程名；不做全窗口枚举、不做截图 | — |
| ACP 显式信号开关 | 默认**关**；关闭时**不轮询**信号文件（无任何读取、零开销） | `acp_enabled`（默认 `false`） |
| ACP 信号文件 | 只**读**本地文件（默认数据目录下 `acp-signals.jsonl`）；**不上传、不外发**，内容由用户 / IDE 自行写入 | `acp_signal_path`（默认空） |
| 外部进程插件 | 仅在存在 `<数据目录>/plugins.json` 时拉起用户指定的本地程序；能力以前缀 `ext.` 暴露，子进程崩溃被隔离 | —（配置文件即约定） |

> P7.1 起 `env.*` 由 `Win32DesktopObserver` 真实采集：
> 前台窗口标题（`GetWindowTextW`）、进程名（`QueryFullProcessImageNameW`，对外只暴露文件名）、
> 空闲时长与增量事件数（低层钩子优先，安装失败降级 `GetLastInputInfo` 差分）、
> 会话锁定 / 屏保（`OpenInputDesktop` / `SPI_GETSCREENSAVERRUNNING`）。
> 任一读数失败一律返回「无数据」并置 `envAvailable = false`，**不伪造**。

以上键一律写入 `settings.json_ext`（**不新建列**，缺省即默认值，未知键保留），
由 `SettingsRepo` 统一读写（`docs/SETTINGS.md` §3）。

### 5.1 HTTP 通道的授权模型（修复 SECURITY-REVIEW.md #1）

**背景**：浏览器里任意网页都能对本机 `http://127.0.0.1:<port>` 发请求。
「只监听回环」**不是**授权机制——旧实现「token 为空即不校验」等于对全世界的网页
开放一次 JSON-RPC 工具调用（含有副作用的工具）。修复采用**三层纵深防御**，
任一层单独失效都仍有兜底：

| 层 | 规则 | 拦截的威胁 |
|---|---|---|
| 1. 强制令牌 | `LocalHttpTransport::start()` 在 token 为空/全空白时**直接失败**（fail closed），根本不监听端口 | 无认证的工具调用 |
| 2. `Content-Type` | 必须是 `application/json`（允许 `; charset=utf-8` 等参数与大小写变体） | CORS「简单请求」：`text/plain` / 表单编码可**无预检**直发本机端口 |
| 3. `Origin` 同源 | 带 `Origin` 时必须为 `http://127.0.0.1:<本端口>` / `localhost:<本端口>` / `[::1]:<本端口>`；`null`、`file://`、userinfo、非回环、异端口一律 **403** | 跨站调用与 DNS rebinding 伪装 |

补充：令牌用**定长比较**（不因首个不同字节短路），避免计时侧信道；
响应固定带 `X-Content-Type-Options: nosniff` / `Cache-Control: no-store`，
且**从不**回 `Access-Control-Allow-*`——因此 `application/json` 触发的预检必然失败，
跨站页面即便发出请求也**读不到响应**。

**无令牌时的行为**：`ContextApiService::start()` 仍会启动**命名管道**通道
（浏览器不可达，且受 Windows 命名管道 ACL 保护），此时 `httpPort()` 返回 `0`、
`started(0)`。为避免功能形同虚设，组合根（`PetWindow::setContextApiEnabled`）在
「用户启用总开关但未配置令牌」时会用 `ContextApiService::generateToken()`
（256 bit CSPRNG → base64url）生成一个并**落盘**到 `context_api_token`。

**回归守卫**：`tests/test_context_http_security.cpp`（跨站 / 认证 / 来源矩阵 /
Content-Type 矩阵 / 无 CORS 头，逐例断言副作用工具的调用计数保持 0）
与 `tests/test_context_dispatch.cpp::httpChannelRefusesToStartWithoutToken`。

### 5.2 HTTP 通道的资源上限

单连接缓冲、并发连接数、单请求等待时长均有上限（常量见
`LocalHttpTransport.h`，均可在单测中直接核验）：

| 场景 | 响应 | 说明 |
|---|---|---|
| 请求头未收全且超过 16 KiB | `431` + 关闭 | 慢速/恶意客户端不得无限累积缓冲 |
| 头部结束但整体超过 16 KiB | `431` + 关闭 | 同上 |
| 缺 `Content-Length` | `411` + 关闭 | POST 定长正文 |
| `Content-Length` 超上限 / 溢出 | `413` + 关闭 | 不等待正文 |
| `Content-Length` 非法（负数 / 非数字） | `400` + 关闭 | — |
| 两个不一致的 `Content-Length` | `400` + 关闭 | 请求走私（request smuggling） |
| `Transfer-Encoding`（含 chunked） | `501` + 关闭 | 本通道刻意不支持（§4） |
| 头部行缺冒号 | `400` + 关闭 | — |
| 正文长度超出「头 + 声明长度」 | `400` + 关闭 | 客户端在撒谎或发超长正文 |
| 并发连接超过 32 | 直接 `abort` | 不为超限连接分配任何缓冲 |
| 单请求等待超过 10s | `abort` + 移出缓冲表 | 250ms 巡检周期，无需每连接一个定时器 |

错误响应与正常响应都走同一条 `writeResponse`，随后统一 `finishConnection` 关闭连接；
`stop()` 会断开本对象到各 socket 的连接、清空缓冲表并关闭服务器，
重复 `stop()` / 反复 `start()`–`stop()` 均幂等。

---

## 6. ACP / IDE Agent 集成

两个抽象接口 `ISignalSource` / `IAgentBridge`（`src/contextapi/ISignalSource.h`、
`src/contextapi/IAgentBridge.h`）**保持不变**；**具体实现已落地（P7.5）**，位于
`src/contextapi/acp/`。传输选**本地 JSONL 文件**：不引入新依赖，且与主进程彻底解耦——
IDE 扩展崩溃不影响桌宠，桌宠未开启时信号只是堆在文件里。

```cpp
// src/contextapi/ISignalSource.h（接口不变）
class ISignalSource {
public:
    virtual ~ISignalSource() = default;
    virtual QString id() const = 0;              // "vscode" / "acp" / "git" …
    virtual bool available() const = 0;
    // 返回 false 表示该源当前无信号（不产生假状态）
    virtual bool poll(qint64 nowMs, CoreSignal &out) = 0;
};

// src/contextapi/IAgentBridge.h（接口不变）
class IAgentBridge {
public:
    virtual ~IAgentBridge() = default;
    virtual bool start(const QString &agentId) = 0;   // 建立会话
    virtual void stop(const QString &agentId) = 0;    // 结束会话
    virtual bool push(const QString &agentId, const QJsonObject &event) = 0; // 事件推送
};
```

### 6.1 显式信号源 `AcpSignalSource`

`src/contextapi/acp/AcpSignalSource.{h,cpp}`：外部进程（IDE 扩展 / 脚本）向一个
**JSONL 信号文件**追加「一行一个 JSON 对象」，桌宠按采样周期轮询读取。

行格式（每行一个对象）：

```json
{"sourceId":"vscode","kind":"agent.turn","payload":{"state":"vibe-coding"},"atMs":1699999999999}
```

- `kind` **必填**（缺失 / 非法 JSON 的行被忽略并计数，**不产生假信号**）；
- `sourceId` 可选（缺省用构造时注入的 id）；`payload` / `atMs` 可选。

读取语义（可核验，均有单测）：增量读取（记录字节偏移，不重复消费）；文件被截断 / 轮转时
自动从头再读；**无换行结尾的最后一行视为「尚未写完」不消费**；未换行尾部超过 1 MB 时
丢弃并计数（防御异常输入）。

### 6.2 会话桥接 `AcpAgentBridge`

`src/contextapi/acp/AcpAgentBridge.{h,cpp}`：会话生命周期 + 事件推送落到一个本地
**JSONL 事件日志**（IDE / Agent 侧自行消费），因此**不改总线与分发核心**；替换为管道 /
socket 时只是换一个 `IAgentBridge` 实现。

事件行格式：

```json
{"agentId":"vscode","atMs":1699999999999,"event":{"type":"context.changed"}}
```

- `start(agentId)`：建立会话（幂等；空 `agentId` 拒绝）；
- `stop(agentId)`：结束会话（幂等）；
- `push(agentId, event)`：仅对**已建立**的会话推送；会话不存在 → `false`（不静默写入）。

### 6.3 信号 → 工作态映射与「覆盖性输入」

映射规则在 `src/contextapi/acp/AcpSignalRules.{h,cpp}`（纯函数，可脱 UI 单测）：

| kind | 工作态 | 默认置信度 |
|---|---|---|
| `agent.turn` / `agent.burst` / `ai.iterate` | vibe-coding | 0.90 |
| `edit.burst` | coding | 0.85 |
| `file.saved` | coding | 0.70 |
| `build.start` / `debug.start` / `test.run` | debugging | 0.85 |
| `meeting.start` | meeting | 0.85 |
| `browse` / `browsing` | browsing | 0.80 |
| `game.start` | game | 0.85 |
| `idle` | idle | 0.80 |
| `afk` / `session.lock` / `session.locked` | afk | 0.95 |

- `payload.state`（`core::workStateId` 可识别的字符串）**优先级最高**，允许 IDE 直接定态；
- `payload.confidence` / `payload.holdMs` 可覆盖默认值；两者都不命中的信号**不映射、不覆盖**。

编排：`viewmodel::AcpSignalService` 以 1s 级周期轮询信号源 → 映射 → 广播 `workStateOverride`；
`viewmodel::WorkStateService::applyExternalState` 据此建立**覆盖窗口**：`holdMs` 窗口内
**保持显式状态，不被 platform 推断改写**；窗口过期自动回到推断（`clearExternalState` 可显式清除）。

> **显式优先于推断**：这是本节的语义核心——IDE 直接上报「正在与 Agent 快速迭代」时，
> 桌宠即按 `vibe-coding` 表现，而不必等推断「猜」出来；IDE 关闭后窗口过期即回落到推断。

- 默认**关**（`acp_enabled = false`）：关闭时**不轮询信号文件**（零开销）。
- 开启入口：右键菜单「ACP / IDE 信号」；信号文件默认位于数据目录 `acp-signals.jsonl`
  （可由 `acp_signal_path` 覆盖）。

### 6.4 与 MCP 的关系

MCP 是「宿主对外暴露能力 / 作为 Client 接入外部进程」；ACP 是「外部显式**告知**工作状态」。
两者互不替代：MCP 走 `capabilities.list` / `tools/call`，ACP 走 `ISignalSource` / `IAgentBridge`。

### 6.5 ACP 客户端（P7.6：直连 DeepSeek Harness）

§6.1–§6.2 的 JSONL 是「外部写文件、桌宠轮询」，适用于任意工具；对**实现了 ACP
（Agent Client Protocol）**的 agent，另有直连路径（`docs/ACP-EVAL.md`）：

- 链路：`dsh --profile acp`（**stdio NDJSON JSON-RPC**）← `AcpClient`（作为子进程拉起）
  → `session/update` → `AcpEventMapper` → `CoreSignal` → `AcpSignalService::submitSignal`
  → `WorkStateService` 覆盖窗口（与文件来源**完全同一条**下游路径）；
- 实现：`src/contextapi/acp/AcpClient.{h,cpp}`，依 ACP v1 规范**自行实现**，未引入第三方代码
  （官方无 C++ 绑定）；
- ⚠️ **传输差异**：ACP 用 **NDJSON**（`\n` 分隔、禁内嵌换行），与项目的 `McpStdioClient`
  （`Content-Length` 分帧）**不可复用**；
- 配置：`acp_dsh_path`（dsh 的 `lib/bin.js`；空 = 不启动子进程）、`acp_profile`（空 = `acp`）、
  `acp_workspace`（空 = 数据目录）；
- 会话：优先 `session/list` + `session/resume` **接管**既有会话（旁观），失败再 `session/new`；
- 权限：Agent 发 `session/request_permission` 时自动「允许一次」（可经
  `AcpClient::setAutoApprovePermissions(false)` 关闭）；
- 隔离：Agent 崩溃 → pending 提示结束 + 进程回收，主程序与其它能力不受影响。

---

## 7. 实现落位

| 交付项 | 代码位置 |
|---|---|
| Context 数据模型 | `src/contextapi/ContextSnapshot.{h,cpp}` |
| 分发核心（method 表 / 错误码 / 能力转发） | `src/contextapi/JsonRpcDispatcher.{h,cpp}` |
| 服务装配（dispatcher + 注册表 + 通道 + 门控） | `src/contextapi/ContextApiService.{h,cpp}` |
| 上下文能力插件（`context.*` / `pet.status` / `session.stats`） | `src/contextapi/builtin/ContextCapabilities.{h,cpp}` |
| MCP stdio 通道 | `src/contextapi/transport/StdioTransport.{h,cpp}` |
| 本地命名管道通道（P7.2） | `src/contextapi/transport/LocalPipeTransport.{h,cpp}`（管道名约定源 `kDefaultContextPipeName`） |
| MCP 控制台桥接进程（P7.2） | `src/app/mcp_bridge_main.cpp`（CMake 目标 `whalepet-mcp`，**控制台子系统**，stdio ↔ 命名管道字节转发） |
| 本地 HTTP 通道（回环） | `src/contextapi/transport/LocalHttpTransport.{h,cpp}` |
| 感知实现（P7.1：真实 Win32 采集） | `src/platform/Win32DesktopObserver.{h,cpp}`、`src/platform/Win32TextUtil.{h,cpp}` |
| 感知生命周期（钩子安装/卸载） | `src/platform/DesktopObserver.h`（`setObserving`）、`src/viewmodel/EnvironmentService.cpp` |
| 数据提供者接口 | `src/contextapi/IContextProvider.h` |
| view 侧数据提供者实现 | `src/viewmodel/PetContextProvider.{h,cpp}` |
| ACP 接口（不变） | `src/contextapi/ISignalSource.h`、`src/contextapi/IAgentBridge.h` |
| ACP 实现（P7.5） | `src/contextapi/acp/AcpSignalSource.{h,cpp}`、`AcpAgentBridge.{h,cpp}`、`AcpSignalRules.{h,cpp}` |
| ACP 事件映射与客户端（P7.6） | `src/contextapi/acp/AcpEventMapper.{h,cpp}`、`AcpClient.{h,cpp}` |
| ACP 编排（P7.5） | `src/viewmodel/AcpSignalService.{h,cpp}`（P7.6 增 `submitSignal`）、`src/viewmodel/WorkStateService.{h,cpp}`（覆盖窗口） |
| MCP Client / 外部进程插件（P7.4） | `src/plugin/process/McpStdioClient.{h,cpp}`、`McpPluginSession.{h,cpp}`、`ProcessPluginLoader.{h,cpp}`、`ProcessServerSpec.h` |
| 组合根装配与菜单门控 | `src/view/PetWindow.{h,cpp}`（`setupAcp` / `setAcpEnabled` / `startAcpClient` / `attachAcpSession` / `setupProcessPlugins`） |
| 设置项落位 | `src/model/SettingsData.h`、`src/model/SettingsRepo.cpp`（`json_ext`） |
| 单测 | `tests/test_context_dispatch.cpp`、`tests/test_context_pipe.cpp`（P7.2：命名管道 + 真实桥接进程端到端）、`tests/test_context_http_security.cpp`（**SECURITY-REVIEW.md 极端边界 1/2：HTTP 跨站调用与认证、请求缓冲与连接清理**）（**EX3 已移除 `tests/test_gamestate_boundaries.cpp` 与外部游戏陪玩**）、`tests/test_dll_plugin.cpp`（P7.3）、`tests/test_acp.cpp`、`tests/test_acp_event_mapper.cpp`、`tests/test_acp_client.cpp`、`tests/test_process_plugin.cpp`（+ 子进程 `tests/mcp_test_server.cpp` / `tests/acp_test_agent.cpp`、夹具 `tests/fixtures/acp-real-events.json`） |

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

**P7.4 / P7.5 追加（2026-10-02）**：新增 `test_acp`（ACP 信源 / 桥接 / 映射 / 覆盖窗口）
与 `test_process_plugin`（以真实子进程 `mcp_test_server` 端到端验证 MCP Client 的
握手 / 发现 / 异步转发 / 超时 / 崩溃隔离）；Debug / Release `ctest` 各 **20/20 通过**。

**P7.6 追加（2026-10-02）**：新增 `test_acp_event_mapper`（以**真实 dsh 报文夹具**
`tests/fixtures/acp-real-events.json` 驱动 `session/update` → `CoreSignal` → 工作态映射）
与 `test_acp_client`（以假 Agent 子进程 `acp_test_agent` 端到端验证握手 / `session/new` /
`session/list` + `resume` / `session/prompt` 事件映射 / 权限自动应答 / 崩溃隔离）；
Debug / Release `ctest` 各 **22/22 通过**。

**P7.2 / P7.3 追加（2026-10-02）**：新增 `test_context_pipe`（8 用例）与 `test_dll_plugin`。

- `test_context_pipe`：命名管道承载**完整 MCP 会话**（`initialize` → `tools/list`（含
  `context.snapshot`）→ `tools/call`）；token 门控（未带 / 带错 token → `-32003`）；
  **总开关同时启停两通道**（`start()` 后 HTTP 与管道均监听、`stop()` 后均停止）；
  以及**真实桥接进程 `whalepet-mcp.exe` 端到端**——`QProcess` 以 stdio 驱动桥接，
  桥接经命名管道连回宿主进程内服务，含 `--token` 注入与 `Content-Length` 分帧
  （宏 `WHALEPET_MCP_EXE` 指向构建产物 `$<TARGET_FILE:whalepet-mcp>`）。
- `test_dll_plugin`：以**真实构建的插件 DLL**（`ext_hello` 合法 / `ext_badabi` 负例）验证装载 /
  `apiVersion` 协商（不兼容被跳过且不影响其它插件）/ 失败降级 / 缺失目录与非插件文件不报错 /
  能力可见且可调用。

Debug / Release `ctest` 各 **24/24 通过**（CTest 22 → 24）。
P7.2 / P7.3 的逐项核查见 `docs/P7-REMAINING-INTERFACES-AUDIT.md`。

`test_context_http_security`（11 例）覆盖 **SECURITY-REVIEW.md 极端边界 1/2**：

- **跨站调用与认证**（1）：注册**有副作用**的假工具 `ext.test.sideEffect`（每次调用递增
  计数器），逐例断言「工具是否真的被触发」——
  外部 `Origin` + `text/plain`（CORS 简单请求）→ 415；`Origin: https://evil…` +
  `application/json` → 403；缺失 / 错误 / 只差一字符的 token → 401；
  不可信 `Origin` 矩阵（`null`、外部主机、userinfo、`127.0.0.2`、异端口 `[::1]`、
  `file://`、缺端口）全部 403；**对照组**：无 `Origin` 的本机客户端与同源同端口
  `Origin` → 200 且计数器递增。
- `Content-Type` 矩阵：`application/json` 及其 `; charset=utf-8` / 大小写变体放行；
  `text/plain` / 表单编码 / `text/json` / `application/json-patch+json` / 缺失 → 415。
- 响应**从不**带 `Access-Control-Allow-*`（预检与跨站读取必须失败），带 `nosniff`。
- **缓冲与连接清理**（2）：未收尾的超长头 / 4 条并发超长头 / 逐字节延迟的超长头 → 431
  且连接被关闭；超大与溢出 `Content-Length` → 413；负数 / 非数字 / 冲突长度 → 400；
  缺 `Content-Length` → 411；`Transfer-Encoding: chunked` → 501；
  合法但未收完的超大正文 → 继续等待（不误判、不丢请求），补齐后正常 200；
  慢速客户端在超时窗口内被巡检关闭（且通道仍能服务新请求）；
  反复 `start`/`stop` 期间套接字与缓冲被回收、重复启动幂等。

> **【EX3 已归档】** 原 `test_gamestate_boundaries`（23 例，**极端边界 3–6**：桥接输入 /
> CDP 发现与 WebSocket 生命周期 / profile 数值 / 只读内存地址与预算）随外部游戏陪玩一并移除，
> 测试文件已移至 `dump/tests/`（不入库）；以下断言不再在 CI 中运行，保留备查：

- **3 桥接输入**（`RpgMakerBridgeAdapter`）：空文件 / 超 1 MiB / 仅空行 / 读取中途截断 /
  被替换 / 200 KB 超长 jsonl 末行（正常取到）；socket 空响应、仅空白、畸形 JSON、
  端点格式非法、**无换行的 4 MiB 流**（按字节上限快速失败，早于总时长上限）、
  **每 100ms 发 1 字节的长期流**（`100ms < 1500ms` 单次等待，旧实现会永远读下去；
  现在被总时长上限约束并断开）、合法首行 + 尾随垃圾。
- **4 CDP**（`CdpWebSocketClient`）：`isTrustedDebuggerUrl` 白名单矩阵
  （远程主机 / 非 ws / userinfo / 非法端口 / 相对地址全部拒绝）；
  假 `/json` 服务返回远程主机 + `http` + userinfo + 非法端口 + 畸形 URL 共 7 个目标，
  只采用合规的 `ws://127.0.0.1:9222/...`；全不可信时错误里写明「已拒绝 N 个」；
  畸形 JSON / 非数组根 / 超大响应；**错误 id 后再发正确 id**（必须忽略前者、只取 42）、
  永不回应（超时）、**超大消息**（按上限中止并断开，随后可重连）、
  引擎 `error` 与 `exceptionDetails`；握手后被对端断开 + 反复 3 轮「连接→超时→关闭」
  均不留活动连接或悬挂请求。
- **5 profile**（`ProfileLoader`）：空 / 超大 / 截断 / 非对象根 / 不存在路径；
  `fields` 类型错误、空 `name`、非法 `kind`、`chain` 非数组 / 空数组 / 超跳数；
  `maxJumps` 负数 / 小数 / 超上限（上限边界本身允许）；偏移负数 / 小数 / `1e30` /
  `2^64` / 非数字；`maxBytesPerRound` 为 0 / 负数 / 小数 / `1e30` / 超上限
  （上限边界本身允许）；**失败时输出档案原封不动**（不留部分生效的 profile）。
- **6 内存读取**（`PointerChainResolver` / `ChainSampler` / `Win32GameMemoryReader`）：
  字节预算**恰好用满**通过、超 1 字节失败且失败不计入消耗、utf16 固定 128 字节；
  `staticRoot + chain[0]` 与 `pointer + chain[i]` 溢出 → 明确失败且**不发起读取**
  （用「记录每次读取地址」的假读取器断言）；魔数偏移溢出；
  **NaN / ±Inf / 超 int64 范围的 float / double 一律失败**（float→int 是 UB）；
  跳数边界、空指针、部分读取（不可读页）、进程退出、重复 attach/detach 句柄成对；
  `ChainSampler` 的 `moduleBase + moduleBaseOffset` 溢出 ⇒ 0 次读取；
  Win32 读取器真实 attach 到**自身**（读 PE `MZ` 头）、非法参数、
  **8 轮 attach/detach 后进程句柄数不增长**（`GetProcessHandleCount`）、
  以及**目标进程被杀后读取必须失败**（用本 exe 的副本作靶进程，
  `--boundary-child-sleep` 子进程模式）。

`test_win32_observer` 额外覆盖（全部用**注入替身读数**驱动，不安装任何系统钩子，
无桌面 / CI 环境也可稳定运行）：

- UTF-16 → UTF-8 中文标题不乱码；路径取进程名（识别 `\` 与 `/`，结尾分隔符返回空）；
- 前台采样器：填充 `appId`（只给文件名）/ `windowTitle`；读不到前台窗口时**不伪造**；
- 输入采样器：差分降级每次至多计 1、空闲时长计算与时钟回绕保护、注入替身**绝不安装钩子**；
- 系统状态采样器：锁屏 / 屏保 → `systemPaused`；
- 端到端：锁屏导致前台窗口读不到时仍判 `afk`（`docs/pitfalls/` TRAP-P7-006 的回归守卫）。

**人工目视项（待用户复验）**：

1. 右键菜单勾选「本地 Context API」后，日志应出现
   `[PetWindow] 本地 Context API 已启动，端口 = N`（端口由系统分配），
   且 `settings.json_ext` 的 `context_api_token` 被自动填入一个非空随机值。
   带该令牌 `POST http://127.0.0.1:<port>/rpc` 能取到快照：
   ```powershell
   $t = (Get-Content settings.json_ext -Raw | ConvertFrom-Json).context_api_token
   curl.exe -s -X POST "http://127.0.0.1:<port>/rpc" `
     -H "Content-Type: application/json" -H "X-WhalePet-Token: $t" `
     -d '{"jsonrpc":"2.0","id":1,"method":"context.snapshot"}'
   ```
   取消勾选后端口不再监听。
   **反向验证（安全修复）**：去掉 `X-WhalePet-Token` 头应得 `401`；
   把 `Content-Type` 换成 `text/plain` 应得 `415`；
   加上 `Origin: https://evil.example.com` 应得 `403`。
2. 同时勾选「工作状态感知」后，`context.snapshot` 的 `env.appId` / `env.windowTitle` /
   `env.idleMs` / `env.inputEvents` 应随前台切换在 1 个采样周期内变化；
   取消勾选后 `envAvailable` 应为 `false`（不再有真实数据）。
3. **P7.2 桥接**：勾选后日志应出现
   `[LocalPipeTransport] 本地 Context API 已监听命名管道 whalepet-context-v1`；
   以 MCP 客户端（或手动喂一帧 `Content-Length` 报文）经 `whalepet-mcp.exe` 取回
   `initialize` / `tools/list` 响应；取消勾选后管道不再监听、桥接无法连接（退出码 2）。
   因 `context_api_token` 非空，MCP 侧须由 `whalepet-mcp.exe --token <t>` 注入。
4. **无令牌降级**：手动把 `context_api_token` 清空后重新勾选，日志应出现
   `[PetWindow] 本地 Context API 已启动（仅命名管道：未配置令牌，不监听 HTTP 端口）`，
   此时 `curl` 连不上任何端口（fail closed），而命名管道仍可用。
