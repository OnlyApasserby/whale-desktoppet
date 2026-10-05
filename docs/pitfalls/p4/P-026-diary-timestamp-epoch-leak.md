# 成长日记时间戳以 1970 起算（elapsed 时钟泄漏到内容层）

> **原编号**：`TRAP-P4-004`　**阶段**：P4　**来源**：原按阶段聚合的 `traps-P4.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：时间基准 / 数据 ｜ **影响**：成长日记里所有**由交互触发**的条目（任务完成 / 成就解锁）
显示为 `1970-01-01`；同时污染每日任务与周签到的「今天」判定

### 现象（用户复现）

成长日记中的条目时间从 **Unix 时间戳 0**（1970-01-01）起算，而不是系统当前时间。
`ContentPanel::formatRelative()` 对 `now - ts`（≈1.7e12ms）落入 `>30 天` 分支，
直接打印 `1970-01-01`。

### 根因

`PetController` 用 `QElapsedTimer m_clock` 做时钟，`nowMs()` 返回的是**进程启动起算**的毫秒数：

```cpp
qint64 nowMs() const { return m_clock.elapsed(); }   // ← 不是 Unix 墙钟
```

该值经 `interactionOccurred(type, nowMs())` 广播，组合根 `PetWindow` 原样喂给三个内容层 Service
（`AchievementService` / `QuestService` / `SigninService`）。这三个 Service 的
`nowOrCurrent()` 只判断 `nowMs > 0` 就采信，于是把「启动后几百毫秒」当成时间戳写入
`bond_diary.ts_ms`，并据此算出 `dayKey == "1970-1-1"`。

**连带影响**：`QuestService::refreshForToday` / `SigninService::syncWeek` 也拿到该 elapsed 值，
与启动时用墙钟建立的 `m_dayKey` 不一致 → 每次交互都误判「跨天」，重建任务槽位、重置周签到板。

对照：凡是以默认参数 `nowMs = 0` 调用的路径（签到 `markToday()`、账目 `settle()` 等）
拿到的是正确墙钟，所以**同一条日记里时间戳是混用的**（签到类正确、交互类为 1970）。

### 解决

把 `PetController` 的时间基准统一为**系统墙钟**（Unix 毫秒）：

```cpp
qint64 PetController::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}
```

`PetStateMachine` 内部只使用事件的**时间差**（`now - lastInput`、`now + ttl`），
与绝对基准无关，故切换后表现逻辑不受影响（已由 `test_state_machine` / `test_smoke` 回归确认）。

### 影响与关联文档

- `GAMEPLAY.md §6`（成长日记）、`DATA-MODEL.md`（`bond_diary.ts_ms` 语义 = Unix 毫秒）。
- 教训：**跨层传递的「时间」必须显式约定基准**（墙钟 ms vs 单调 elapsed ms）；
  `nowOrCurrent()` 这类「>0 就用」的兜底无法识别基准错误。状态机/动效可用单调时钟，
  但只要要落库/展示，就必须是墙钟。

---
