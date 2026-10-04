# 预设问答（DIALOGUE）

> 阶段：P8（见 `ROADMAP-P8.md`）。语料：`assets/lines/dialogue.txt`。
> 规则（零 Qt 纯逻辑）：`src/core/PresetDialogue.*`、`src/core/DialogueOptions.*`、
> `src/core/DialoguePoseRules.h`、`src/core/WeatherRules.*`。
> 编排：`src/viewmodel/DialogueService.*`、`src/viewmodel/WeatherService.*`。
> 面板：`src/view/DialoguePanel.*`。

## 1. 形态与目标

**主人（用户）提问 → 鲸鱼娘回答**：

1. 面板标题固定为 **「主人的问题」**，列出 **五个**可选问题（**五选一**）；
2. 五个槽位中：
   - **1 个天气问题**（固定槽位）：**未配置彩云天气 API 时不可用**（按钮禁用并说明原因）；
   - **1 个敏感 / 私密问题**（固定槽位）：**好感度 ≥ 5000 解锁**，且**每日最多 3 次**；
   - **其余 3 个问题每次打开面板时随机刷新**（从日常 / 选择类问题池中抽取）；
3. 每个问题都有 **三个预设回答**；主人选中问题后，程序**随机取其中一个**，
   由鲸鱼娘**以文字输出**（走既有的气泡管线）。

| 槽位 | 来源 category | 可用条件 | 立绘（独立池） |
|---|---|---|---|
| 天气 | `weather` | 彩云 `weather_key` + `weather_location` 均已填写 | 按天气类型：`weather-umbrella` / `weather-snow` / `weather-thunder` / `weather-cold` / `weather-rain-happy`，未判定 → `curious` |
| 敏感 / 私密 | `sensitive` | 好感度 ≥ `kSensitiveUnlockAffinity`（5000）且当日剩余次数 > 0 | `meme-broke` / `meme-cry` / `meme-heart`（随机且不连号） |
| 随机 1–3 | `normal` | 总是可用 | `curious` |
| 随机 1–3 | `choice` | 总是可用 | `meme-no` / `meme-yes`（随机且不连号） |

> 槽位**恒定存在**：即使语料里缺某一类题目、或该槽位不可用，界面仍然是五个位置
> （不可用项禁用 + tooltip 说明原因），保证「五选一」的形状稳定。

## 2. 语料格式（`assets/lines/dialogue.txt`）

```
q|<id>|<category>|<问题文本>     ← 主人可以问的问题
a|<id>|<slot>|<回答文本>         ← 鲸鱼娘的回答；同一 slot 可写多条候选
```

- `category`：`normal` / `sensitive` / `choice` / `weather`。
- `slot`：前三类为 `0` / `1` / `2` —— **每个问题三个预设回答**；
  `weather` 为天气类型 id（`sunny` / `cloudy` / `overcast` / `rain` / `snow` / `fog` /
  `thunder` / `hail` / `unknown`），由彩云天气 API 判定后取该类型下的回答。
- **同一 `(id, slot)` 可写多条**：它们登记进**同一个**台词场景 key
  `dialogue.<id>.<slot>`，由 `LineTable::pick` 随机取一条（带最近 N 条去重）。
  天气题即用这一点保证「每个类型也有三个候选」。
- 解析口径（`core::PresetDialogueTable::loadFromText`）：
  - 空行与 `#` 注释忽略；同一 `id` 只认首次 `q|` 声明；
  - 孤儿 `a|`（无对应 `q|`）、非法 category、空 id / 空文本一律**丢弃**。

## 3. 五选一选项池（`core/DialogueOptions.h`）

