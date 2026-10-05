# `kCodingPose` 定义被误删 → C2065

> **原编号**：`TRAP-P8-001`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译 ｜ **影响**：`whalepet_core` 编译失败，全部下游目标阻塞。

### 现象

```
src/core/PetStateMachine.cpp(87,20): error C2065: “kCodingPose”: 未声明的标识符
```

复现：`cmake --build build --config Debug --target test_preset_dialogue`。

### 根因

`WorkPosePool.h` 里原本同时有 `constexpr const char *kCodingPose = "running";` 与
`bool workStateIsCoding(...)` 声明；在一次局部替换中把常量定义连同注释一起替换掉了，
只剩下函数声明 —— 而 `PetStateMachine.cpp` 仍在引用该常量。

### 解决

把 `kCodingPose` 常量定义放回 `WorkPosePool.h`（与 `workStateIsCoding` 相邻，语义成组）。

### 影响与关联文档

`src/core/WorkPosePool.h`、`src/core/PetStateMachine.cpp`；见 `ROADMAP-P8.md` P8.2。

---
