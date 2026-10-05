# ROADMAP · P8 — 时段常驻立绘 / 工作立绘池 / 预设对话

> 环境基线：Qt 6.8.4 + MSVC（VS 18 2026）+ CMake 4.4.2（见 `BUILD.md`）。
> 本阶段为**追加批次**（在 P7 / EX1 之后），不改动 P0–P7 的既有语义，只做**增量接入**。

## 0. 需求（用户口径，逐条对应）

| # | 需求原文（要点） | 落地 |
|---|---|---|
| R1 | 日间（7:00–18:00）空闲常驻 `dsh-whale-state-idle-cute`；夜间（18:00–23:00）空闲常驻 `dsh-whale-state-night` | `core/DaySlotRules.h` + `PetStateMachine::contextPose()` §1.2 |
| R2 | 深夜（23:00–7:00）空闲常驻 `dsh-whale-state-daily-pajama`，**不作为日间待机池候选**；用户点击后恢复 `night`，1 分钟无操作切回睡衣 | `DaySlot::LateNight`；**2026-10-04 二次修订：该「点击唤醒」口径已废弃** —— 深夜常驻恒为 `daily-pajama`，点击不换立绘（仅回应台词 + 累计计数，满 10 次转虚弱），跨时段当帧立即刷新（见 §P8.1） |
| R3 | 检测到用户在编程时，常驻立绘改为 `dsh-whale-state-running`；工作时使用**含 work 字段的立绘池**；该池需联动**热词检测**与 **ACP** | `core/WorkPosePool.*`（13 张 work-* 轮转 + 最近窗口）+ `workStateIsCoding()`；热词 `speak()`/`KeywordHit` → `note()`；ACP → `WorkStateChanged` |
| R4 | 预设问答（**修订版**，见 §P8.5）：**主人提问 → 鲸鱼娘回答**；问题选项为独立池、**五选一**；固定 1 个天气问题（**无 API 时不可用**）、固定 1 个敏感私密问题（**好感度 ≥ 5000 解锁，每日 3 次**），其余 3 个每次随机刷新；每个问题对应**三个预设回答**，选中后**随机取一个并以文字输出**；面板窗口标题固定为「主人的问题」 | `core/PresetDialogue.*`、`core/DialogueOptions.*`、`core/DialoguePoseRules.h`、`core/WeatherRules.*`；`viewmodel/DialogueService`、`viewmodel/WeatherService`；`view/DialoguePanel`；`assets/lines/dialogue.txt`；`docs/DIALOGUE.md` |

## 1. 交付物

### P8.1 时段常驻立绘（R1 / R2）

| 项 | 位置 |
|---|---|
| 时段划分与每段空闲立绘（纯函数） | `src/core/DaySlotRules.h`（`daySlotOf` / `daySlotPoseOf`） |
| 深夜**无唤醒态**（2026-10-04 二次修订；原为 1 min 唤醒窗口） | `PetStateMachine::touchInput()` 不再维护窗口；`lateNightAwake()` / `kLateNightAwakeMs` 已移除 |
| 上下文链第 2 档改为时段态 | `PetStateMachine::contextPose()`（日间**不接管**，仍走挂机 / 静息 / 节日） |
| 立绘零延迟 | `PoseLibrary::coreKeys()` 加入 `night` / `daily-pajama` / `running`；`sleep` 移出（无代码路径） |

> **P8 二次修订（2026-10-04）**：R2 的「点击唤醒 `night` 1 分钟」实现后暴露出两个问题 ——
> ① 跨时段的唤醒窗口会**泄漏**（22:59 的交互续期到 23:00 之后，深夜仍显 `night`）；
> ② 跨时段切换依赖后续 `Tick`，进入 23:00 后最长约 60s 才换成睡衣。
> 现已**移除唤醒态**（常驻恒为 `daily-pajama`，点击不换立绘、仅累计），并让 `Clock` 检测到
> **跨时段时当帧立即刷新**。踩坑记录见 `docs/pitfalls/` TRAP-P8-010。

### P8.2 工作立绘池（R3）

