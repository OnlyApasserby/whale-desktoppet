# 状态机每 tick 重推缓存结果，下游「按字段判断」导致特效连播、台词狂换

> **原编号**：`TRAP-P2-009`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：表现/状态机契约 ｜ **影响**：目视验收第 4、6 项不通过（粒子迸发过于频繁、台词切换过于频繁）

### 现象

真人目视（Release 部署版）观测到：

- 三连击后星星/碎钻**持续迸发约 2s**（不是一次），夸夸的爱心持续约 6s；
- 气泡里的台词**每 200ms 换一句**，且同一句不会重复，看起来在「刷屏」；
- 连点不同部位时，只有**第一次**点击有台词（与「只显示最后一次操作的台词」相反）。

### 根因

`PetController::onTick()` 每 `kTickMs`（200ms）都会 `present()` 两次，
其中 `present(m_sm.current())` 推的是状态机的**缓存结果**，而缓存结果在一次性姿态存续期内
**始终携带同一个 `fx` 与 `lineKey`**：

1. `PosePresenter::present()` 只要 `result.fx != Fx::None` 就 `playFx()` → 特效按 tick 重放
   （三连击 `Fx::Particle` 存续 `kSuccessWindowMs`=2s ≈ 10 次 × 26 粒；夸夸 `Fx::Heart` 存续 6s ≈ 30 次 × 6 粒）。
2. 台词同理，且 `LineTable::pick()` 是**播放时**才随机取句，所以每次重放都会**换一句**。
3. `PetStateMachine::makeLine()` 的 6s 节流只在**生成 `lineKey` 那一刻**生效，对「缓存态重放」毫无约束 → 节流形同失效；
   同时该节流也作用于用户交互（`proactive=false`），于是连点只有第一次能拿到台词。

### 解决

把「新的一次表现」变成**显式语义**，而不是让下游从字段值去猜：

```cpp
struct PoseResult {
    ...
    std::uint32_t fxSerial = 0;    // 只在产生新特效时自增
    std::uint32_t lineSerial = 0;  // 只在产生新台词时自增
};
```

- 状态机侧：所有产出路径统一走 `compose()`；重复重放缓存态与回落（`fx=None`）时**不自增**；`reset()` 刻意不清零，保持单调。
- `PosePresenter` 侧：持有 `m_lastFxSerial` / `m_lastLineSerial`，只有序号变化才 `playFx()` / `startStream()`。
- 台词节流范围收窄为**只约束主动说话**（用户交互不再被吞），「连点不刷屏」改由「序号去重 + 流式打断」保证。

### 影响与关联文档

- 表现层另有 `kFxMinGapMs` = 500ms 强制间隔（**共用**、丢弃不排队），见 `PRESENTATION.md §2.1`。
- 台词改为流式输出且**打断式**（只保留最后一次），见 `PRESENTATION.md §4.1`。
- 契约变化：`STATE-MACHINE.md` §4/§5/§7、`TESTING.md` §2；单测新增 `serialsMarkOnlyNewOutcomes`、
  `speechThrottleAppliesToProactiveOnly`（原 `speechIsThrottled` 按新语义改写）、
  `fxSerialPlaysOnceAndRespectsGap`、`lineSerialDedupesAndStreamInterrupts`。

---
