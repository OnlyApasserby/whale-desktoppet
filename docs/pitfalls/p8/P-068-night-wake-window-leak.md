# 深夜「唤醒窗口」跨时段泄漏 + 跨时段立绘延迟

> **原编号**：`TRAP-P8-010`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：行为 / 优先级（需求二次修订） ｜ **影响**：需求 R2「深夜常驻 `daily-pajama`」在**跨时段**场景下失效，或延迟最多约 60s 才切换。

### 现象

- 22:5x 与角色交互（点击 / 拖拽 / 喂食 / 关键词命中等**任一** `touchInput` 路径）后跨入 23:00，
  角色仍显 `night`（傍晚立绘）—— 深夜常驻立绘不生效，直到 60s 唤醒窗口到期；
- 反向同样成立：22:5x 的一次性姿态（如 `react-head`）跨入 23:00 后仍继续占位，切换被一并延迟；
- 时段边界（23:00 整）本应立刻切换，实际要等后续 `Tick` 才纠正。

### 根因

原实现把「深夜唤醒」做成 `m_lateNightAwakeUntilMs = nowMs + kLateNightAwakeMs` 的**绝对时刻窗口**，
且该窗口**不随时段变化而失效**：

```cpp
case EventType::Clock: {
    if (event.hour >= 0) { m_hour = event.hour; }
    if (m_oneShotUntilMs == 0 && !m_dragging) { m_current = fallback(event.nowMs); }
    return m_current;      // 跨时段：不清窗口、不清一次性姿态、不立刻重算
}
```

- `lateNightAwake()` 只判「当前时刻 < 窗口绝对时刻」且「当前是深夜」，**不区分窗口是在哪个时段建立的** →
  22:59 建立、覆盖到 23:00 之后的窗口在深夜成立，立绘被判为 `night`；
- `Clock` 分支仅在 `m_oneShotUntilMs == 0` 时重算立绘，跨时段**不清一次性姿态** → 继续沿用上一时段姿态；
- 无「跨时段」显式分支，刷新完全依赖下一次 `Tick`，故最长延迟约一个 `kLateNightAwakeMs`（60s）。

**为何测试没拦住**：既有用例只在**同一时段内**验证唤醒与到期（`daySlotsAndLateNightWake`），
没有任何「在时段 A 交互后**立刻跨到时段 B**」的断言 → 交叉处零覆盖。

### 解决

按需求二次修订**移除深夜唤醒态**（根因消除），并新增跨时段立即刷新：

- 删除 `kLateNightAwakeMs` / `kLateNightAwakePose` / `lateNightAwake()` / `m_lateNightAwakeUntilMs`；
  `touchInput()` 只刷新「最后输入时刻」，不再影响深夜立绘 → **泄漏窗口不复存在**；
- 深夜常驻立绘恒为 `daily-pajama`；点击**不换立绘**（仅回应台词并累计计数，满 `kLateNightWeakClickCount`
  次 → `meme-smile-pain` 虚弱 20s）；
- `Clock` 分支新增跨时段判断：`daySlotOf(m_hour)` 与上一时段不同时**当帧**清残留
  （离开深夜 → 清零计数 / 虚弱窗口；非拖拽时清一次性姿态）并按新时段立即重算立绘。

回归用例（修复前必然失败）：

- `test_state_machine::daySlotsAndLateNightPersistent`：22:59 `click`（生成 `react-head`，旧实现还泄漏出 `night`）后，
  跨 23:00 的 `clock` **当帧**返回 `daily-pajama`，随后 `tick` 仍为 `daily-pajama`；深夜一轮 9 次点击恒为 `daily-pajama`。
- `test_state_machine::lateNightClicksTriggerWeakPose`：前 9 次点击立绘不变（`daily-pajama`）+ 回应 `click.*` 台词；
  第 10 次 → `meme-smile-pain`（ttl 20s）；20s 到期**直接**回 `daily-pajama`。

### 影响与关联文档

`src/core/PetStateMachine.{h,cpp}`、`src/core/DaySlotRules.h`、`src/core/IdleRules.h`、
`src/viewmodel/PetController.cpp`、`tests/test_state_machine.cpp`、`tests/test_preset_dialogue.cpp`；
`docs/STATE-MACHINE.md` §1 / §1.2 / §2 / §3 / §5、`docs/ROADMAP-P8.md` §P8.1 / §2、
`docs/POSE-ASSETS.md` 附录 A、`docs/README.md`。