| 项 | 位置 |
|---|---|
| 编程族判定（Coding / VibeCoding / Debugging） | `core::workStateIsCoding()` |
| 池成员与轮转（13 张 `work-*`，最近窗口 3，60s 一张） | `src/core/WorkPosePool.{h,cpp}` |
| 与常驻立绘同源（避免「编程时闪一下 work-ram」） | `PetStateMachine::contextWorkPose()`（`contextPose` 与工作态播报共用） |
| 热词联动 | `PetStateMachine::speak()` / `EventType::KeywordHit` → `m_workPool.note(pose)` |
| ACP 联动 | ACP → `WorkStateService::applyExternalState` → `WorkStateChanged` → 池节奏重置 |

### P8.3 预设对话（R4）

| 项 | 位置 |
|---|---|
| 语料（4 类 9 题） | `assets/lines/dialogue.txt` + `assets/assets.qrc` |
| 语料解析 + 问题池（天气题保留） | `src/core/PresetDialogue.{h,cpp}` |
| 独立立绘池 | `src/core/DialoguePoseRules.h` |
| 天气类型判定 | `src/core/WeatherRules.{h,cpp}` |
| 编排（低频提问 / 三选一 / 刷新池） | `src/viewmodel/DialogueService.{h,cpp}` |
| 彩云天气（缓存 30min / 退避 60min / 空配置不联网） | `src/viewmodel/WeatherService.{h,cpp}` |
| 提问面板（无边框工具窗口） | `src/view/DialoguePanel.{h,cpp}` + `resources/qt-ui/project.qss` |
| 接线（菜单开关 + 立即提问 + 门槛） | `PetWindow::setupDialogue()` / `dialogueCanAsk()` / `askDialogueNow()` |
| 设置项（`dialogue_enabled` / `weather_key` / `weather_location`） | `model/SettingsData.h`、`model/SettingsRepo.cpp`、`view/SettingsDialog.*` |

### P8.5 问答系统（R4 修订版，2026-10-04 二次需求）

| 项 | 位置 |
|---|---|
| 语料（主人提问 + 每问三回答；天气题按类型各三条候选） | `assets/lines/dialogue.txt` |
| 语料解析（同一 slot 可多条候选；`answerSlots` / `answersFor`） | `src/core/PresetDialogue.{h,cpp}` |
| **五选一选项池**（固定天气槽 + 固定敏感槽 + 随机 ×3，含可用性与原因） | `src/core/DialogueOptions.{h,cpp}` |
| 回答选取（普通题随机 slot；天气题按彩云类型，逐级回落） | `core::pickAnswerSlot` |
| 编排（提醒门槛 / 选择 / 敏感题配额落库） | `src/viewmodel/DialogueService.{h,cpp}` |
| 面板（标题「主人的问题」、五选一、禁用项 tooltip、60s 超时） | `src/view/DialoguePanel.{h,cpp}` |
| 接线（好感度 provider、`meta` 配额、菜单入口） | `PetWindow::setupDialogue()` / `dialogueCanAsk()` / `askDialogueNow()` |
| 解锁与限额常量（禁止散落字面量） | `core::kSensitiveUnlockAffinity = 5000`、`core::kSensitiveDailyLimit = 3` |
| 每日配额存储 | `model::Database::meta`：`dialogue.sensitive_day` / `dialogue.sensitive_used_today`（跨天清零） |

**与旧版（P8.3）的差异**：① 方向反转（原为鲸鱼娘提问、用户作答）；② 由「问题池 + QA 后刷新」
改为「**五选一 + 每次打开重新刷新 3 个随机题**」；③ 新增固定**天气槽**（无 API 禁用）与
固定**敏感槽**（好感度 5000 + 每日 3 次）；④ 回答改为「每问三选一后随机取一条、**文字输出**」；
⑤ 面板标题固定为「主人的问题」。旧的 `PresetDialoguePool` 已移除（其职责被 `DialogueOptions` 取代）。

### P8.4 测试与文档

- 新增测试目标 `test_preset_dialogue`（时段 / 天气 / 工作池 / 语料 / 问题池，13 用例）；
- 更新 `test_state_machine`（`nightIsSleep` → `daySlotsAndLateNightPersistent`；编程态期望 `running`）、
  `test_work_state`（编程 `running`；Meeting 走池）、`test_game_companion`（编程 `running`）、
  `test_pose_assets`（core 14 / warm 25 / 容量 40）；
