# ACP 实现评估（Vibe Coding 实时状态接入 WhalePet）

> 目标：让桌宠**实时获取 Vibe Coding 状态**（思考 / 编写 / 调试 / 报错）并做出反馈
> （`docs/ROADMAP-P7-Fin.md` 全阶段目标）。
>
> 本文是 **P7.6 的准入评估**（*历史文档*）：撰写时只做事实核查与方案对比、不含实现；
> **P7.6 已于 2026-10-02 按本文结论实施完成**（见 `docs/ROADMAP-P7-Fin.md` P7.6）。
> 参考实现：`references/dsh-whale-musume/`（MIT）。
>
> 结论先行：**推荐 ACP（Agent Client Protocol）客户端方案**，且必须自行实现协议层
> （官方无 C++ SDK）。
>
> **实施状态（2026-10-02 更新）**：`AcpEventMapper` 与 `AcpClient` 均已实现，并以**真实 dsh 报文**
> 完成测试（`test_acp_event_mapper`）与假 Agent 端到端测试（`test_acp_client`；
> Debug / Release 各 **22/22**）。本机 dsh 已定位：
> `DSH_HOME = C:\Users\19117\.dsh`，dsh 本体 `@deepseek-ai/dsh@0.1.5-rc.3`（npm 全局安装）。
>
> ⚠️ **两条 profile 不可混淆**：
> * **ACP（本方案的数据源）** = `dsh --profile acp` → **stdio JSON-RPC**（由客户端作为子进程拉起）；
> * `npx @deepseek-ai/dsh web`（`127.0.0.1:3080`）= **web profile**，是 HTTP 服务，
>   **不提供 ACP**，且其 bundles 中**不含** `dsh-acp`（实测其 profile 只挂了看板娘类插件）。

---

## 1. 术语澄清：两个「ACP」不是一回事

| | WhalePet 的「ACP」 | Agent Client Protocol（ACP） |
|---|---|---|
| 出处 | 本项目自定义（`docs/CONTEXT-API.md` §6） | Zed + JetBrains 主导的开放标准 |
| 语义 | 本地**显式信号**（`ISignalSource` / `IAgentBridge`） | **IDE ↔ Coding Agent** 通信协议 |
| 传输 | 本地 **JSONL 文件**（桌宠轮询） | **NDJSON over stdio**（Client 启动 Agent 子进程） |
| 现状 | 已实现（`src/contextapi/acp/**`；P7.5 信源 / 桥接 / 映射，P7.6 ACP 客户端） | 已实现（`AcpClient`，P7.6） |

**两者同名不同物。** 本评估中的「ACP」一律指 **Agent Client Protocol**；WhalePet 已有的
`AcpSignalSource` 是**信号落地层**，正好可以作为 ACP 事件的**下游消费者**（见 §6）。

---

## 2. 参考实现核查：`dsh-whale-musume`（MIT）

### 2.1 它是什么

- **DSH（DeepSeek Harness）的桌面看板娘插件**，纯前端注入 + 宿主侧静态资源路由；
- 版本 2.2.0，MIT 许可（`references/dsh-whale-musume/LICENSE`）；
- 支持旧版 Web（`http://127.0.0.1:3080`）与 DSH 桌面端（Electron，`dsh-app://app`）。

### 2.2 它的感知方式：**DOM 契约，不是 ACP**

全量检索该仓库：**`acp` / `agent-client-protocol` / `session/update` 零命中**。
它依赖 DSH 前端渲染出的 DOM 属性（参考项目内 `references/dsh-whale-musume/docs/desktop-0.2.0-rc.2-contract.md`）：

