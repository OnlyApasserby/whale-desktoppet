# 满值常驻位于时段态之前 → 深夜 / 傍晚立绘永不显示

> **原编号**：`TRAP-P8-008`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：行为 / 优先级（代码审查 + 运行时数据发现） ｜ **影响**：需求 R2「深夜 23:00–06:59 空闲常驻 `daily-pajama`」在养成满值时**完全失效**（傍晚 `night` 同样失效）。

### 现象

用户报告「23:00–7:00 的立绘变更未生效」。运行时表现为：无论几点，立绘恒为 `tail-swing`（摇尾巴）。

### 根因

新增的「满值特殊常驻」（2026-10-04 立绘激活批次）被插在 `PetStateMachine::contextPose()` 的**时段态之前**：

```cpp
// 1.5) 满值特殊常驻
if (m_mood >= kMoodMax && m_satiety >= kSatietyMax) {
    return kVitalsFullPose;   // 无条件 return，且没有像 1.6 睡眠循环那样排除深夜
}
...
// 2) 时段态（傍晚 night / 深夜 daily-pajama）—— 永远到不了
```

同一批次插入的 `1.6` 睡眠循环写了 `idleSlot != DaySlot::LateNight` 主动给深夜让路，
`1.5` 漏了同样的判断，于是「心情 & 饱腹同时满值」时时段态被整体遮蔽。
本机 `whalepet.db` 佐证：4 个库中 3 个 `satiety = 100`，其中 `deploy-release/data` 为
`mood = 100 & satiety = 100`；且 `m_satietyAccumMs` 不落库、每次重启清零，
开发期反复启停会让 satiety 长期不掉点，满值窗口被拉长。

**为何测试没拦住**：`test_state_machine::vitalsFullShowsTailSwing` 只在默认小时（12，日间）验证；
`daySlotsAndLateNightPersistent`（原名 `daySlotsAndLateNightWake`，见 TRAP-P8-010）从不调用
`setVitals`（默认 0/0）。两个特性的**交叉处零覆盖**，故 35/35 全绿。

### 解决

按方案 A 把「满值常驻」整块**下移到时段态之后**（`contextPose()` 第 4 档）：

```
工作态 > 睡眠循环 > 时段态（傍晚/深夜）> 满值常驻 > 挂机态 > 游戏陪玩态 > 静息态
```

同时把深夜做成**独立阶段**（不参与任何随机立绘池：待机小剧场 / 睡眠循环 / 逗弄 / 满值），
只保留点击反馈，并新增「深夜点击累计 ≥ 10 次 → `meme-smile-pain` 虚弱 20s + `click.latenight.weak` 台词」。

回归用例（修复前必然失败）：

- `test_state_machine::vitalsFullYieldsToTimeSlots`：满值 + 12 点 → `tail-swing`；18 点 → `night`；23 点 → `daily-pajama`；回到日间 → `tail-swing`。
- `test_state_machine::lateNightIsIndependentStage`：深夜连续 tick 不出现 `teasing`、待机 20min+ 不出 `sleep`、满值不出 `tail-swing`；离开深夜后睡眠循环立即恢复。
- `test_state_machine::lateNightClicksTriggerWeakPose`：第 10 次深夜点击 → `meme-smile-pain`（ttl 20s + 虚弱台词）；计数清零后需再满 10 次。
  （**注**：该用例随 TRAP-P8-010 的需求二次修订一并更新 —— 前 9 次点击立绘**不再切换**、虚弱 20s 到期**直接**回 `daily-pajama`；原文描述的「20s 后回 `night`、唤醒窗口到期回 `daily-pajama`」已随唤醒态移除而作废。）

### 影响与关联文档

`src/core/PetStateMachine.{h,cpp}`、`src/core/IdleRules.h`、`assets/lines/lines.txt`、
`tests/test_state_machine.cpp`；`docs/STATE-MACHINE.md` §1 / §1.2 / §2 / §3 / §3.1 / §4、
`docs/POSE-ASSETS.md` 附录 A（2026-10-04 增量修订）。

---
