# `signals` 是 Qt 关键字宏，用作变量名导致「语法错误: public」

> **原编号**：`TRAP-P7-009`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象（可复现步骤 / 报错原文）**：`tests/test_acp_event_mapper.cpp` 编译失败，报错全部指向
  **无关位置**——`for` / `if` 语句处，且报的是关键字 `public`。复现：`cmake --build build --config Debug --target test_acp_event_mapper`。
  报错原文（逐字，节选）：
  ```
  tests\test_acp_event_mapper.cpp(49,23): error C2059: 语法错误:“public”
  tests\test_acp_event_mapper.cpp(57,13): error C2059: 语法错误:“public”
  tests\test_acp_event_mapper.cpp(63,46): error C2143: 语法错误: 缺少“)”(在“public”的前面)
  tests\test_acp_event_mapper.cpp(64,1): error C2447: “{”: 缺少函数标题(是否是老式的形式表?)
  ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug，
  目标 `build\Debug\test_acp_event_mapper.exe`。
- **根因**：Qt 为「信号 / 槽」定义了**关键字宏**（`qobjectdefs.h`）：
  `signals` → `public`、`slots` → 空、`emit` → 空。
  测试里把 `QList<CoreSignal> signals;` 用作**变量名**，预处理后变成 `QList<CoreSignal> public;`，
  于是 `for (const CoreSignal &signal : signals)` 展开为 `... : public)` ——
  编译器在范围 for 处看到 `public`，报「语法错误: public」，而行号指向**使用点**而非定义点，极难定位。
  （首轮误判为 `QVector` 模板问题，改为 `QList` + 输出参数后错误**完全不变**，正是本陷阱的特征。）
- **解决或规避**：把该变量改名为 `mapped`（避开全部 Qt 关键字宏）。修复后 Debug / Release
  `ctest` 各 **21/21 通过**。
- **影响与关联文档**：`tests/test_acp_event_mapper.cpp`。
  **回灌规则**：Qt 项目中**禁止**把 `signals` / `slots` / `emit` / `foreach` 等 Qt 关键字宏
  用作标识符；遇到「语法错误: public」且指向 `for` / `if` 时，**优先检查标识符是否撞宏**。

---
