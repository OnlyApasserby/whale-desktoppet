# `QRegularExpression` 分段原始字符串——`(?:` 首字符被当作分隔符吞掉

> **原编号**：`TRAP-EX1-003`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`test_unity_adapters` 4 个用例失败，报「dump.cs 中未识别到任何带偏移（// 0x…）的字段」；
  逐字：
  ```
  FAIL!  : UnityAdaptersTest::parseFieldOffsetsExtractsByClassAndName()
    ...returned FALSE. (dump.cs 中未识别到任何带偏移（// 0x…）的字段)
  ```
  可复现：`.\build\Debug\test_unity_adapters.exe -o result.txt,txt`。
- **根因**：`UnityDumpConverter.cpp` 的 `classRe()` 把正则拆成多段 `QStringLiteral(R"(…)" R"(…)" R"(…))`。
  C++ 原始字符串 `R"(…)"` 的**首个 `(` 是分隔符的一部分**、不属于内容；当我写
  `R"(?:class|…)"` 想表达内容 `(?:class|…` 时，实际内容变成 `?:class|…`，**丢了一个 `(`**。
  多段拼起来后正则括号不平衡 → `QRegularExpression` 无效 → `match()` 恒不命中，
  而 `haveClass` 始终为 false，字段被整体跳过，最终只报「未识别到字段」（掩盖了真因）。
- **解决或规避**：
  1) 把 `classRe()` 合并为**单一**原始字符串（内容以 `^\s*` 开头，不触碰分隔符歧义）；
  2) 在 `parseFieldOffsets()` 入口加 `namespaceRe()/classRe()/fieldRe().isValid()` 防御检查，
     正则无效时直接返回「内部解析正则无效（实现缺陷）」，把静默失配变成显式错误。
- **影响与关联文档**：`src/gamestate/UnityDumpConverter.cpp`；关联 `docs/ROADMAP-ex1.md` EX1.2、
  `docs/UNITY-SOP.md` §2。
