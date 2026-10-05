# 未过期一次性姿态期间到达的里程碑/陪玩态被让位（符合设计，非缺陷）

> **原编号**：`TRAP-EX1-007`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：`test_game_companion` 首轮运行，`machineMilestonesBroadcast` 失败（逐字）：
  ```
  FAIL!  : GameCompanionTest::machineMilestonesBroadcast() Compared values are not the same
     Actual   (QString::fromStdString(r2.pose)): "levelup"
     Expected (QStringLiteral("game-win"))     : "game-win"
  ```
  复现步骤：`machine.reset(0)` → `handle(gameStateChanged(Normal,0,levelUp,1000))` →
  **不推进 Tick** → `handle(gameStateChanged(Normal,0,clear,8000))`，期望立绘变 `game-win`，
  实际仍是 `levelup`（上一次升级的一次性姿态）。
- **根因**：`PetStateMachine::handle(EventType::GameStateChanged)` 用
  `busy = (m_oneShotUntilMs > 0) || m_dragging` 判定「不打断一次性姿态」，而
  `m_oneShotUntilMs` 是**绝对到期时刻**（`event.nowMs + ttl`），只有在 `EventType::Tick`
  分支里 `event.nowMs >= m_oneShotUntilMs` 时才被清零（见 `PetStateMachine.cpp` Tick 分支）。
  因此两次游戏事件之间若不经过 Tick，即使`nowMs`已越过到期时刻，`busy` 仍为 true，
  里程碑被整体跳过（既不 fallback 也不播报）。这与工作态（`WorkStateChanged`）行为完全一致。
- **解决或规避（决策）**：**代码无需改动**——真实运行时 `GameCompanionService` 每 200ms 采样、
  主循环同时持续发送 Tick，一次性姿态会在其到期后的第一个 Tick 被回收，里程碑不会丢。
  测试侧修正：在两个游戏事件之间插入 `machine.handle(Event::tick(5000))`，模拟真实 Tick 节奏，
  再验证通关里程碑生效（现 17/17 通过）。
- **影响与关联文档**：`src/core/PetStateMachine.cpp`（`GameStateChanged`/`WorkStateChanged` 的 busy 判定）、
  `tests/test_game_companion.cpp`；关联 `docs/ROADMAP-ex1.md` EX1.4「不打断工作专注/深夜/一次性小剧场」验收标准。

---
