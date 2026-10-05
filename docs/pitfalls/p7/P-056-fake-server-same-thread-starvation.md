# 假 TCP 服务与被测代码同线程 ⇒ 被测的阻塞读饿死服务端

> **原编号**：`TRAP-P7-018`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：想让假 socket 服务「分多次、每次间隔一段时间」发数据，
  于是和被测适配器放在同一个线程：被测侧 `waitForReadyRead()` 阻塞 →
  该线程的事件循环停摆 → 服务端的 `QTcpServer` **永远不派发** `newConnection` /
  `readyRead` → 表现为「连接失败」，看起来像被测代码有 bug。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；`tests/test_gamestate_boundaries.cpp`。
- **根因**：Qt 的事件驱动只在**事件循环运行时**才推进。`waitForReadyRead` /
  `waitForConnected` 是**阻塞**调用（内部只跑 socket 自己的等待，不跑用户的
  事件循环），因此同线程的其它 `QObject` 在此期间完全收不到事件。
  这与 `tests/test_context_pipe.cpp` 里 `PipeClient` 顶部注释、
  以及 `docs/pitfalls/` TRAP-P7-012 是**同一条约定**的另一面。
- **解决或规避**：**要喂数据的假服务必须放到独立线程**（本文件里的 `ScriptedServer`：
  `QThread::run()` 里建 `QTcpServer` + `QEventLoop`，端口用「轮询到非 0」等待就绪）。
  反之，被测代码内部跑**嵌套事件循环**的（如 `CdpWebSocketClient`），
  假服务**可以**留在同线程。
- **影响与关联文档**：`tests/test_gamestate_boundaries.cpp` 的 `ScriptedServer`。
