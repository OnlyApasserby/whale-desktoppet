# 状态机设计（STATE-MACHINE）

> 移植来源：`referances/dsh-whale-musume/assets/whale-moe-core.js`（纯函数、DOM-free）。
> 移植原则：**保持纯逻辑、零 Qt UI 依赖**，可脱离界面单测。

## 1. 状态集合（Pose）

| 语义 | pose 名 | 触发场景 |
|---|---|---|
| 待机 | `idle-cute` | 默认空闲 |
| 等待 | `waiting` | 等待用户操作 |
| 思考 | `thinking` | 思考/长时间无输入 |
| 工作 | `running` | *（资产已备，**状态机未接入**）*通用忙碌 |
| 成功 | `success` | 任务达成（`EventType::QuestDone`） |
| 失败 | `failure` | *（资产已备，**状态机未接入**）* |
| 好奇 | `curious` | 点击（`Zone::Body` 默认态） |
| 逗弄 | `teasing` | 待机随机小剧场（`TEASE_CHANCE`） |
| 离开 | `afk` | 挂机超时 |
| 脸红 | `blush` | 夸夸（`EventType::Praise`） |
| 生气 | `angry` | 戳一下（`EventType::Tease`） |
| 吃 | `eat` | 投喂（`EventType::Feed`） |
| 星星眼 | `star` | 三连击 |
| 庆祝 | `levelup` / `achievement` | 升级 / 成就解锁（**不是 `celebrate`**） |
| 睡眠 | `sleep` | 深夜时段 / 长时间挂机 |
| 问候 | — | *（`greet` 资产已备但**未接入**；分时问候走 `greet.*` **台词**，无专属立绘）* |
| 眨眼 | `wink` | *（资产已备，**状态机未接入**）* |
| 日常系列 | `daily-*`（咖啡/伸懒腰/吃东西…） | 目前仅 `WorkState::Game` → `daily-gaming`；待机小剧场用 `teasing` |
| 互动分区 | `react-head` / `react-belly` / `react-tail` | 点击不同部位（`Fx::None`） |
| 表情梗 | `meme-*` | 关键词命中（关键词表情共 **21 项**，其中 `meme-*` **10 项**；见 `CHAT.md` §4） |

> **「资产已备但未接入」说明**：`running` / `failure` / `celebrate` / `greet` / `wink` / `night`
> 等姿态只存在于 `src/core/PoseNames.h` 的**资产清单**与 `assets/poses/` 中，
> **当前没有任何代码路径会输出它们**（`PetStateMachine` 实际输出见上表右列）。
> 保留在清单里是为了「资产可寻址 + 后续可接入」，不代表已生效。
> 深夜时段走的是 `sleep`（`PetStateMachine::contextPose()`），**没有 `night` 这条分支**。

完整清单与命名以 `assets/poses/` 的 **93 张 webp** 为准（见 `PRESENTATION.md` §1）。

> 其中 4 张为**贴边立绘**（`home-peek` / `home-bottom` / `settings-peek` / `workbench-peek`）：
> 它们**不是状态机姿态**，而是由「桌宠窗口是否贴合桌面四条边框」驱动的**表现层**效果
> （见 `PRESENTATION.md` §3.1）。状态机**不会**输出这几个 pose，贴边期间也不打断 / 不占用
> 一次性姿态——离开边框后立绘立即回到状态机此刻应有的姿态。
> 反向的一条表现层约束：**贴边期间 `PoseView` 丢弃拖动姿态**（`setPose` 在「拖动中 + 已贴边」时
> 不换图），状态机照常输出 `pick-up`，只是不参与显示——避免拖动把探头立绘顶掉。

## 2. 时间窗口与概率常量（沿用 whale 取值）

| 常量 | 值 | 含义 |
|---|---|---|
| `AFK_MS` | 180000（180s） | 无输入进入 `afk` |
| `SPEECH_GAP_MS` | 6000（6s） | **主动说话**两条之间的最小间隔（用户交互台词不参与节流） |
| `SUCCESS_WINDOW_MS` | 2000（2s） | 成功态保持窗口 |
| `CURIOUS_WINDOW_MS` | 6000（6s） | 好奇态保持窗口 |
| `TEASE_CHANCE` | 0.006 | 每次 tick 触发逗弄概率 |

## 3. 输入事件

| 事件 | 来源 | 影响 |
|---|---|---|
| `Tick` | 定时器（如 200ms） | 概率事件、时间推进 |
| `Click(zone)` | PoseView 命中分区（head/belly/tail/body） | 互动姿态 + 台词 |
| `TripleClick` | 三连击检测 | `star` + 粒子特效 |
| `DragStart/DragEnd` | PetWindow | `pick-up` 立绘 + 惯性 |
| `Feed` / `Tease` / `Praise` | 右键菜单 | 投喂 `eat` / 生气 `angry` / 夸夸 `blush`+爱心 |
| `LevelUp` / `AchievementUnlocked` / `QuestDone` | GrowthService | `levelup` / `achievement` / `success` |
| `IdleTimeout` | AFK 计时 | `afk` / `sleep` |
| `Clock(hour)` | 系统时间 | 深夜判定 → `sleep`（无 `night` 分支） |
| `KeywordHit(kw)` | `PetController::handleKeywordHit`（经 `speak()` 播报，P5/P6） | `meme-*` 表情 |
| `WorkStateChanged(state)` | EnvironmentService → WorkStateService（P7） | 工作态通道：切立绘 + 播报一句 `work.*`；Unknown = 退出工作态 |

