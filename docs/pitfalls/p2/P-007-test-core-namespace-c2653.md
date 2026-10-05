# 测试里 `core::` 无法解析（C2653）

> **原编号**：`TRAP-P2-002`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译 ｜ **影响**：`test_state_machine` / `test_line_table` 无法编译

### 现象

测试文件里用 `using namespace whalepet;` 后写 `core::PetStateMachine`，报：

```
error C2653: "core": 不是类或命名空间名称
```

### 根因

`using namespace` 只把**命名空间的成员**引入当前作用域，**不会**引入命名空间本身的名字。
因此 `core` 这个限定名依然不可见。

### 解决

显式加命名空间别名（README 允许，且比逐个 `using` 更不易污染）：

```cpp
namespace core = whalepet::core;
```

### 影响与关联文档

- 所有 P2+ 新增测试统一用命名空间别名，禁止依赖 `using namespace` 带来的间接可见性。
- 关联：`docs/TESTING.md` §3。

---
