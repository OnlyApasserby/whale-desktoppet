# `static_cast<long long>` 转换 NaN/±Inf 是未定义行为

> **原编号**：`TRAP-P7-020`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：让假内存里某 `float` 字段读到 `quiet_NaN()`，
  `readField()` 走到 `value.integer = static_cast<long long>(raw);`——
  MSVC 报 C4244（`'float': conversion from 'double' to 'long long'`，possible loss of data），
  而按 C++ 标准，浮点转整数在**值域外或 NaN** 时是**未定义行为**
  （x86 上通常得到 `0x8000000000000000`）。
- **环境**：Qt 6.8.4 + MSVC（VS 18 2026）+ CMake 4.4.2；
  `src/gamestate/PointerChainResolver.cpp` 的 `GameFieldKind::Float` / `Double` 分支。
- **根因**：只判了「读取是否成功」，没判**读到的数值是否可表示为整数**。
  游戏内存里的字段值来自目标进程，**不可信**。
- **解决或规避**：新增 `toInteger(raw, out, error, what)`，先判
  `std::isnan` / `std::isinf`，再判是否落在 `(-2^63, 2^63)` 开区间内，
  **不合格即明确失败**（不写半成品结果）。正常负小数（如 `-1234.75`）
  仍能正确截断为 `-1234`，有正例覆盖。
- **影响与关联文档**：`src/gamestate/PointerChainResolver.cpp`；
  `tests/test_gamestate_boundaries.cpp` 的 `resolverRejectsNonFiniteAndOutOfRangeFloats`。

---
