# 「状态 / 日常 / 设置」三处签到状态不同步

> **原编号**：`TRAP-P4-005`　**阶段**：P4　**来源**：原按阶段聚合的 `traps-P4.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：状态同步 / UI ｜ **影响**：在任一处签到后，其余面板仍显示「今日签到（可点）」，状态各说各话

### 现象（用户复现）

「状态」面板、「日常」面板（右键菜单打开）、「设置 → 日常」内嵌页三处都有「今日签到」按钮，
但在一处签到后，另外两处**不刷新**，仍显示未签到。

### 根因

1. **设置内嵌页是另一个实例**：`PetWindow::syncContentPanel()` 只刷新独立窗口的
   `m_contentPanel`；`SettingsDialog` 内嵌的 `ContentPanel`（`m_content`）是**另一个对象**，
   未被刷新。
2. **状态面板按钮从不更新**：`StatusPanel::updateFrom()` 只写标签，从不改 `m_signInButton`
   的文案 / `enabled`，因此签到后仍显示「今日签到」。

### 解决

- `SettingsDialog` 暴露 `refreshContent()`（转调内嵌 `ContentPanel::refreshAll()`）；
  `PetWindow::syncContentPanel()` 同时刷新独立面板与设置内嵌面板。
- `StatusPanel` 新增 `setTodaySigned(bool)`（置灰 + 文案切换）；
  `PetWindow::syncStatusPanel()` 以 `SigninService::isTodaySigned()` 为统一口径喂入。
- 追加接线：`SigninService::boardChanged → PetWindow::syncStatusPanel`，
  使周签到板变化能实时驱动状态面板按钮。

### 验证状态

- **已验证**：Debug / Release 均可构建；`ctest` 各 **9/9 通过**（`test_content` / `test_settings` 覆盖相关链路）。
- 状态面板按钮文案/置灰、三处实时联动属人工目视项。

### 影响与关联文档

- `SETTINGS.md §2`（设置面板内嵌日常页）、`GAMEPLAY.md §4`（签到）、`PRESENTATION.md §3`（状态面板）。
- 教训：**同一逻辑面板被复用到多个宿主时，每个宿主实例都要纳入刷新链路**；
  只读展示控件若承载「状态按钮」，也必须随状态变化更新，不能只在构造时定型。

---
