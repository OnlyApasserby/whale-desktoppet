# ROADMAP · P5 — 梗聊天

> ✅ 代码与单测已落地并已改签为 `ROADMAP-P5-Fin.md`（自动化验证完成，Debug / Release 各 7/7）；
> **人工目视项**见本文件「验收标准」，待用户复验（复验不影响本阶段已完成的判定）。

## 阶段目标

接入 whale 台词库与关键词表情感知，实现「陪伴式」闲聊（本项目保留的唯一延伸功能）。

## 交付物

- `ChatService` + `LineTable`（外部台词语料）。
- 分时问候 / 心情分层 / 羁绊专属台词。
- **21** 关键词 → `meme-*`/`work-*`/`abstract`/`bold` 表情感知。
- 说话节流与「不打断」规则。

## 任务清单

1. ✅ 迁移 whale `LINES` 台词库到 `assets/lines/`（`lines.txt` / `greet.txt` / `bond.txt` / `meme.txt`）。
2. ✅ `LineTable` 解析与场景查询；`PosePresenter::loadBundledLines` 多文件加载 + 缺失优雅降级（空表 + 日志）。
3. ✅ `ChatService`：场景决策 + 跨档去重；候选选取与最近 N 条去重由 `LineTable::pick` 承担；
   ≥6s 节流与深夜静默由 `PetStateMachine`（`proactive` 台词）统一把关。
4. ✅ 分时问候（`greet.morning/forenoon/noon/afternoon/evening`；23:00–05:59 静默；同时段只问候一次）。
5. ✅ 心情分层选词（`<40`→`bond.low-mood`；`≥70`→`bond.high-mood`；仅跨档播报）。
6. ✅ 羁绊 Lv3/5/7 专属台词（`bond.l3` / `bond.l5` / `bond.l7`；仅跨档播报）。
7. ✅ 关键词表情感知：`keyword_aware` 开关（**默认关**），命中查表切立绘（21 项，非拼接）。
8. ✅ 单测 `tests/test_chat.cpp`：分时/深夜、心情、羁绊、关键词映射与开关、节流与序号、语料覆盖、立绘存在性。

## 验收标准

- [ ] 待机/互动时气泡台词自然出现且不重复、不刷屏。**（需人工目视）**
- [x] 分时问候与深夜静默生效。（`test_chat::greetByHourAndNightSilence`）
- [ ] 关键词命中时立绘切换为对应表情（开关开启时）。**（逻辑与映射已单测；目视待验）**
- [x] 台词语料可外部替换且不影响主程序。（`test_chat::missingCorpusDegradesToSilence`）
- [x] ChatService 单测通过。（`ctest -C Debug/Release` 均 7/7）

## 依赖

- P2（气泡与状态机）、P3/P4（心情、羁绊数值）。

## 验证记录

- `cmake --build build --config Debug` / `--config Release` 均成功。
- `ctest --test-dir build -C Debug` → **7/7 passed**；`-C Release` → **7/7 passed**
  （Release 侧需按 `BUILD.md` 部署 Qt Release DLL，或用开发期临时 PATH 方式运行）。
- 踩坑记录：`docs/pitfalls/`（`TRAP-P5-001` 立绘映射拼接、`TRAP-P5-002` 测试漏检、`TRAP-P5-003` 文档与源不符）。

## 完成标记

自动化验证已全部通过 → 已改签为 `ROADMAP-P5-Fin.md`；
上表两条**人工目视项**（气泡观感、关键词立绘切换）待用户复验，复验结论单独记录，不改签文件名。
