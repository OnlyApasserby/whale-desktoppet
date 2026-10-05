# `emit` 不能用作成员函数名

> **原编号**：`TRAP-P7-001`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`whalepet_plugin` 目标编译失败，报错原文（节选，逐字）：
  ```
  src\plugin\Capability.cpp(98,25): error C2589: “(”:“::”右边的非法标记
  src\plugin\Capability.cpp(113,21): error C2511: “void whalepet::plugin::InvokeContext::respond(const int)”
                                      :“whalepet::plugin::InvokeContext”中没有找到重载的成员函数
  src\plugin\Capability.h(79,73): error C2664: 无法从“unknown”转换为“const QJsonObject”
  ```
  复现步骤：直接 `cmake --build build --config Debug`（`whalepet_plugin` 为本阶段新增目标）。
- **根因**：`InvokeContext` 的私有方法命名为 `emit`。`emit` 是 Qt 的关键字宏（**展开为空**），
  于是声明 `void emit(QJsonObject response);` 变成 `void (QJsonObject response);`，
  类体解析在此崩坏；`InvokeContext::emit(...)` 退化为 `InvokeContext::(...)`（C2589）。
  后续关于 `QJsonObject` 的 “unknown” 报错都是这一次解析失败的**连锁反应**，不是头文件缺失。
- **解决或规避**：改名为 `deliver()`，并在头文件写下「不能命名为 emit」的注释；
  同时把 `respond/fail` 内部改为 `deliver(std::move(response))`。
- **影响与关联文档**：`src/plugin/Capability.{h,cpp}`；同类风险词还包括 `signals` / `slots` / `foreach`。

---