| 语义 | DOM 信号 | 备注 |
|---|---|---|
| 会话运行中（最佳「AI 在干活」） | `[data-chat-running]` | 存在性布尔，回合结束即消失 |
| 工具执行（**精确到工具名**） | `[data-tool]`（值 = 工具名）+ `data-state` ∈ `preparing / running / stopped / error / ok` | 0.2.0-rc.2 起为**最精确**的工具信号 |
| 思考 / 流式 | `[data-variant="think"][data-state="running"]`、`[data-streaming]`、`[data-shimmer]` | |
| 终端运行中 | `[data-terminal][data-running]` | |
| 报错 | `[data-tool][data-state="error"]`、`[data-state="error"]`、`[data-error]` | |
| 设置面板是否打开 | `[data-shortcut-modal="settings"]`（唯一可靠判据） | `[role="dialog"]` 会误命中引导弹窗 |

**它按 `data-tool` 的取值直接映射姿态**（跑命令 / 改文件 / 搜索 / 测试 / 评审 / 部署 / 调试），
替代了早期的文本关键词猜测。

### 2.3 可复用与不可复用

| 项 | 结论 |
|---|---|
| **信号维度**（要区分思考 / 工具 / 终端 / 错误 / 成功） | ✅ **可复用**——这正是 WhalePet 需要的状态集合 |
| **工具名 → 姿态**的映射思路 | ✅ **可复用**——WhalePet 已有 `core::workStatePose()` 可作为落点 |
| **DOM 契约选择器** | ❌ **不可直接移植**：那是 DSH 前端 DOM，WhalePet 是独立 Qt 进程，无法注入其渲染进程 |
| 版本漂移的历史证据 | ⚠️ **重要警示**：`contract.md` 显示 0.2.0-rc.2 一次性让 12 条旧选择器失效（`data-role`/`data-tool-card`/`data-theme`/`data-status` 等全库 0 命中）→ DOM 感知**必然随宿主 UI 版本反复返工** |

---

## 3. 候选路径对比

| 维度 | **A. ACP 客户端** | **B. DOM / CDP 感知** | **C. MCP** |
|---|---|---|---|
| 原理 | WhalePet 作为 ACP Client 启动 dsh 的 ACP Server（stdio），订阅 `session/update` | WhalePet 经 CDP 连 DSH 调试端口，读 DOM 契约 | WhalePet 作 MCP Server，由外部 Agent 调用 |
| 语义质量 | **高**：结构化事件（思考 / 工具 / 状态 / 计划） | 中：DOM 属性，需自行去噪与基线比对 | 低：需 Agent 主动调用才有数据 |
| 稳定性 | **高**：协议化，v1 已稳定（SDK 1.0 发布） | **低**：随 DSH UI 版本漂移（§2.3 已证） | 高 |
| WhalePet 侧工作量 | 中（NDJSON JSON-RPC 客户端 + 事件映射） | 中高（CDP 协议 + DOM 解析 + 版本适配 + 去抖） | 高（须先补 P7.2 桥接 exe） |
| 前置条件 | dsh + `@deepseek-ai/dsh-acp` 插件 | DSH 桌面端开启远程调试端口 | CodeBuddy/dsh 配置为 MCP Client |
| 许可 | 规范/schema 为 Apache-2.0 | 无第三方代码 | MCP 规范 MIT |
| 与现有代码衔接 | **直接**：`AcpSignalSource`（JSONL）＋ `AcpSignalRules`（映射）＋ `WorkStateService`（覆盖窗口）已就绪 | 需另建 DOM 适配层 | 需先补 MCP Server 侧通道 |

> **结论**：**A 为主**。B 仅在「ACP 插件不可用」时作为降级；C 是并行能力（对外暴露上下文），
> **不能**替代「实时感知 Agent 在干什么」这一诉求。

---

## 4. ACP 协议要点（v1，已核实）

来源：`agentclientprotocol.com/protocol/v1/{transports,initialization,session-setup,prompt-turn}.md`

### 4.1 传输（硬约束）