```cpp
struct DialogueOption {          // 一个槽位
    const DialogueQuestion *question; // nullptr = 语料里没有该类问题
    DialogueOptionKind kind;          // Weather / Sensitive / Random
    bool available;                   // 可点选
    const char *reason;               // 不可用原因（kDialogueReason*）
};
std::vector<DialogueOption> buildDialogueOptions(
    const PresetDialogueTable &table, IRandom *rng,
    const DialogueOptionRequest &request,          // 天气可用 / 好感度解锁 / 配额剩余 / 随机数
    const std::vector<std::string> &recentIds = {}); // 上一轮的随机题（优先避开）
```

- 返回**恒为 5 项**，顺序固定：`天气` → `敏感` → `随机 ×3`；
- 随机部分：候选 = `normal` + `choice`，洗牌后取前 3；**优先避开 `recentIds`**，
  语料不足时才允许重复（不足的槽位以 `question == nullptr` 占位并禁用）；
- 不可用原因常量：`kDialogueReasonWeather` / `kDialogueReasonSensitiveLocked` /
  `kDialogueReasonSensitiveQuota` / `kDialogueReasonUnavailable`。

### 回答选取（`pickAnswerSlot`）

| 问题类别 | 选取方式 |
|---|---|
| `normal` / `sensitive` / `choice` | 从该问题已登记的 slot（`0`/`1`/`2`）中**随机取一个** → 该 key 下 1 条候选 |
| `weather` | 取**天气类型**对应的 slot；该类型没有槽位 → 回落 `unknown` → 再回落首条回答的槽位 |

## 4. 解锁与每日配额

| 项 | 值 / 实现 |
|---|---|
| 解锁门槛 | 好感度（`pet_state.affinity`，上限 10000）≥ **5000**（`core::kSensitiveUnlockAffinity`），由 `PetWindow` 注入 `GrowthService` 的实时值 |
| 每日次数 | **3 次**（`core::kSensitiveDailyLimit`），**每次实际选择敏感问题**时消耗一次 |
| 落库 | `model::Database::meta`：`dialogue.sensitive_day`（`core::dayKey` 自然日）/ `dialogue.sensitive_used_today`；跨天自动清零（与 `MiniGameService` 的每日上限同构） |
| 拒绝路径 | 未解锁 / 配额用尽 → 槽位禁用（tooltip 说明）；即使被绕过，`choose()` 也会拒绝且不输出回答 |

## 5. 触发时机与门槛

`viewmodel::DialogueService` 用单次 `QTimer` 低频**提醒**（默认 **15–30 分钟**随机一档），
是否真的弹出面板由 View 注入的门槛回调决定（`PetWindow::dialogueCanAsk`）：

| 条件 | 要求 |
|---|---|
| 桌宠 | 可见（`pet_enabled`） |
| 面板 | 未打开（无待选的一批问题） |
| 气泡 | 空闲（不打断正在显示的台词） |
| 工作态 | 非 busy（Reading / Coding / VibeCoding / Debugging / Meeting 一律不打扰） |
| 游戏陪玩 | 非「静默陪伴」（CG / 影片 / 对话演出） |
| 时段 | 非深夜（`DaySlot::LateNight`，即 23:00–06:59） |

门槛不满足时**顺延一轮**，不打扰。菜单「**我想问鲸鱼娘…**」为**用户主动**入口
（`offerOptions(force=true)`）：跳过「静息」门槛，但仍要求面板未打开。

面板在 **60 秒**内无人选择则自动关闭（`DialoguePanel::kAnswerTimeoutMs`），
**不消耗任何配额**；关闭与超时都不影响下一次重新刷新的三个随机题。

## 6. 彩云天气（`viewmodel::WeatherService`）

| 项 | 值 |
|---|---|
| 接口 | `GET https://api.seniverse.com/v3/weather/now.json?key=<key>&location=<城市 / 经纬度>&language=zh-Hans&unit=c` |
| 判定 | 响应 `now.weather`（中文）→ `core::weatherKindFromCaiyun()`（子串优先级：雷 > 雹 > 雪 > 雨 > 雾霾尘 > 阴 > 云 > 晴） |
| 可用性 | **key 或城市为空 → 天气槽位禁用**（不联网、不发任何请求） |
| 缓存 | 成功结果 **30 分钟**新鲜期内不重复请求 |
| 失败 | 静默退避 **60 分钟**（`failed` 信号仅作诊断，不弹窗） |
| 降级 | 已配置但请求失败 / 类型无专属回答 → 回落 `unknown` 槽位（「我这边看不到窗外…」）；仍无 → 回落首条回答 |

