# 编程时立绘闪一下 `work-ram`（播报与常驻立绘不同源）

> **原编号**：`TRAP-P8-004`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：行为（测试暴露） ｜ **影响**：需求 R3「编程时常驻 running」在**状态切换的那一帧**被破坏。

### 现象

`test_work_state::machineDrivesPoseAndSilencesProactive` 失败：

```
FAIL!  : 实际 "work-ram" ≠ 期望 "running"   （Event::workStateChanged(Coding) 的返回值）
```

复现：`ctest -C Debug -R test_work_state`。

### 根因

`contextPose()` 已按 P8 规则把编程族映射为 `running`，但 `EventType::WorkStateChanged` 分支
在**播报**时仍调用旧的 `workStatePose(next)`，随后 `applyOneShot()` 把该值写进 `m_current` ——
于是"状态变化的那一次 return"返回 `work-ram`，下一个 tick 才回到 `running`（肉眼即"闪一下"）。

### 解决

抽出 `PetStateMachine::contextWorkPose()`（编程族 → `running`；busy 非编程族 → 池当前张；
其余按 `workStatePose`；`Idle`/`Unknown` → `nullptr` 交回下层），
`contextPose()` 与 `WorkStateChanged` 播报**共用同一口径**，消除两处规则漂移。

### 影响与关联文档

`src/core/PetStateMachine.{h,cpp}`；`ROADMAP-P8.md` A4；`STATE-MACHINE.md` §1.3。

---
