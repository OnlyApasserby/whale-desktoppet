# 裸 `QTcpServer` 冒充 CDP 端点 ⇒ 握手就超时，测不到真正的路径

> **原编号**：`TRAP-P7-017`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：用 `QTcpServer` 起一个「连上就永不回应」的端点，
  期望验证 `CdpWebSocketClient` 的「求值超时 → 反复重连不留悬挂请求」，
  结果 `connectToUrl()` 直接失败：
  `CDP WebSocket 连接失败：超时或地址无效`。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；`tests/test_gamestate_boundaries.cpp`。
- **根因**：`CdpWebSocketClient` 用的是 **`QWebSocket`**，它必须先完成
  HTTP `Upgrade` 握手；裸 TCP 服务端永远不回握手报文，于是**连接阶段**就超时，
  根本走不到「已连接 → 求值超时」那段逻辑。测试前提错误，不是被测代码有问题。
- **解决或规避**：改用同线程的 `QWebSocketServer`（`NonSecureMode`），
  在 `newConnection` 里按用例脚本决定「永不回应」或「握手后立刻 `close()`」。
  **注意**：`CdpWebSocketClient::connectToUrl` / `evaluate` 内部跑的是**嵌套**
  `QEventLoop`，所以同线程的 `QWebSocketServer` 会被正常驱动，**不需要**额外线程。
- **影响与关联文档**：`tests/test_gamestate_boundaries.cpp` 的
  `cdpReconnectAfterTimeoutLeavesNoActiveConnection`；与 TRAP-P7-012 同源
  （「同步完成的信号」/「事件循环归属」类误判）。