### 3.1 工作态通道（P7，详见 `PLUGIN-ARCHITECTURE.md` §6.2）

- 状态集合与判据在 `src/core/WorkState.h` / `WorkStateRules.h`（**零 Qt 纯逻辑**）；
  立绘与台词场景映射集中在 `WorkState.cpp` 的 `workStatePose()` / `workStateScene()`。
- **默认 `WorkState::Unknown`（无感知数据）时完全跳过工作态分支**，行为与 P6 一致（零回归）。
- **会话锁定 / 屏保（`systemPaused`）视为「有数据」并优先判 `Afk`**：锁屏时前台窗口读不到，
  数据形状与「未启用感知」相同，若先判「无数据」会把「确定离开」误降级为 `Unknown`
  （P7.1，见 `traps-P7.md` TRAP-P7-006）。
- 优先级插入位置：**一次性事件 > 工作态 > 时段态（夜/睡）> 挂机态 > 默认**。
- 「不打断」：一次性姿态未过期或拖拽中时，工作态只更新内部状态，**不覆盖立绘、不插话**。
- 「专注态主动静默」：`workStateIsFocus()`（Coding / VibeCoding / Debugging / Meeting）为真时，
  主动台词一律不说；**唯一豁免是 `work.*` 场景自身**（否则「状态显著变化时出现」会被自己静默掉）。

## 4. 输出（语义结果，非 UI）

状态机只产出**语义**，由 `PosePresenter` 翻译成 Qt 表现：

```
struct PoseResult {
    std::string pose;      // pose 名，如 "blush"
    std::string lineKey;   // 台词场景 key，空表示不说话
    std::string fx;        // 特效类型：none/heart/star/particle
    int         ttlMs;     // 该姿态保持时长（0 = 由规则决定）
    uint32_t    fxSerial;  // 表现批次序号：仅在产生**新特效**时自增（重放不变）
    uint32_t    lineSerial;// 表现批次序号：仅在产生**新台词**时自增（重放不变）
};
```

- 状态机**不**直接操作控件、不加载图片、不建动画。
- 优先级：一次性事件（点击/升级）> 时段态（夜/睡）> 挂机态（afk/thinking）> 默认（idle）。
- **表现批次序号**（P2 增补）：`handle()` 每 tick 都会返回同一个缓存结果，若下游按「fx≠none 就播」处理，
  一次三连击会被连续重放约 10 次粒子、一句台词会被每 200ms 换掉。
  因此序号只在**真有新表现**时自增，`PosePresenter` 只在序号变化时才播特效/台词。
  序号单调递增且**不随 `reset()` 清零**（避免复位后与新事件撞号导致漏播）。

## 5. 转移规则（高层）

1. **互斥**：同一时刻仅一个主姿态；一次性姿态到期后回落到「上下文默认态」
   （上下文默认态的优先级链见 §3.1：工作态 > 夜/睡 > 挂机态 > 默认）。
2. **不打断**：拖拽中、游戏/设置打开时，抑制主动小剧场与闲聊。
3. **台词节流（只约束主动说话）**：主动发言间隔 ≥ `SPEECH_GAP_MS`，且额度只由主动发言消耗。
   用户交互（点击/拖拽/菜单）**不受节流**——一次操作必须有一次性回应；
   「连点不刷屏」由下游保证：序号去重 + 气泡流式打断，最终只保留**最后一次操作**的台词。
4. **深夜静默**：23:00–05:59 不主动发言（可被点击等主动交互豁免）。
5. **概率事件**：每 tick 以 `TEASE_CHANCE` 触发逗弄/日常小动作。
6. **特效一次一迸发**：一次性事件产生的特效只在此次事件后触发一次（靠序号去重）；
   表现层另有 500ms 强制最小间隔，间隔内的新特效**丢弃不排队**（见 `PRESENTATION.md §2`）。

## 6. 可测性要求

- `PetStateMachine` 为纯 C++ 类：输入「事件 + 时间戳」，输出 `PoseResult`，**无随机源注入不可测** → 概率与随机数采用**可注入 RNG 接口**，测试用固定序列。
- 单测覆盖：各事件转移、窗口超时回落、主动说话节流与「用户交互不节流」、序号语义
  （同一缓存态重放不改号、新事件递增、回落归零）、深夜静默、不打断规则。

## 7. 与 whale `core.js` 的差异

| 项 | whale | 本项目 |
|---|---|---|
| 宿主状态信号（`data-tool` 等） | 有 | **移除**（无宿主） |
| 工作/余额/天气相关状态 | 有 | **移除** |
| 概率 RNG | `Math.random` | **注入式 RNG**（便于测试） |
| 输出 | DOM class / data 属性 | `PoseResult` 语义结构 |
| 重复表现 | DOM 重绘天然幂等（改 class 不重放特效） | 显式 `fxSerial`/`lineSerial` 去重（`PoseResult` 每 tick 重推同一缓存结果） |
| 台词节流 | 所有发言共用同一窗口 | 只约束**主动说话**，用户交互不节流（收敛交给表现层） |