设置项：`dialogue_enabled`（默认 **开**）、`weather_key`、`weather_location`（默认 **空 = 不联网**），
均落 `settings.json_ext`（见 `SETTINGS.md` §8）。

## 7. 数据流

```
QTimer（15–30min 提醒）→ DialogueService::offerOptions()（门槛回调把关）
  ├─ 采集可用性：天气（彩云已配置？）/ 敏感（好感度 ≥ 5000？当日剩余 > 0？）/ 随机
  ├─ core::buildDialogueOptions()：五选一（天气、敏感、随机 ×3，优先避开上一轮）
  └─ signal optionsOffered(questions, enabled, hints)
        → DialoguePanel::showOptions()（标题「主人的问题」；不可用项禁用 + tooltip）

主人点选某个问题 → DialoguePanel::chosen(index)
  → DialogueService::choose(index)
        ├─ 敏感题：消耗今日配额（+1 落库 —— 用尽则拒绝）
        ├─ core::pickAnswerSlot()：普通题随机 slot；天气题按彩云类型
        └─ signal answered(pose, "dialogue.<id>.<slot>")
              → PetController::presentGame(pose, sceneKey, 6s) → 状态机 speak（proactive=false）
                    → Presenter：setPose(pose)（该问题的独立立绘）+ LineTable.pick(sceneKey)
                      → SpeechBubble 逐字输出鲸鱼娘的回答文字
  → 下一轮提醒重新排期（三个随机题届时重新刷新）

面板关闭 / 超时 → DialoguePanel::dismissed → DialogueService::cancel()（不消耗配额）
```

## 8. 测试

| 覆盖点 | 用例（`tests/test_preset_dialogue.cpp`） |
|---|---|
| 时段划分与立绘 | `daySlotCoversWholeClock`、`daySlotPosesExist` |
| 彩云中文 → 类型 | `caiyunWeatherMapsToKinds`（含「雷阵雨」「雨夹雪」「阴转多云」等边界） |
| 天气立绘可达 | `weatherKindPosesExist`、`weatherKindIdRoundTrip` |
| 工作立绘池 | `workPoolRotatesWithoutRepeat`、`workPoolAvoidsNotedPoses`、`codingStatesAreSeparated` |
| 语料解析 | `corpusParsesQuestionsAndAnswers`、`corpusKeepsMultipleCandidatesPerSlot`、`corpusIgnoresDirtyLines`、`sceneKeysAreStable` |
| 五选一 | `optionsAreAlwaysFiveWithFixedSlots`、`randomOptionsAvoidRecentIds` |
| 槽位可用性 | `weatherSlotDisabledWithoutApi`、`sensitiveSlotLockedByAffinity`、`sensitiveSlotBlockedByDailyQuota` |
| 回答选取 | `pickAnswerSlotRandomForQuestion`、`pickAnswerSlotFollowsWeatherKind` |
| 独立立绘池 | `dialoguePosesExist` |

> 门槛（静息 / 非深夜 / 面板未打开）与配额落库的运行期行为，由
> `PetWindow::dialogueCanAsk` / `DialogueService::setDatabase` 承担；
> 纯逻辑判定（可用性输入 → 选项状态）已由上表覆盖。

## 9. 明确不做

- 不做 LLM / 联网对话（问题与回答均为本地语料，可自行替换文本）；
- 不做用户自由输入提问（只能从五个选项里选）；
- 不做多语言、不做语音播报；
- 天气不做多城市卡片、不做手动刷新按钮（设置里改配置即触发一次请求）。
