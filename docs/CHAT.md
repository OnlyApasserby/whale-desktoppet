# 梗聊天设计（CHAT）

> 保留范围：**仅梗聊天**（whale 的天气/余额/TTS/无障碍全部移除）。

## 1. 台词库组织

- **不硬编码进 C++**，以外部资源承载（便于个人替换）。
- 位置：`assets/lines/`，**按场景分文件**（P5 落地）：
  | 文件 | 覆盖场景 key | 说明 |
  |---|---|---|
  | `lines.txt` | `click.*` / `drag.*` / `menu.*` / `evt.*` / `idle.*` / `proactive.*` | 状态机全部交互场景（P2 起） |
  | `greet.txt` | `greet.*` | 分时问候 |
  | `bond.txt` | `bond.low-mood` / `bond.high-mood` / `bond.l3` / `bond.l5` / `bond.l7` | 心情分层 + 羁绊专属 |
  | `meme.txt` | `meme.*` | 关键词梗台词 |
  | `game.txt` | `game.*` | 小游戏（扫雷）播报 |
  | `kitten.txt` | `kitten.*` | 小游戏（鲸鱼娘找小猫）播报 |
  | `chess.txt` | `chess.*` | 小游戏（国际象棋）播报 |
  | `work.txt` | `work.*` | 工作状态播报（P7） |
  | `dialogue.txt` | `dialogue.<id>.<slot>` | 预设对话的问题与回答（P8，**格式见 `DIALOGUE.md` §2**，非 `sceneKey\|文本` 单行格式） |
- 单行格式：`sceneKey|台词文本`（只取**第一个** `|`；`#` 与空行忽略；首尾 trim）。
- 每条台词标注所属**场景 key**（与 `STATE-MACHINE.md` 的 pose/场景对应）。
- 规模：取材自 whale 原库 **530+ 条**；本项目当前实际收录 **368 条**（8 个文件，
  以「含 `|` 的非注释行」计：`lines` 116 / `meme` 81 / `kitten` 48 / `work` 38 /
  `bond` 25 / `greet` 25 / `chess` 20 / `game` 15）。语料可裁剪/精简，个人使用可自定。
- 来源：`references/dsh-whale-musume/assets/whale-moe-core.js` 的 `LINES` 常量，迁移为独立文本资源。

## 2. 分时问候

> 规则落地于 `src/core/ChatRules.h::greetSlot()` / `greetSceneKey()`（照搬源 `greetBucket()`）。

| 时段 | 小时 | 场景 key |
|---|---|---|
| 早上 | 06–08 | `greet.morning` |
| 上午 | 09–11 | `greet.forenoon` |
| 中午 | 12–13 | `greet.noon` |
| 下午 | 14–17 | `greet.afternoon` |
| 傍晚 | 18–22 | `greet.evening` |
| 深夜 | 23–05 | 不主动发言（点击等主动交互豁免） |

同一时段（含跨整点但在同一段内，如 9/10/11 点同属上午）**只问候一次**，由 `ChatService` 去重。

## 3. 心情分层台词

> 规则落地于 `src/core/ChatRules.h::moodTier()` / `moodSceneKey()`；阈值 `<40` 低落 / `40–69` 中性 / `≥70` 高涨。

- 心情**低落**（`<40`）→ `bond.low-mood` 温柔向台词；**高涨**（`≥70`）→ `bond.high-mood` 元气向台词；中性不替换。
- **仅在心情档位发生变化时**播报一次（避免反复刷屏）。
- 羁绊等级 **Lv3 / Lv5 / Lv7** 解锁专属台词（`bond.l3` / `bond.l5` / `bond.l7`，对应 `GAMEPLAY.md`），同样**只在跨档时**播报一次。

## 4. 关键词表情感知（21 种梗）

> 触发词表与立绘映射落地于 `src/core/ChatRules.h`：`kKeywordRules[]`（30 组触发词）、
> `kKeywordPoses[]`（21 项 id→立绘）。
>
> **勘误**：本节原写「13 种」，与参考项目源文件 `KEYWORD_POSES` **实测 21 项**不符，
> P5 已按源文件修正为 **21**（详见 `docs/pitfalls/` TRAP-P5-003）。