- Client **启动 Agent 作为子进程**；Agent 从 `stdin` 读、向 `stdout` 写 JSON-RPC；
- **stdout 只允许合法 ACP 消息**（MUST NOT 写别的），`stderr` 才是日志通道；
- 消息为 **NDJSON**：每条一个 JSON 对象、以 `\n` 分隔、**禁止内嵌换行**（因此必须紧凑序列化）；
- ⚠️ **与 WhalePet 现有 `StdioTransport` 不同**：项目用的是 `Content-Length` 分帧（LSP 风格），
  **两套分帧不能复用**，ACP 侧须另写 NDJSON 读写。

### 4.2 流程

```
initialize（protocolVersion / clientCapabilities / clientInfo）
   → session/new { cwd, mcpServers } → sessionId
   → session/prompt { sessionId, prompt[] } → StopReason（回合结束）
   ↗ 期间 Agent 持续推送 session/update（通知，无 id）
   → session/cancel（可选）
```

### 4.3 `session/update` 变体 → 我们的状态需求（关键映射）

| `sessionUpdate` | 含义 | 映射到的 WhalePet 语义 |
|---|---|---|
| `agent_thought_chunk` | Agent 推理（**思考**） | → `vibe-coding` / `thinking` |
| `agent_message_chunk` | 回复流式分片 | → `vibe-coding` / `waiting` |
| `tool_call` | 新建工具调用（含 `title` / `kind` / `status`） | 按 **`title`（工具名）** 细分——**实测 dsh 的 `kind` 恒为 `other`**，不可作为分类依据 |
| `tool_call_update` | 状态推进（`pending` / `in_progress` / `completed` / `cancelled`）与结果 | `in_progress` → 工作中；`completed` → 成功 |
| `plan` | 执行计划（`entries[]`，含 `priority` / `status`） | → `vibe-coding` / `tool` |
| `available_commands_update` | 可用斜杠命令变更 | 忽略 |
| `current_mode_update` / `session_info_update` | 会话模式 / 元数据 | 忽略或用于展示 |
| `usage_update` | 上下文用量与费用（`used` / `size` / `cost`） | 可选：余额/用量反馈 |
| （`tool_call` 的 `isError` / `status=cancelled`） | **报错** / 中断 | → `debugging` / `failure` |

> 「**编写 / 调试 / 报错**」的区分依据是 `tool_call` 的 **`kind` / `title`** 与 `status`：
> 需要按工具名做细分映射（与参考实现的 `data-tool` → 姿态同构）。
>
> 注：上表第三列中的 `thinking` / `waiting` / `tool` / `failure` 是**评估期的描述性写法**，
> **不是** `core::WorkState` 的取值。`WorkState` 的全部取值为
> `unknown / idle / reading / coding / vibe-coding / debugging / browsing / meeting / game / afk`
> （`src/core/WorkState.h`）；实际落到的状态见 §6.2 后的差异说明。

---

## 5. 关键约束：ACP 官方**没有 C++ SDK**

官方 `LIBRARIES` 仅提供：**Kotlin / Java / Python / Rust / TypeScript**（+ Community / Testing）。
TypeScript 包为 `@agentclientprotocol/sdk`，官方 Rust/TS 实现均为 **Apache-2.0**。

因此对 WhalePet（Qt/C++）而言，**「引用 ACP 官方 SDK」在工程上不成立**——不能 `find_package`
也不能链接。可选做法只有：

| 做法 | 说明 | 许可影响 |
|---|---|---|
| **A1. 依规范自行实现协议层**（推荐） | 只实现用到的子集（`initialize` / `session/new` / `session/update` / `session/cancel`），NDJSON + JSON-RPC，零第三方依赖 | 不复制任何代码 → **无需附带许可文本**，仅需在文档注明「依 ACP v1 规范实现」 |
| A2. 引用官方 schema 派生类型 | 若把官方 JSON Schema 或生成类型**纳入仓库**，则属「分发衍生作品」 | 必须附带 **Apache-2.0 LICENSE + NOTICE**，并在文件头标注来源与修改说明 |
| A3. 引入 TS/Rust 侧 helper 进程 | 多一个进程与 Node/Rust 工具链 | 与项目「零第三方依赖」口径冲突，不推荐 |

