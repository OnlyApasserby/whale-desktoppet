# `slots` 与 Qt 关键字宏同名，`QObject` 相关头全部编译失败

> **原编号**：`TRAP-P4-002`　**阶段**：P4　**来源**：原按阶段聚合的 `traps-P4.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译错误 ｜ **影响**：`whalepet_view` 编译中断，代码「看起来完全正确」

### 现象

`cmake --build build --config Debug` 报错原文（节选）：

```
src/model/QuestRepo.cpp(55,27): error C2530: "s": 必须初始化引用
src/model/QuestRepo.cpp(55,29): error C2143: 语法错误: 缺少";"(在":"的前面)
src/model/QuestRepo.cpp(55,36): error C2143: 语法错误: 缺少";"(在")"的前面)
```

对应的源码是**完全合法**的 range-for：

```cpp
for (const QuestSlot &s : slots) {
```

### 根因

`QuestRepo.cpp` 经由 `model/Database.h` 间接引入了 `<QObject>`，而 Qt 在
`qobjectdefs.h` 中把 `slots` 定义成了**空宏**（不启用 `QT_NO_KEYWORDS` 时）：

```cpp
# define slots      // → 展开为空
```

于是预处理后这一行变成 `for (const QuestSlot &s : ) {`——
编译器看到的是一个「声明了引用却没初始化」的语句，才报出 C2530/C2143 这种**指向含义完全无关的**错误。

函数参数名 `slots` 在**声明处**（`const QList<QuestSlot> &slots`）不会报错
（未命名形参合法），所以错误只在 `.cpp` 的 `for` 处炸开，更增迷惑性。

### 解决

模块内不再使用裸标识符 `slots`（`slot`、`m_slots` 不是宏，可继续用）：

- `QuestRepo::replaceAll(const QList<QuestSlot> &newSlots)`
- `QuestService::slotList()`（原 `slots()`；成员仍是 `m_slots`）
- `ContentPanel::refreshDaily()` 内局部变量 → `questSlots`

并在 `QuestService.h` 的访问器上方写明原因，避免后续「顺手改回 `slots()`」。

### 影响与关联文档

- `ARCHITECTURE.md` 命名约定；同类的 Qt 宏还有 `signals` / `emit` / `foreach`，**不得**用作标识符。
- 排查技巧：报错位置语义明显不通（如「引用未初始化」出现在 `for (x : y)`）时，
  优先怀疑**标识符被宏替换**，可用 `cl /P` 或 IDE 的「预处理后」视图确认。

---
