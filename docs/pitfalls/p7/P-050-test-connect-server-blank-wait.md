# 单测 `connectToServer()` 后同步等信号 → 每个用例白等 5s

> **原编号**：`TRAP-P7-012`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤 / 报错原文）**：`test_context_pipe` 8 个用例**全部通过**，但单个用例约 **5s**、
  全目标约 **21.5s**（接近每个用例都触发一次 5s 超时）。复现：`ctest --test-dir build -C Debug -R test_context_pipe -V`。
- **环境**：同 TRAP-P7-010（Debug）；被测对象为 `QLocalServer` / `QLocalSocket` 本机命名管道。
- **根因**：`PipeClient::connectTo()` 调 `QLocalSocket::connectToServer()` 后**无条件** `loop.exec()`
  等 `connected` 信号；而本机命名管道连接常**同步**完成，`connected` 信号在 `exec()` 之前就已发出，
  于是 `exec()` 一直等到兜底 `QTimer` 的 5s 超时——功能不受影响，但测试慢且掩盖真实等待。
- **解决或规避**：连接后**先查 `state() == QLocalSocket::ConnectedState`**，已连接则直接返回；
  否则再 `exec()` 等信号。修复后单个用例耗时降到约 **1.7s**（8 用例）。
- **影响与关联文档**：`tests/test_context_pipe.cpp`。
  **回灌规则**：Qt 异步 API 等待完成时，先判断「是否已同步完成」，再决定是否进事件循环。

---