> **合规建议**：采用 **A1**。这样既满足「仅引用 MIT」的原始约束（不引入任何第三方代码），
> 也避免 Apache-2.0 的分发义务；若后续确需内联官方 schema，则按 **A2** 附声明
> （模板见 §8）。

---

## 6. 推荐方案（A1）与设计要点

### 6.1 架构（复用既有链路，不新建总线）

```
dsh（ACP Server，stdio）
        ▲ NDJSON JSON-RPC
        │
WhalePet: AcpClient（QProcess 子进程）
        │  session/update（通知）
        ▼
   AcpEventMapper（ACP 事件 → CoreSignal.kind）
        │
        ├─► AcpSignalSource（既有：JSONL 落地，便于排查/复现）
        └─► WorkStateService::applyExternalState（既有：覆盖窗口，显式优先于推断）
        ▼
   PetController → PetStateMachine → 立绘 / 台词
```

**要点**：WhalePet 侧的**下游已经全部就绪**（P7.4/P7.5 交付），本方案只需新增
**① ACP 客户端**（NDJSON 读写 + 握手 + 订阅）与 **② 事件映射器**（纯函数，可脱 UI 单测）。

### 6.2 建议新增的 `CoreSignal.kind`（需同步 `AcpSignalRules`）

| ACP 事件 | 新增 kind | 建议工作态 |
|---|---|---|
| `agent_thought_chunk` | `agent.thought` | `vibe-coding` |
| `agent_message_chunk` | `agent.message` | `vibe-coding` |
| `tool_call`（`kind=execute` / bash） | `tool.command` | `debugging` |
| `tool_call`（`kind=edit` / 写文件） | `tool.edit` | `coding` |
| `tool_call`（`kind=read` / `search`） | `tool.search` | `reading` |
| `tool_call_update`（`completed`） | `tool.done` | 保持 / 成功 |
| `tool_call_update`（错误 / `cancelled`） | `tool.error` | `debugging` |
| `plan` | `agent.plan` | `vibe-coding` |
| `StopReason=end_turn` | `agent.turn.end` | 清除覆盖，回落推断 |

> ⚠️ **上表是评估期提案，与实际交付存在差异**——**唯一真源**是代码：
> `src/contextapi/acp/AcpEventMapper.cpp`（事件 → kind）与 `AcpSignalRules.cpp`（kind → 工作态）。
> 已交付的差异：
> * `tool.read` / `tool.search` → **`coding`**（非 `reading`）；`tool.edit` → `coding`；`tool.command` → `debugging`；
> * 新增 `tool.fetch` → `browsing`、`tool.plan` / `tool.subagent` → `vibe-coding`、`tool.other` → `coding`；
> * `tool.done` / `tool.cancelled` / `agent.usage` **不映射**（不改写当前状态）；
> * **不存在 `agent.turn.end`**：覆盖的回落由 `AcpClient` 的 `promptSettled` + `holdMs` 窗口过期完成
>   （`WorkStateService`），没有对应的 kind。

### 6.3 dsh 侧配置（已核实存在，语法以官方为准）

- 插件：**`@deepseek-ai/dsh-acp`**（官方 config-catalog 收录，`AcpConfig{ provider, model, sessionListPageSize }`，
  生产用 **stdio**；`stream` 为运行时 seam，不可经配置文件设置）；
- 该插件注入 `agents` / `llm` / `sessions` / `sessionPersistence`，即**以 dsh 自身为 Agent 运行时**；
- 配置落点：dsh profile 的插件配置（`cordis.yml` 层）；`DSH_HOME` 下 `profiles/{acp,desktop,headless,web}`
  的存在说明 **`acp` profile 是 dsh 的一等形态**。

---

## 7. 依赖

