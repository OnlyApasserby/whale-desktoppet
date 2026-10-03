# 安全与资源边界审查

审查对象：当前工作区代码（`main` 分支）；未提交差异为空。

## 已确认问题

| # | Severity | File | Lines | Vulnerability | Confidence |
|---|----------|------|-------|---------------|------------|
| 1 | 🟡 MEDIUM | [src/contextapi/transport/LocalHttpTransport.cpp](./src/contextapi/transport/LocalHttpTransport.cpp) | 179-211 | Context API 的 HTTP 请求在 token 为空时跳过认证，且未校验 `Origin` 或限制 `Content-Type`。启用 API、配置空 token 并有可调用的外部 MCP 工具时，网页可能向本机端口发送跨站 JSON-RPC 请求并触发工具副作用。API 默认关闭；浏览器对本机端口的访问策略及实际插件副作用未实测。 | 8/10 |

相关上下文：[src/model/SettingsData.h](./src/model/SettingsData.h)、[src/plugin/process/McpPluginSession.cpp](./src/plugin/process/McpPluginSession.cpp)。

建议对有副作用的 HTTP 工具调用强制非空 token，并验证可信来源；不得仅依赖回环绑定作为授权机制。

## 极端边界测试建议

以下为建议新增的测试场景，本次审查未运行这些测试。

1. **HTTP 跨站调用与认证** — `LocalHttpTransport::handleRequestLine`、`JsonRpcDispatcher::handleSync`
   - 注册一个会递增计数器的有副作用假工具。
   - 分别发送带外部 `Origin` 的 `text/plain` JSON-RPC POST、缺失 token、错误 token，以及可信来源且认证正确的请求。
   - 预期：拒绝不可信或未认证请求，假工具调用次数保持为零；仅允许策略认可的请求执行。

2. **HTTP 请求缓冲与连接清理** — `LocalHttpTransport::onReadyRead`、`stop`
   - 多连接并发发送超长/逐字节延迟的未完成请求头、超大 `Content-Length`、冲突或溢出的长度头、未结束的超大正文；期间重复 `stop/start`。
   - 预期：单连接缓冲和等待时间有上限；超限连接被关闭；停止后套接字及缓冲被清理，不因慢速客户端长期占用资源。

3. **桥接文件与 socket 输入** — `RpgMakerBridgeAdapter::read`、`readFileSnapshot`、`readSocketSnapshot`
   - 覆盖空文件、超大 JSON、超长 JSONL 末行、仅空行、读取中途截断/替换；socket 空响应、畸形 JSON、无换行超大流，以及每隔不足 1500ms 仅发送一个字节的长期流。
   - 预期：输入大小和总读取时长受限；超限、超时或截断时安全失败并关闭连接，不无限等待或累积缓冲。

4. **CDP 发现与 WebSocket 生命周期** — `CdpWebSocketClient::discoverWebSocketUrl`、`connectToUrl`、`evaluate`
   - 假 CDP 服务返回远程主机、非 `ws/wss` 协议、userinfo、无效端口或畸形的 `webSocketDebuggerUrl`；另覆盖超大 `/json` 响应、畸形 JSON、超大 WebSocket 消息、错误/迟到的响应 ID，以及超时断连后重复连接。
   - 预期：自动发现的地址仅限预期本机端点；响应有大小上限；超时、断连和重试不会留下活动连接或悬挂请求。

5. **Profile 数值与文件边界** — `ProfileLoader::loadFromFile`、`loadFromJson`
   - 覆盖空/超大文件、截断 JSON、字段类型错误、空/超长 chain、`maxJumps` 负数及整数边界、负数/小数/大于 `uint64` 的偏移，以及极大的 `maxBytesPerRound`。
   - 预期：超范围或超限配置在适配器启动前被拒绝；不发生整数转换溢出；失败时不留下部分生效的 profile。

6. **内存读取地址与预算边界** — `PointerChainResolver::resolve`、`readRaw`、`readField`；`Win32GameMemoryReader::attach/read/detach`
   - 用假 reader 覆盖预算恰好用满及超一字节、最大跳数、空指针、`staticRoot + offset` 和 `pointer + offset` 溢出、部分读取、进程退出，以及重复 attach/detach；字段值覆盖 NaN、无穷大和超出整数范围的浮点值。
   - 预期：溢出地址不被读取；预算超限失败关闭；异常数值不会触发未定义转换；每轮结束句柄均释放。

7. **陪玩服务重复启停与异常退出** — `GameCompanionService::start/stop/tick`
   - 快速重复 start/stop；覆盖适配器 attach/read 失败、invalidated 后自动停止、停止期间触发计时器回调，以及析构时仍有活动适配器。
   - 预期：同一时刻最多一个计时器/适配器；失败和析构路径均释放资源，不重复上报旧状态。

## 审查范围与限制

- 检查了本地 Context API、能力分发与外部插件调用，以及游戏 profile、CDP、桥接、进程内存读取和陪玩服务相关路径。
- 未运行 GUI 程序、浏览器跨站实测或真实外部 MCP 插件；浏览器是否可访问特定本机端口以及插件工具的实际副作用尚未在运行时验证。
- 未在真实 Windows 游戏进程上执行内存读取边界测试；资源边界与生命周期用例仍需通过假 reader、socket 假服务及集成测试确认。
