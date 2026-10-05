# 成员函数取名 `slots()` 被 Qt 关键字宏展开 → C2059

> **原编号**：`TRAP-P8-007`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译 ｜ **影响**：`whalepet_view` 编译失败（改问答系统时引入，立刻被编译拦住）。

### 现象

```
src/core/PresetDialogue.h(68,36): error C2059: 语法错误:“)”
src/core/PresetDialogue.h(68,43): error C2238: 意外的标记位于“;”之前
```

对应源码为：

```cpp
std::vector<std::string> slots() const;   // ← 第 68 行
```

### 根因

Qt 把 `slots` 与 `signals` 定义为**关键字宏**（`qobjectdefs.h` 里的 `#define slots`）。
`PresetDialogue.h` 经 Qt 头文件之后被编译时，`slots` 被展开为空 token，
声明于是变成 `std::vector<std::string> () const;`（缺函数名）→ 语法错误。
同类保留名还有 `signals`、`emit`、`foreach`。

### 解决

改名为 `answerSlots()`（语义也更准确：返回该问题**已登记的回答槽位**），
并在头文件加注释说明「不可叫 `slots`」，避免后续再犯。
`PresetDialogue.cpp` 与 `core/DialogueOptions.cpp` 同步改名。

### 影响与关联文档

`src/core/PresetDialogue.{h,cpp}`、`src/core/DialogueOptions.cpp`；
`DIALOGUE.md` §2；这类命名冲突在纯逻辑头文件里尤其隐蔽（core 是「零 Qt 依赖」，
但被 Qt 侧编译单元包含时仍会吃到 Qt 的宏）。

---
