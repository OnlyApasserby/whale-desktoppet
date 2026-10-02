# ROADMAP · P4 — 内容系统（成就 / 任务 / 签到 / 羁绊 / 日记）

> 未完成阶段，完成后重命名为 `ROADMAP-P4-Fin.md`。
>
> **状态说明（文档整理时按源码静态核对，2026-10-02）**：本文件的「任务清单」与「验收标准」
> 是**规划期**的原始文本，未随实施更新。实际交付情况以 `docs/README.md` §二.3 为准——
> 该处记录为「实现与自动化验证完成（`test_content`）；文件名为 `ROADMAP-P4.md`（暂不含 `Fin`）」。
>
> 静态可见的落地证据：
>
> | 交付物 | 代码 / 测试位置 |
> |---|---|
> | 39 项成就 | `src/core/Achievements.h`、`src/model/AchievementRepo.{h,cpp}`、`src/viewmodel/AchievementService.{h,cpp}` |
> | 每日任务（3 槽）/ 周签到 | `src/core/Quests.h`、`src/core/SigninRules.h`、`src/model/QuestRepo.*`、`src/model/SigninRepo.*`、`src/viewmodel/QuestService.*`、`src/viewmodel/SigninService.*` |
> | 成长日记（去重与上限） | `src/model/DiaryRepo.{h,cpp}` |
> | 内容面板（成就墙 / 任务 / 日记，内嵌设置面板） | `src/view/ContentPanel.{h,cpp}`、`src/view/SettingsDialog.cpp` |
> | 自动化验证 | `tests/test_content.cpp`（目标 `test_content`，已注册 CTest） |
>
> 是否补做人工验收并改签为 `ROADMAP-P4-Fin.md`，由项目 owner 决定；
> 本次文档整理**不代为判定验收通过**。

## 阶段目标

补齐 whale 养成系统的内容层，形成「可长期陪伴」的循环。

## 交付物

- `AchievementService`（39 项）+ 成就墙界面。
- `QuestService`（每日 3 槽）+ 任务界面。
- 周签到（7 格，1/3/7 里程碑）。
- 羁绊等级解锁（Lv3/5/7）。
- 成长日记（最多 80 条，同日同类去重）。

## 任务清单

1. 迁移 39 项成就定义与判定（互动/陪伴/养成/任务类；小游戏类预留）。
   *（**小游戏类 7 项已随「扫雷」由预留改为可解锁**，见 `src/core/Achievements.h` 与
   `MINIGAME-INTERFACE.md` §5；后续找小猫 / 国际象棋接入时沿用同一判定通道。）*
2. 每日任务：`day_key` 刷新、进度累加、领取幂等。
3. 周签到：`week_key` 记录、1/3/7 里程碑奖励。
4. 羁绊等级达标解锁（新待机动作 / 称号 / 彩蛋）。
5. 成长日记写入与展示（倒序最近 12 条，相对时间）。
6. 设置面板内嵌：成就墙 / 日常与养成（标签页）。
7. 单测：成就幂等、任务刷新与领取、签到里程碑、日记去重与上限。

## 验收标准

- [ ] 成就可按行为解锁并在成就墙正确高亮/灰显。
- [ ] 每日任务跨天自动刷新，领取不重复发奖。
- [ ] 周签到 1/3/7 里程碑奖励正确发放。
- [ ] 羁绊升级触发对应解锁与庆祝表现。
- [ ] 成长日记只记最近 80 条且同日同类只 1 条。

## 依赖

- P3（数据层、养成核心数值）。

## 完成标记

全部验收通过后 → `ROADMAP-P4-Fin.md`。