- 新增 `docs/DIALOGUE.md`；更新 `STATE-MACHINE.md`、`CHAT.md`、`SETTINGS.md`、`POSE-ASSETS.md`、
  `TESTING.md`、`README.md`；
- 真实踩坑记入 `docs/pitfalls/`。

## 2. 验收

| # | 验收项 | 证据 |
|---|---|---|
| A1 | 07:00 / 17:59 → `idle-cute`（日间不接管时段立绘）；18:00 / 22:59 → `night`；23:00 / 06:59 → `daily-pajama` | `test_preset_dialogue::daySlotCoversWholeClock`、`test_state_machine::daySlotsAndLateNightPersistent` |
| A2 | 深夜点击**不换立绘**（一轮 < 10 次常驻恒为 `daily-pajama`，仅回应台词）；累计满 10 次 → `meme-smile-pain` 虚弱 20s；虚弱到期直接回 `daily-pajama` | `daySlotsAndLateNightPersistent`、`lateNightClicksTriggerWeakPose` |
| A2' | 跨时段**当帧立即刷新**：22:59 交互后跨 23:00 立即变睡衣，不被上一时段的交互 / 一次性姿态拖住 | `daySlotsAndLateNightPersistent` |
| A3 | 深夜立绘优先级高于挂机态（长时间无输入仍是睡衣，不回退 `afk`） | `daySlotsAndLateNightPersistent` |
| A4 | 编程（Coding / VibeCoding / Debugging）→ 常驻 `running` | `test_work_state::machineDrivesPoseAndSilencesProactive`、`test_game_companion` |
| A5 | 其余 busy 工作态（Reading / Meeting）→ 从 `work-*` 池轮转，一轮不重复 | `TESTING` 中 `test_preset_dialogue::workPoolRotatesWithoutRepeat` |
| A6 | 热词命中的 work-* 立绘不被紧接着轮回到（联动） | `workPoolAvoidsNotedPoses` |
| A7 | 五选一：恒为 5 项、槽位顺序固定（天气 → 敏感 → 随机 ×3），每次打开重新刷新 3 个随机题 | `optionsAreAlwaysFiveWithFixedSlots`、`randomOptionsAvoidRecentIds` |
| A8 | 天气槽在未配置彩云 key / 城市时禁用并给出原因；敏感槽在好感度 < 5000 或当日次数用尽时禁用 | `weatherSlotDisabledWithoutApi`、`sensitiveSlotLockedByAffinity`、`sensitiveSlotBlockedByDailyQuota` |
| A8' | 敏感 / 选择立绘池成员全部可达且随机不连号 | `dialoguePosesExist` |
| A8'' | 每个问题三个预设回答，选中后随机取一个（普通题随机 slot；天气题按类型并逐级回落） | `corpusKeepsMultipleCandidatesPerSlot`、`pickAnswerSlotRandomForQuestion`、`pickAnswerSlotFollowsWeatherKind` |
| A9 | 未配置天气 key / 城市时**零网络请求**（`WeatherService::configured() == false` 时 `refresh()` 直接返回） | 代码路径 + `DIALOGUE.md` §5 |
| A10 | 立绘档位仍是「预载完成即全部常驻、不互相逐出」且 M3 ≤ 10 MiB | `test_pose_assets`（14 + 25 = 39 < 40；40 × 256² × 4 = 10.0 MiB） |
| A11 | Debug 全套 CTest 全绿 | 35/35（2026-10-04 全量复跑；见 `TESTING.md` §2） |

## 3. 零回归

- `WorkState::Unknown`（无感知数据）时工作态分支**完全跳过** —— 与 P6 一致；
- 日间时段**不接管**上下文链：静息 / 挂机 / 节日换装行为与 P7 完全一致；
- 深夜的**静默口径不变**（`PetStateMachine::isNight()` 仍用于主动台词静默），只换了立绘；
- 预设对话默认**开**但受「静息 + 非深夜 + 气泡空闲」门槛约束，且语料缺失时整体降级为不提问；
- 天气**不配置就不联网**（与参考项目「默认空 = 完全不联网」口径一致）。
