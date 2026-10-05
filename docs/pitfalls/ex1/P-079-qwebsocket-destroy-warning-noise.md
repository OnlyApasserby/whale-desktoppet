# `QWebSocket` 销毁期的 Qt 内部告警（噪声，勿误判为缺陷）

> **原编号**：`TRAP-EX1-006`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`test_rpgmaker_adapters` 的 CDP 用例执行时打印（逐字）：
  ```
  QWARN  : RpgMakerAdaptersTest::cdpAdapterReadsFieldsAndProbe()
    QObject::disconnect: wildcard call disconnects from destroyed signal of QTcpSocket::unnamed
  ```
  用例本身 `PASS`（8 passed / 0 failed），不影响结果。
- **根因**：该告警来自 Qt `QWebSocket`/`QWebSocketServer` 内部在对象析构路径上的
  `disconnect(…, nullptr, …)`；`QTcpSocket::unnamed` 为 Qt 内部传输套接字，与业务代码无关。
- **解决或规避**：**无需修复**。记录以**避免后续误判为「套接字泄漏/未清理」而做无谓改动**；
  验收以「用例是否 PASS」为准，不以该 QWARN 为准。
- **影响与关联文档**：`src/gamestate/CdpWebSocketClient.*`、`tests/test_rpgmaker_adapters.cpp`；
  关联 `docs/ROADMAP-ex1.md` EX1.3 验收标准「连接失败时优雅降级」。