| 关键词 id | 表情立绘 | 备注 |
|---|---|---|
| `kyun` `omg` `doge` `sike` `worship` `peace` `doubt` `wakuwaku` `smilepain` `ojisan` | `meme-kyun` … `meme-ojisan`（`smilepain`→`meme-smile-pain`） | 10 项落在 `meme-*` |
| `deploy` `meeting` `review` `bugtalk` `ddl` `cake` `slack` | `work-deploy` `work-meeting` `work-review` `work-debug` `work-deadline` `work-boss` `work-slack-phone` | 7 项落在 `work-*` |
| `crazy` | `abstract` | 抽象向 |
| `cheer` `flag` | `bold` | 打气向 |
| `tired` | `work-sleep` | 困倦向 |

- **立绘名必须查表（`keywordPose(id)`），不能用 `"meme-" + id` 拼接**（详见 `docs/pitfalls/` TRAP-P5-001）：
  21 项里 11 项的目标立绘不在 `meme-*` 命名空间，拼接会**静默**退化成通用 `curious`。
- `hug` / `cute` / `morning` 三组**既无专属立绘、`meme.txt` 里也没有对应台词**：
  `PetController::handleKeywordHit` 在 `keywordPose()` 返回 `nullptr` 时**直接返回**，
  不切表情也不说话（热词录入的下拉同样不提供这三项，避免「录了却没反应」）。
- 命中即在气泡中间切换为表情立绘，并说 `meme.<id>` 台词；命中来自用户主动输入，**不受深夜静默 / ≥6s 节流限制**。
- **默认关闭**（沿用 whale 对隐私/打扰的谨慎），用户可在设置中开启。
- **本项目无聊天输入源**：触发源为**用户自定义热词**或（可选）本地剪贴板/输入内容匹配；定位为「个人用的趣味反应」，不接入任何外部聊天。

## 5. 说话节流与打断规则

| 规则 | 值 / 行为 |
|---|---|
| 最小间隔 | ≥ 6000ms（`SPEECH_GAP_MS`） |
| 拖拽中 | 不发言 |
| 设置面板打开时 | 不主动发言（小游戏为**例外**：进行扫雷时会主动播报立绘与台词，见 `MINIGAME-INTERFACE.md`） |
| 深夜 | 不主动发言（P6 起可经 `night_quiet` 关闭） |
| 连续两条 | 避免重复同一条（最近 N 条去重） |

## 6. 数据流

```
事件/定时（PetController）
  ├─ 主动时机（分时问候 / 心情跨档 / 羁绊跨档）
  │    → ChatService.greetScene() / moodSceneFor() / bondSceneFor()   # 场景决策 + 去重
  │    → PetStateMachine.speak(pose="", scene, ...)                    # 节流/深夜静默/序号统一把关
  ├─ 外部文本（剪贴板/热词）
  │    → ChatService.matchText()（keyword_aware 关 → 空）
  │    → ChatRules.keywordPose()/keywordSceneKey() → 21 项 id→立绘映射
  │    → PetStateMachine.speak(pose="meme-*"/"work-*", scene, ...)（proactive=false，不节流）
  └─ Presenter.present(PoseResult)
       → LineTable.pick(scene, rng)   # 候选选取 + 最近 N 条去重
       → SpeechBubble 显示气泡（含逐字流式）
```

> **职责边界**：`ChatService` 只做「场景决策 + 关键词门控 + 跨档去重」；
> **候选选取与最近 N 条去重**在 `LineTable`；**≥6s 节流与深夜静默**在 `PetStateMachine`
> （`proactive=true` 的台词统一把关，用户交互豁免），避免两处计时打架。

## 7. 配置项

