# 每日固定任务「今日签到」永不完成（签到从未上报 `Interaction::Signin`）

> **原编号**：`TRAP-P4-006`　**阶段**：P4　**来源**：原按阶段聚合的 `traps-P4.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：接线缺失 / 内容层 ｜ **影响**：签到记录（周签到板 / 连续天数）与每日任务不同步——
每天固定占 slot 0 的任务 `signin-1「今日签到」` 永远停在 **0/1「进行中」**，无法领取

### 现象（用户复现）

完成签到后：「状态 / 日常 / 设置」都能看到签到成功（周签到板点亮、连续天数 +1），
但每日任务列表里的「今日签到（0/1）」不推进、始终不能领取。

### 根因

任务池里有一条 `always` 任务：

```cpp
{"signin-1", "今日签到", "完成今天的签到", QuestMetric::Signin, 1, 5, 6, true}
```

`QuestService::interactionMatches()` 也已把 `core::Interaction::Signin → QuestMetric::Signin` 映射好，
但**生产代码里没有任何地方上报过 `Interaction::Signin`**：

- `PetController::applyGrowthForZone/applyGrowthForEvent` 只映射 摸头/肚子/尾巴/戳/投喂/夸夸/三连击；
- 签到走的是 `PetWindow::handleSignIn()` → `GrowthService::signIn()` + `SigninService::markToday()`
  这条**独立链路**，完全不经过 `PetController::interactionOccurred` 交互总线。

`tests/test_content.cpp::questProgressAndClaimIsIdempotent` 是**直接**调用
`quest.reportInteraction(Interaction::Signin, base)` 才通过的，因而掩盖了「组合根未接线」这一缺口——
服务层有单测、端到端却没人喂数据。

### 解决

把签到接入交互总线（保持「唯一上报口径」）：新增 `PetController::reportSignIn()`，
**只广播不施加养成增量**（签到的心情/好感已由 `GrowthService::signIn()` 落定，重复施加会双倍加心情）：

```cpp
void PetController::reportSignIn() { emit interactionOccurred(core::Interaction::Signin, nowMs()); }
```

`PetWindow::handleSignIn()` 在 `m_growth->signIn()` 成功（= 今天真的签到了）后调用一次。
随后既有接线自动完成其余动作：`QuestService` 推进 `signin-1` → 发 `questDone` →
`AchievementService::reportQuestCompleted()` → `slotsChanged` → 内容面板刷新。

**连带去重**：`signin-1` 一经上报即「完成」，`QuestService` 原本会按任务再写一条日记
（`kind = "quest"`），而签到本身已写了一条（`kind = "signin"`）；日记 UI 只展示 `detail`，
于是同一次签到会出现两条「今日签到」。因「签到任务的完成 = 签到本身」，已让
`QuestService` 对 `QuestMetric::Signin` 不再重复记日记（其余任务照旧）。

### 验证状态

- **已验证**：`tests/test_smoke.cpp` 新增 `signInInteractionReportsWallClock`——断言
  `reportSignIn()` 广播一次 `Interaction::Signin`，且时间戳为墙钟（同时守住 `TRAP-P4-004`）；
  `tests/test_content.cpp::questProgressAndClaimIsIdempotent` 增断言：完成 `signin-1` 后
  日记条数为 **0**（签到任务不重复记日记）。Debug / Release 各 **9/9 通过**。
- 端到端（签到后任务变「可领取」）建议在真实桌面点一次签到目视确认。

### 影响与关联文档

- `GAMEPLAY.md §5`（每日任务含固定「今日签到」）、`Quests.h`（`always` 槽）、`QuestService`。
- 教训：**服务层单测通过 ≠ 端到端接线完成**。凡是「某交互 → 某服务」的映射，
  都要在组合根（`PetWindow`）确认该交互确实被广播；新增交互枚举值时尤其要检查上报点。
  另：签到这类「有独立服务链路」的交互，勿让 `applyGrowthForEvent` 再走一遍养成增量。