| 侧 | 依赖 | 说明 |
|---|---|---|
| WhalePet | `Qt6::Core`（已有） | `QProcess` + `QJsonDocument`，**无新增第三方依赖** |
| dsh | dsh 本体 + `@deepseek-ai/dsh-acp` 插件 + 一个可用 provider/model | 用户环境 |
| 运行期 | dsh 可执行文件路径（配置项） | 建议与 `plugins.json` 同风格落到 `<数据目录>` |

---

## 8. 许可与引用声明

- **ACP 协议规范与官方实现**：Apache-2.0（`agentclientprotocol/agent-client-protocol`、
  `agentclientprotocol/typescript-sdk`）；
- 本方案（A1）**不复制官方代码**，因此仅需在实现文件与本文档注明：

  ```
  // 本文件依 Agent Client Protocol v1 规范（https://agentclientprotocol.com）独立实现，
  // 未复制官方代码（官方 Rust/TypeScript SDK 以 Apache-2.0 授权）。
  ```

- 参考实现 `references/dsh-whale-musume` 为 **MIT**，仅作为**信号维度与映射思路**的参考，
  未复制其代码；
- 若后续内联官方 schema，必须补 **Apache-2.0 LICENSE 全文 + NOTICE**（含来源、版本、修改说明）。

---

## 9. 验证方法

### 9.1 可自动化（不依赖 dsh）

- `test_acp`（既有）继续覆盖信号落地与覆盖窗口；
- `test_acp_client`（**P7.6 已交付**）：NDJSON 分帧（含跨包截断 / 多帧粘包）、握手字段、
  `session/update` 各变体解析、映射表、异常与超时——用**假 Agent 可执行文件**
  （`tests/acp_test_agent.cpp`）驱动，与 `tests/mcp_test_server.cpp` 同法
  （控制台程序 + 标准 C stdio，见 `TRAP-P7-008`）。

### 9.2 真实 dsh 采集（已完成，2026-10-02）

本机 dsh 已定位（`DSH_HOME = C:\Users\19117\.dsh`，`@deepseek-ai/dsh@0.1.5-rc.3`，npm 全局），
并已用**真实服务**完成采集（客户端作为子进程拉起 `dsh --profile acp`，NDJSON over stdio）：

| 步骤 | 结果 |
|---|---|
| 启动 ACP 服务 | `node <npm-global>/@deepseek-ai/dsh/lib/bin.js --profile acp`（`acp` profile 首用自动初始化） |
| `initialize` | `protocolVersion=1`、`agentInfo={name:"deepseek-harness-acp",version:"0.0.1"}`、`sessionCapabilities={close,list,resume}` |
| `session/new` | 返回 `sessionId` + `configOptions`（`model` / `reasoning_effort`） |
| `session/prompt`（纯文本） | `agent_message_chunk` → `usage_update` → `stopReason=end_turn` |
| `session/prompt`（带工具） | `agent_thought_chunk` → `tool_call(title="read", status="in_progress")` → `tool_call_update(status="completed")` → `agent_thought_chunk` → `agent_message_chunk` → `usage_update` |

采集到的**原始报文**已固化为 `tests/fixtures/acp-real-events.json`，被 `test_acp_event_mapper`
直接消费——因此该测试同时是「dsh 事件形状是否漂移」的**回归守卫**。

> 说明：本节验证的是**协议形状与映射正确性**；**完整链路**（WhalePet 进程内 `AcpClient`
> 拉起 dsh 子进程并驱动桌宠）已于 **P7.6 交付并通过端到端验证**
> （`initialize` → `session/new` → `session/prompt` → `agent.message` → `stopReason=end_turn`，
> 见 `ROADMAP-P7-Fin.md` P7.6 验证记录与本文件 §12）。

### 9.3 契约漂移监控（若将来评估方案 B）

参考实现内的 `references/dsh-whale-musume/docs/desktop-0.2.0-rc.2-contract.md` §8 提供了一组
DevTools 探测表达式，可直接复用为「DOM 契约是否失效」的巡检脚本。

---

## 10. 风险与工作量

