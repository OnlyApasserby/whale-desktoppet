# 一次性姿态到期断言写错，误判状态机有 bug

> **原编号**：`TRAP-P2-004`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：测试设计 ｜ **影响**：1 条用例长期失败，浪费排查方向（怀疑状态机回落逻辑）

### 现象

`oneShotExpires` 断言「点击头部 → 姿态窗口到期后落回 `waiting`」：

```
FAIL!  : StateMachineTest::oneShotExpires()
   Actual   : "idle-cute"
   Expected : "waiting"
```

### 根因

**断言本身错了，不是状态机错了。** 窗口到期时距上次输入仅 `kCuriousWindowMs = 6s`，
而进入挂机态 `waiting` 需要 `kWaitingMs = 15s`（见 `PetStateMachine::contextPose()` 的优先级：
时段态 > 挂机态 > `idle`）。6s 时正确的返回值就是上下文空闲态 `idle-cute`。

### 解决

修正期望值，并**补充**一次推进到 `kWaitingMs` 的断言，把「两级回落」都钉住：

```cpp
// 到期回落上下文态：距上次输入仅 6s（< kWaitingMs），故为 idle-cute
QCOMPARE(..., QStringLiteral("idle-cute"));
// 距上次输入满 15s → waiting
QCOMPARE(..., QStringLiteral("waiting"));
```

### 影响与关联文档

- 教训：断言必须与**常量语义**对齐（`kCuriousWindowMs` ≠ `kWaitingMs`），
  断言失败先验证「期望值是否合理」，再怀疑实现。
- 关联：`docs/STATE-MACHINE.md`、`src/core/PetTypes.h`（挂机分级常量）。

---
