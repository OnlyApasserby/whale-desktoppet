# 桥接把裸 JSON 写入命名管道，对端 `StdioTransport` 只认分帧 → 无响应

> **原编号**：`TRAP-P7-010`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤 / 报错原文）**：`whalepet-mcp.exe`（P7.2 桥接）启动后**不产生任何响应**，
  stderr 为空、进程常驻不退出，看上去像「卡死」。复现：主程序勾选「本地 Context API」后运行桥接，
  向其 stdin 写入一帧 `Content-Length` 包裹的 `initialize`，stdout 无任何输出。
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug，
  二进制 `build\Debug\whalepet-mcp.exe`；宿主为进程内 `LocalPipeTransport`（`QLocalServer`）。
- **根因**：桥接从 stdin **解开** `Content-Length` 帧后，把帧内**裸 JSON 载荷**直接写进命名管道；
  而管道对端是 `StdioTransport`，其分帧解析器**只认** `Content-Length: N\r\n\r\n{...}`——
  收到裸 JSON 后一直等待头部，于是既不分发也不报错，表现为静默死锁。
  与「写 stdio 必须分帧」是同一件事，**两条边路都要分帧**。
- **解决或规避**：桥接新增 `makeFrame()`，**写管道前重新按 `Content-Length` 分帧**；
  修复后 `test_context_pipe` 8 个用例全绿，全量 CTest **24/24 passed**（Debug / Release）。
- **影响与关联文档**：`src/app/mcp_bridge_main.cpp`、`src/contextapi/transport/StdioTransport.cpp`（分帧）。
  **回灌规则**：任何把字节**转发**给 `StdioTransport` 的中间层，都必须**重新分帧**；
  排障时先确认「对端到底收到的是分帧还是裸载荷」——此前已用临时诊断确认对端收到 **88 字节裸载荷**。