| 风险 | 影响 | 缓解 |
|---|---|---|
| ACP v2 已进入 Draft | 协议升级可能破坏兼容 | 只实现 v1 稳定子集；把 `protocolVersion` 协商做成入口参数 |
| 官方无 C++ SDK | 需自行实现协议层 | 已按 A1 评估；实现量集中在「分帧 + 方法子集 + 事件映射」 |
| 本机无 dsh | 端到端无法验证 | **该风险已消除**：本机 dsh 已定位并完成真实端到端（§9.2 / §12）；自动化侧另有假 Agent 兜底 |
| `session/update` 变体随版本增长 | 映射需维护 | 未知变体**按忽略处理并计数**（不猜测、不误判状态），与既有「不伪造数据」口径一致 |
| 子进程崩溃 | 影响桌宠 | 复用 P7.4 已有隔离范式（退出 → 标记不可用 + 不阻塞 GUI） |

**工作量预估**（相对 P7.4「外部进程插件」）：约 **0.6–1.0 倍**——
`AcpClient`（NDJSON/JSON-RPC/握手/生命周期）为主，`AcpEventMapper` 为纯函数可单测，
下游（信号源 / 映射 / 覆盖窗口 / 隔离）**已全部就绪**。

---

## 11. 结论与下一步

1. **结论**：ACP 路径可行且语义最优；参考实现的**信号维度**可复用，但其 **DOM 传输不可移植**。
2. **已完成（2026-10-02）**：
   - `AcpEventMapper`：`session/update` → `CoreSignal` 纯映射；
   - `AcpSignalRules` 新增 ACP 事件 → 工作态（thought → vibe-coding；tool.edit → coding；
     tool.command / tool.error → debugging；`tool.done` / `agent.usage` **不映射**）；
   - `AcpClient`：NDJSON over stdio + `QProcess` 子进程 + `initialize` / `session/new` /
     `session/list` / `session/resume` / `session/prompt` / `session/cancel` + 权限自动应答 + 崩溃隔离；
   - `AcpSignalService::submitSignal`：ACP 事件与文件轮询**共用**同一条映射 / 广播路径；
   - 设置项 `acp_dsh_path` / `acp_profile` / `acp_workspace` 与组合根装配
     （`PetWindow::startAcpClient` / `attachAcpSession`）；
   - 测试：`test_acp_event_mapper`（真实报文夹具）+ `test_acp_client`（假 Agent 端到端），
     CTest 20 → **22**，Debug / Release 各 **22/22**。
3. **真实端到端已验证**：`dsh --profile acp` → `initialize`（agent = `deepseek-harness-acp`）→
   `session/new` → `session/prompt` → 收到 `agent.message` 信号 → `stopReason = end_turn`。
4. **后续（P7.6 之外）**：ACP v2（Draft）；权限交互 UI；由桌宠主动发起 `session/prompt`。

---

## 附：本评估的事实来源

| 事实 | 来源 |
|---|---|
| ACP 传输为 NDJSON/stdio、stdout 纯协议 | `agentclientprotocol.com/protocol/v1/transports.md` |
| ACP 方法流程与 `session/update` 变体、`StopReason` | `.../v1/session-setup.md`、`.../v1/prompt-turn.md` |
| ACP 官方 SDK 语言与许可（Apache-2.0，无 C++） | `github.com/agentclientprotocol/typescript-sdk`、`.../agent-client-protocol` |
| dsh ACP 插件 `@deepseek-ai/dsh-acp` 与 `AcpConfig` | `deepseek-harness.github.io/.../reference/config-catalog` |
| 参考实现为 DOM 感知、无 ACP | `references/dsh-whale-musume/**`（含其内的 `references/dsh-whale-musume/docs/desktop-0.2.0-rc.2-contract.md`） |
| ~~本机无 dsh（`ENOENT`）~~ **已作废** | *评估撰写时的实测*：旧路径 `D:\DeepseekHarness_Data\.dsh` 不存在；其后已在 `C:\Users\19117\.dsh` 定位并完成真实端到端（§9.2 / §12） |
