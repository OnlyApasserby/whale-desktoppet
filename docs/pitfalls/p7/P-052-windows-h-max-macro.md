# `windows.h` 的 `max` 宏污染 `std::numeric_limits<T>::max()`

> **原编号**：`TRAP-P7-014`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤）**：单测里写 `std::numeric_limits<std::uint64_t>::max()`，
  编译报错（逐字）：
  ```
  tests\test_gamestate_boundaries.cpp(1419,68): error C2589: “(”:“::”右边的非法标记
  tests\test_gamestate_boundaries.cpp(1419,68): error C2059: 语法错误:“)”
  ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，Debug。
- **根因**：`windows.h` 定义了对象式宏 `#define max(a,b) …`；预处理器把
  `::max()` 里的 `max` 也替换掉，展开成 `::(((a) > (b)) ? …)`，于是 `::` 后面
  成了非法标记。与 TRAP-P7-009（`signals` 宏）**同源**：Windows 头里的宏会污染
  看起来像标准库标识符的名字。
- **解决或规避**：写成 `(std::numeric_limits<std::uint64_t>::max)()`——
  多一层括号让预处理器认出它不是函数式宏调用。同类名字（`min`/`near`/`far`/
  `interface`/`small`）一律同样处理。
- **影响与关联文档**：`tests/test_gamestate_boundaries.cpp`；与 TRAP-P7-009 同类。
