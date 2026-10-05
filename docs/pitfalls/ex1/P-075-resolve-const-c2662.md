# `resolve()` 为 const 却调用非 const 辅助方法（MSVC C2662）

> **原编号**：`TRAP-EX1-002`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：编译 `whalepet_gamestate` 时报错（逐字）：
  ```
  PointerChainResolver.cpp(81,14): error C2662: “bool whalepet::gamestate::PointerChainResolver::readPointer(uint64_t,uint64_t *,QString *)”:
    不能将“this”指针从“const whalepet::gamestate::PointerChainResolver”转换为“whalepet::gamestate::PointerChainResolver &”
  ```
  可复现：`cmake --build build --config Debug --target whalepet_gamestate`。
- **根因**：`PointerChainResolver::resolve()` 按接口契约（§六 6.2）声明为 `const`，其内部经
  `readPointer()` → `readRaw()` 累加「本轮字节预算」`m_bytesThisRound`，故辅助方法只能是非 const；
  const 成员函数不能把 `this` 传给需要非 const 的方法 → C2662。
- **解决或规避**：将「本轮字节预算」`m_bytesThisRound` 声明为 `mutable`，并把私有辅助
  `readRaw()` / `readPointer()` 改为 `const`。因该计数是「读取开销预算」这一逻辑上的观察量，
  不影响对象可观察语义，故用 `mutable` 是恰当的最小改动（未放宽接口的 const 契约）。
- **影响与关联文档**：`src/gamestate/PointerChainResolver.h` / `.cpp`；关联 `docs/ROADMAP-ex1.md`
  §2.6.2.1、§4.3、§六 6.2。
