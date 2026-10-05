# lambda 不能带默认参数 → 错误回调改用显式类型别名

> **原编号**：`TRAP-P7-003`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：
  ```
  src\contextapi\JsonRpcDispatcher.cpp(79,9): error C2064: 项不会计算为接受 2 个参数的函数
  src\contextapi\JsonRpcDispatcher.cpp(90,9): error C2064: 项不会计算为接受 3 个参数的函数
  ... (同类共 10 处)
  src\contextapi\JsonRpcDispatcher.cpp(201,9): error C2664: 无法将参数 4 从
      “const JsonRpcDispatcher::handle::<lambda_2>”转换为“const JsonRpcDispatcher::Responder &”
  ```
- **根因**：错误回调写成 `[](int code, const QString &msg, const QJsonObject &data = QJsonObject())`；
  **C++ 的 lambda 参数不允许默认实参**，故两参调用点全部无法编译。
  同时它在头文件里以 `const Responder &`（即 `std::function<void(const QJsonObject&)>`）传递，
  三参 lambda 无法转换（C2664）——两个错误同源：**一个类型别名承担了两种签名**。
- **解决或规避**：`JsonRpcDispatcher` 增加 `using ErrorResponder = std::function<void(int, const QString&, const QJsonObject&)>`；
  `invokeCapability` 的第 4 个参数改用 `ErrorResponder`；全部错误路径**显式传三个实参**
  （无 data 时传 `QJsonObject()`），不再依赖默认参数。
- **影响与关联文档**：`src/contextapi/JsonRpcDispatcher.{h,cpp}`。

---