- `bubble_enabled`：气泡开关（P6 起经 `SpeechBubble::setSuppressed` 生效）。
- `keyword_aware`：关键词感知开关（**默认关**）。落库于 `SettingsData.keywordAware`；
  P5 提供右键菜单「关键词感知（梗表情）」勾选项，P6 起亦在设置面板「陪伴表现」组；
  勾选后挂接**本地剪贴板**文本匹配（仅开启时读取剪贴板内容，关闭时不做任何匹配）。
- `night_quiet`：深夜静默开关（**默认开**，P6）。落库于 `json_ext`（`SETTINGS.md` §3）；
  关闭后深夜也会主动发言（用户主动交互本就豁免）。
- 台词库路径：可指向外部目录，便于个人改写（`PosePresenter::loadBundledLines` 逐份加载，缺失只降级）。

## 8. 明确移除

- 余额播报、TTS 播报、无障碍 aria 播报。
- 依赖宿主 DOM 的「检测工具运行」联动。

> **P8 修订**：**天气台词重新引入**，但形态与参考项目不同 —— 只作为
> **预设对话的「天气题」**（保留项）按彩云天气类型取回答与立绘，
> 不做待机闲聊里的天气插入、不做天气卡片；且 `weather_key` / `weather_location`
> 为空时**完全不联网**。详见 `DIALOGUE.md` §5 与 `SETTINGS.md` §8。

## 10. 预设问答（P8）

**主人提问 → 鲸鱼娘回答**的固定问答功能，与梗聊天（§1–§7）**并列**、
共用同一份 `core::LineTable` 与同一条 `speak → Presenter → SpeechBubble` 管线：

- 语料：`assets/lines/dialogue.txt`；回答以 `dialogue.<id>.<slot>` 登记进台词表
  （同一 slot 可有多条候选，随机取一条）；
- 面板标题固定为「主人的问题」，**五选一**：固定 1 个天气问题（未配置彩云 API 时禁用）、
  固定 1 个敏感 / 私密问题（好感度 ≥ 5000 解锁，每日 3 次），其余 3 个每次随机刷新；
- 每个问题三个预设回答，选中后随机取一个**以文字输出**（气泡）；
- 立绘：敏感池（`meme-broke`/`meme-cry`/`meme-heart`）、选择池（`meme-no`/`meme-yes`）、
  天气（`weather-*`）、日常（`curious`）—— 选中问题后接管常驻立绘，回答到期即回落；
- 设计、解锁与配额、门槛与降级：见 `DIALOGUE.md`（本文不重复）。

## 9. 实现状态（P5）

| 交付项 | 代码位置 | 单测 |
|---|---|---|
| 语料分文件迁移（`lines.txt` 116 条 + 分时/心情/羁绊/梗） | `assets/lines/*.txt`、`assets/assets.qrc` | `test_line_table`、`test_chat` |
| 多文件加载 + 缺失优雅降级 | `PosePresenter::loadBundledLines` | `test_chat::missingCorpusDegradesToSilence` |
| 分时问候 / 深夜静默 | `core/ChatRules.h`、`viewmodel/ChatService` | `test_chat::greetByHourAndNightSilence` |
| 心情分层 / 羁绊专属 | `core/ChatRules.h::moodTier/bondSceneKey`、`ChatService` | `test_chat::moodSceneOnTierChange`、`bondSceneOnThresholdCrossing` |
| 关键词映射（21 项）+ 开关 | `core/ChatRules.h`、`ChatService`、`PetController::handleText` | `test_chat::keywordSwitchGatesMatching`、`keywordPosesAllExist` |
| ≥6s 节流 / 序号统一 | `core/PetStateMachine::speak` | `test_chat::speakRespectsThrottleAndNightSilence` |
| 接线（气泡 + 立绘） | `PetController`（`presentSpeak`）、`PetWindow::setupChat` | `test_smoke` |

验证：`ctest -C Debug` 与 `-C Release` 均 **7/7 通过**（P5 当时共 7 个测试目标；详见 `TESTING.md`，
现为 22 个）。踩坑记录见 `docs/pitfalls/`。
