# `Event::keyword` 工厂函数与同名字段冲突（C2365）

> **原编号**：`TRAP-P2-001`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译 ｜ **影响**：`whalepet_core` 无法编译，P2 起步即卡住

### 现象

`PetTypes.h` 中 `Event` 既有数据成员 `std::string keyword;`，又提供了静态工厂：

```cpp
static Event keyword(std::string kw, qint64 nowMs);   // 与成员同名
```

MSVC 报：

```
error C2365: "whalepet::core::Event::keyword": 重定义；以前的定义是"数据成员"
```

### 根因

C++ 中同一作用域内**数据成员与成员函数不能同名**；`keyword` 作为成员名已被占用。

### 解决

工厂函数改名 `keywordHit`（语义也更清晰：「关键词命中」），并同步修改调用点与测试：

```cpp
static Event keywordHit(const std::string &kw, qint64 nowMs);
```

### 影响与关联文档

- 约定：`Event` 工厂命名一律用**动词/事件名**（`tick/click/…/keywordHit`），避免与字段名撞车。
- 关联：`docs/STATE-MACHINE.md`（事件清单）、`tests/test_state_machine.cpp`。

---
