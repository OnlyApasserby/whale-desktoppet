# 状态机设计（STATE-MACHINE）

> 移植来源：`referances/dsh-whale-musume/assets/whale-moe-core.js`（纯函数、DOM-free）。
> 移植原则：**保持纯逻辑、零 Qt UI 依赖**，可脱离界面单测。

## 1. 状态集合（Pose）

| 语义 | pose 名 | 触发场景 |
|---|---|---|
| 待机 | `idle-cute` | 日间（07:00–17:59）空闲常驻 / 默认空闲（P8） |
| 等待 | `waiting` | 等待用户操作（日间静息第二档） |
| 思考 | `thinking` | 思考/长时间无输入（日间静息第三档） |
| 工作 | `running` | **P8 起接入**：编程族（Coding / VibeCoding / Debugging）工作时常驻 |
| 成功 | `success` | 任务达成（`EventType::QuestDone`） |
| 失败 | `failure` | *（资产已备，**状态机未接入**）* |
| 好奇 | `curious` | 点击（`Zone::Body` 默认态） |
| 逗弄 | `teasing` | 待机随机小剧场（`TEASE_CHANCE`；**深夜独立阶段不触发**） |
| 离开 | `afk` | 挂机超时 |
| 脸红 | `blush` | 夸夸（`EventType::Praise`） |
| 生气 | `angry` | 戳一下（`EventType::Tease`） |
| 吃 | `eat` | 投喂（`EventType::Feed`） |
| 星星眼 | `star` | 三连击 |
| 庆祝 | `levelup` / `achievement` | 升级 / 成就解锁（**不是 `celebrate`**） |
| 睡眠 | `sleep` | **P8 起无代码路径输出**（深夜空闲改 `daily-pajama`）；资产保留、按需加载 |
| 夜间 | `night` | **P8 起接入**：傍晚（18:00–22:59）空闲常驻；深夜被交互唤醒的 1 分钟内 |
| 睡衣 | `daily-pajama` | **P8 起接入**：深夜（23:00–06:59）空闲常驻（**独立阶段**，不参与任何随机立绘池） |
| 虚弱 | `meme-smile-pain` | **2026-10-04 起接入**：深夜点击累计 ≥ 10 次 → 保持 20s + `click.latenight.weak` 虚弱台词 |
| 问候 | — | *（`greet` 资产已备但**未接入**；分时问候走 `greet.*` **台词**，无专属立绘）* |
| 眨眼 | `wink` | *（资产已备，**状态机未接入**）* |
| 日常系列 | `daily-*`（咖啡/伸懒腰/吃东西…） | 目前仅 `WorkState::Game` → `daily-gaming` 与深夜 `daily-pajama`；待机小剧场用 `teasing` |
| 工作立绘池 | `work-*`（13 张，**P8 起接入**） | busy 且非编程族（Reading / Meeting）工作时常驻，每 60s 轮转一张（见 §1.3） |
| 节日换装 | `festival-spring` / `festival-mid-autumn` / `festival-halloween` / `festival-christmas` / `valentine` | **静息态**按当日日期自动切换（见 §5.1） |
| 互动分区 | `react-head` / `react-belly` / `react-tail` | 点击不同部位（`Fx::None`） |
| 表情梗 | `meme-*` | 关键词命中（关键词表情共 **21 项**，其中 `meme-*` **10 项**；见 `CHAT.md` §4） |

> **「资产已备但未接入」说明**：`failure` / `celebrate` / `greet` / `wink` / `sleep`
> 等姿态只存在于 `src/core/PoseNames.h` 的**资产清单**与 `assets/poses/` 中，
> **当前没有任何代码路径会输出它们**（`PetStateMachine` 实际输出见上表右列）。
> 保留在清单里是为了「资产可寻址 + 后续可接入」，不代表已生效。
>
> **P8 起状态变化**：`running`（编程常驻）、`night`（傍晚 / 深夜唤醒）、
> `daily-pajama`（深夜空闲）与 13 张 `work-*`（工作立绘池）**均已接入**；
> `sleep` 反而**退出**了输出路径（深夜空闲立绘改为 `daily-pajama`）—— 见 §1.2 / §1.3。

完整清单与命名以 `assets/poses/` 的 **93 张 webp** 为准（见 `PRESENTATION.md` §1）。

> 其中 4 张为**贴边立绘**（`home-peek` / `home-bottom` / `settings-peek` / `workbench-peek`）：
> 它们**不是状态机姿态**，而是由「桌宠窗口是否贴合桌面四条边框」驱动的**表现层**效果
> （见 `PRESENTATION.md` §3.1）。状态机**不会**输出这几个 pose，贴边期间也不打断 / 不占用
> 一次性姿态——离开边框后立绘立即回到状态机此刻应有的姿态。
> 反向的一条表现层约束：**贴边期间 `PoseView` 丢弃拖动姿态**（`setPose` 在「拖动中 + 已贴边」时
> 不换图），状态机照常输出 `pick-up`，只是不参与显示——避免拖动把探头立绘顶掉。

### 1.1 工作 / 未工作（busy / calm）对齐参考项目

参考项目用 `BUSY_STATES = { thinking, tool, success, failure }` 做两分法：**忙时情绪立绘
（含节日）一律让位，未工作才允许回落静息**。本项目以 `core::workStateIsBusy()` 显式承载该分类：

| 类别 | 本项目 `WorkState` | 参考项目对应态 | 立绘 |
|---|---|---|---|
| 工作（busy） | `Reading` | `thinking` | `thinking` |
| 工作（busy） | `Coding` / `VibeCoding` | `tool`（+ 工具细分） | `work-ram` / `meme-wakuwaku` |
| 工作（busy） | `Debugging` | `tool`（调试细分） | `work-debug` |
| 工作（busy） | `Meeting` | `tool`（会议/写作细分） | `work-meeting` |
| 未工作（calm） | `Unknown` | —（无感知数据） | 上下文态（时段 / 挂机 / 静息） |
| 未工作（calm） | `Idle` | `idle` | **`idle-cute`**（原 `waiting`，见下） |
| 未工作（calm） | `Browsing` | `curious` | `curious` |
| 未工作（calm） | `Game` | 日常小剧场（`daily-gaming`） | `daily-gaming` |
| 未工作（calm） | `Afk` | `afk` | `afk` |

- **对齐项（本次变更）**：`WorkState::Idle` 的立绘由 `waiting` 改为 `idle-cute` —— 参考项目
  `idle → idle-cute`，且 `waiting` 在参考项目中已不再触发（`signals.waiting` 恒为 false）。
  同时状态机把 `Idle` 归入静息链（`workStatePose(Idle) == kDefaultPose` 时不提前返回），
  使「在电脑前但未产出」也能参与节日换装。
- **保留项**：`Coding → work-ram` / `VibeCoding → meme-wakuwaku` 是本项目对参考项目
  `running` + 工具细分（`TOOL_POSES`）的具象化，语义一致、表达更细，故保留；
  二者均为 busy，节日换装不会介入。
- 专注态静默（`workStateIsFocus`）与 busy 分类**互不替代**：前者管「主动台词」，后者管「立绘让位」。

### 1.2 时段常驻立绘（P8，需求 R1 / R2）

时段划分与每段空闲立绘由纯函数 `core/DaySlotRules.h` 给出（零 Qt、可脱界面单测）：

| 时段 | 小时 | 空闲常驻立绘 | 说明 |
|---|---|---|---|
| 日间 `DaySlot::Day` | 07:00–17:59 | `idle-cute` | **不接管**上下文链：仍走挂机（`thinking`/`afk`）、静息第二档（`waiting`）与节日换装 |
| 傍晚 `DaySlot::Evening` | 18:00–22:59 | `night` | 时段态优先于挂机态与游戏陪玩态 |
| 深夜 `DaySlot::LateNight` | 23:00–06:59 | `daily-pajama` | **最高优先级独立阶段**（覆盖工作态等一切常驻态）；被交互唤醒时改显 `night` |

**深夜唤醒窗口**（R2）：

- 任何 `touchInput()` 路径（点击 / 拖拽 / 喂食 / 关键词命中 / 外部播报）都会把
  `m_lateNightAwakeUntilMs` 置为「当前时刻 + `kLateNightAwakeMs`（60 000 ms）」；
- 窗口内 → 常驻立绘为 `night`；到期无操作 → 自动切回 `daily-pajama`（窗口内再交互则**重新计时**）；
- 窗口由 `Tick` 驱动判定（与其余上下文态同频，无额外定时器）。

**深夜独立阶段**（2026-10-04）：

深夜（23:00–06:59）的立绘是**封闭集合**，只由时段态决定，**不参与任何随机立绘池**：

| 优先级 | 立绘 | 触发 |
|---|---|---|
| 1 | `meme-smile-pain` | 深夜点击**累计 ≥ 10 次** → 虚弱，保持 `kLateNightWeakHoldMs`（20s）+ `click.latenight.weak` 台词；触发后计数清零（每满 10 次一次） |
| 2 | `night` | 交互唤醒窗口（`kLateNightAwakeMs`，60s）内 |
| 3 | `daily-pajama` | 空闲常驻 |

- 深夜**不**发生：待机小剧场随机池、睡眠循环（`sleep` ↔ `daily-stretch`）、逗弄（`teasing`）、满值摇尾（`tail-swing`）；
- 深夜**保留**：点击分区反馈（`react-head` / `react-belly` / `react-tail` / `curious`）、一次性事件（升级 / 成就等）；
- 计数与虚弱窗口在**离开深夜或 `reset()` 时清空**（跨时段不残留）。

**虚弱期间不再响应鼠标**（2026-10-04）：

一旦进入虚弱窗口（20s），角色「装死」，**一切鼠标交互被整体忽略**：

| 层 | 行为 |
|---|---|
| 状态机 `PetStateMachine::handle()` | `Click` / `TripleClick` / `DragStart` / `DragEnd` / `Feed` / `Tease` / `Praise` 直接返回当前态 —— 不换立绘、不播台词、不累加点击计数，**也不刷新唤醒窗口**（因此 20s 后直接回睡衣，而非被点击续期） |
| `PetController` | `handleClick` / `handleDragBegin` / `handleDragEnd` / `handleMenuAction` 返回 `false`（被拒绝），不调状态机、**不施加减值 / 不涨养成**、不广播 `interactionOccurred`；`lateNightWeak()` 供 View 查询 |
| `PetWindow` | 左键按压整体作废（**不拖窗**、不进拖拽态）、`handleClick` 返回 `false` 时**不播点击反馈动画**、文件拖入投喂直接 `ignore()`（在「入胃」之前拒绝） |

> 右键菜单**保留**（退出 / 设置入口，不属于「角色响应」）；
> `Tick` 不受影响 —— 虚弱窗口照常到期回落到 `night`（唤醒窗口内）或 `daily-pajama`。

> **台词要求**：`click.latenight.weak` 的文案必须同时包含「虚弱感」与「**对主人的关怀**」
> （虚弱时仍惦记主人熬夜）—— 见 `assets/lines/lines.txt`。

**优先级链**（`PetStateMachine::contextPose()`，2026-10-04 二次修订）：

```
一次性事件（点击 / 拖拽 / 升级 …）
  > 深夜独立阶段（最高；覆盖一切常驻态：weak > night > daily-pajama）
    > 工作态（编程 running / busy 池 / 专属立绘；Idle 归静息）
      > 睡眠循环（日间 / 傍晚，待机 ≥ 20min）
        > 时段态（傍晚 night）
          > 满值特殊常驻（tail-swing；2026-10-04 起移到时段态之后，见下）
            > 挂机态（afk / thinking）
              > 游戏陪玩态（EX1.4）
                > 静息态（节日换装 → idle-cute / waiting）
```

> **深夜最高优先级（2026-10-04 需求）**：深夜不再让位给工作态 —— 编程 / 会议等只更新
> 内部状态，不切立绘、也**不播报** `work.*`（否则 `work-*` 立绘会闪现一帧）。
> 回归用例：`test_work_state::unknownWorkStateKeepsLegacyBehavior`、
> `test_state_machine::lateNightIsIndependentStage`。

> **满值常驻的让位（2026-10-04 修复）**：`tail-swing`（心情 & 饱腹同时满）此前位于时段态**之前**，
> 导致满值时深夜永不显示睡衣、傍晚也不显示 `night`。现已下移到时段态之后 ——
> **时段态优先于满值常驻**，`tail-swing` 只在日间（时段态不接管时）生效。
> 回归用例：`test_state_machine::vitalsFullYieldsToTimeSlots` / `lateNightIsIndependentStage`。

> 注意：**深夜立绘优先于挂机态** —— 深夜长时间无输入仍显示睡衣，不会退化成 `afk`
> （睡衣本身就表达「该睡了」，比「离开」更贴语义）。日间则无此分支，挂机链照常生效。
> `PetStateMachine::isNight()`（23:00–05:59）**口径不变**，仍只用于「主动台词静默」。

### 1.3 编程常驻与工作立绘池（P8，需求 R3）

| 工作态 | 常驻立绘 | 来源 |
|---|---|---|
| `Coding` / `VibeCoding` / `Debugging` | `running` | `core::workStateIsCoding()` |
| `Reading` / `Meeting` 等 busy 非编程族 | `work-*` 立绘池当前张 | `core::WorkPosePool` |
| `Idle` | 交回静息链（`idle-cute` / 节日换装） | `contextWorkPose()` 返回 `nullptr` |
| `Browsing` / `Game` / `Afk` | 各自专属立绘（`curious` / `daily-gaming` / `afk`） | `workStatePose()` |
| `Unknown`（无感知数据） | **完全跳过**（零回归） | — |

**工作立绘池**（`src/core/WorkPosePool.{h,cpp}`）：

- 成员：13 张 `work-*`（`work-ram` / `work-idea` / `work-review` / `work-debug` /
  `work-deploy` / `work-deadline` / `work-boss` / `work-slack` / `work-slack-phone` /
  `work-meeting` / `work-sleep` / `work-celebrate` / `work-pat`）；
- 节奏：**每 60s 换一张**（`kWorkPoseDwellMs`）；进入工作态时立即取一张（不等 60s）；
  按**轮转**（round-robin）而非纯随机，且维护「最近 3 张」窗口 —— 一轮内不重复、跨轮也不紧邻重复；
- **热词联动**：`speak()`（关键词命中 / 小游戏播报走这条）与 `EventType::KeywordHit` 都会
  把命中的 `work-*` 立绘 `note()` 进「最近」，使随后的轮转**避开刚出现过的这一张**；
- **ACP 联动**：ACP 显式信号经 `WorkStateService::applyExternalState` 覆盖工作态后，
  仍以 `EventType::WorkStateChanged` 进入状态机 → 池节奏重置并在本帧取新张（见 `CONTEXT-API.md` §6）；
- **播报与常驻同源**：工作态「显著变化时播报一句」用的立绘与常驻立绘共用
  `contextWorkPose()`，避免「编程时先闪一下 work-ram 再变 running」（见 `traps-P8.md` TRAP-P8-004）。

## 2. 时间窗口与概率常量（沿用 whale 取值）

| 常量 | 值 | 含义 |
|---|---|---|
| `AFK_MS` | 180000（180s） | 无输入进入 `afk` |
| `SPEECH_GAP_MS` | 6000（6s） | **主动说话**两条之间的最小间隔（用户交互台词不参与节流） |
| `SUCCESS_WINDOW_MS` | 2000（2s） | 成功态保持窗口 |
| `CURIOUS_WINDOW_MS` | 6000（6s） | 好奇态保持窗口 |
| `TEASE_CHANCE` | 0.006 | 每次 tick 触发逗弄概率 |
| `kLateNightAwakeMs`（P8） | 60000（1 min） | 深夜被交互后保持「醒着」（`night`）的时长，到期回 `daily-pajama` |
| `kLateNightWeakClickCount`（2026-10-04） | 10 | 深夜点击累计阈值 → 触发 `meme-smile-pain`（虚弱）并清零计数 |
| `kLateNightWeakHoldMs`（2026-10-04） | 20000（20s） | 深夜「虚弱」立绘保持时长 |
| `kWorkPoseDwellMs`（P8） | 60000（1 min） | 工作立绘池的轮转节奏（每 60s 换一张 work-*） |

## 3. 输入事件

| 事件 | 来源 | 影响 |
|---|---|---|
| `Tick` | 定时器（如 200ms） | 概率事件、时间推进 |
| `Click(zone)` | PoseView 命中分区（head/belly/tail/body） | 互动姿态 + 台词；**深夜**同时累计点击次数，达 10 次 → `meme-smile-pain`（虚弱 20s） |
| `TripleClick` | 三连击检测 | `star` + 粒子特效 |
| `DragStart/DragEnd` | PetWindow | `pick-up` 立绘 + 惯性 |
| `Feed` / `Tease` / `Praise` | 右键菜单 | 投喂 `eat` / 生气 `angry` / 夸夸 `blush`+爱心 |
| `LevelUp` / `AchievementUnlocked` / `QuestDone` | GrowthService | `levelup` / `achievement` / `success` |
| `IdleTimeout` | AFK 计时 | `afk` / `sleep`（P8：深夜分支为 `daily-pajama`） |
| `Clock(hour)` | 系统时间 | 时段判定 → 傍晚 `night`、深夜 `daily-pajama`（唤醒窗口内为 `night`；虚弱窗口内为 `meme-smile-pain`）（P8，见 §1.2） |
| `Clock(hour)` / `Tick` 的 `nowMs` | 系统墙钟（`QDateTime::currentMSecsSinceEpoch`） | **静息换装的日期来源**：按事件时间戳的本地自然日查节日表（见 §5.1） |
| `KeywordHit(kw)` | `PetController::handleKeywordHit`（经 `speak()` 播报，P5/P6） | `meme-*` 表情 |
| `WorkStateChanged(state)` | EnvironmentService → WorkStateService（P7） | 工作态通道：切立绘 + 播报一句 `work.*`；Unknown = 退出工作态 |

### 3.1 工作态通道（P7，详见 `PLUGIN-ARCHITECTURE.md` §6.2）

- 状态集合与判据在 `src/core/WorkState.h` / `WorkStateRules.h`（**零 Qt 纯逻辑**）；
  立绘与台词场景映射集中在 `WorkState.cpp` 的 `workStatePose()` / `workStateScene()`。
- **默认 `WorkState::Unknown`（无感知数据）时完全跳过工作态分支**，行为与 P6 一致（零回归）。
- **会话锁定 / 屏保（`systemPaused`）视为「有数据」并优先判 `Afk`**：锁屏时前台窗口读不到，
  数据形状与「未启用感知」相同，若先判「无数据」会把「确定离开」误降级为 `Unknown`
  （P7.1，见 `traps-P7.md` TRAP-P7-006）。
- 优先级插入位置（2026-10-04 二次修订）：**一次性事件 > 深夜独立阶段（最高）> 工作态 > 睡眠循环 > 时段态（傍晚 night）> 满值常驻 > 挂机态 > 默认**。
- P8 起工作态的**立绘细分**见 §1.3（编程 `running` / busy 池轮转 / 热词与 ACP 联动）。
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
- 优先级：一次性事件（点击/升级）> **深夜独立阶段**（最高）> 工作态 > 睡眠循环 > 时段态（傍晚）> 满值常驻 > 挂机态（afk/thinking）> 默认（idle）。
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
7. **静息换装（新增）**：**仅静息态**按当日日期换成节日立绘；工作态（busy）、浏览 / 游戏 / 离开、
   深夜睡眠与思考各有专属立绘，不换装 —— 与参考项目「忙时情绪（含节日）让位」一致（见 §5.1）。

### 5.1 静息换装（节日立绘）

**节日范围（5 个，与参考项目 `festivalKey()` 逐条一致）**：

| 节日 | 日期口径 | pose key | 资源 |
|---|---|---|---|
| 春节 | 农历新年（**小表**驱动，逐年补充） | `festival-spring` | `dsh-whale-state-festival-spring.webp` |
| 中秋 | 农历八月十五（**小表**驱动，逐年补充） | `festival-mid-autumn` | `dsh-whale-state-festival-mid-autumn.webp` |
| 万圣节 | 公历 **10-31**（不受年份限制） | `festival-halloween` | `dsh-whale-state-festival-halloween.webp` |
| 圣诞节 | 公历 **12-25**（不受年份限制） | `festival-christmas` | `dsh-whale-state-festival-christmas.webp` |
| 情人节 | 公历 **02-14**（不受年份限制） | `valentine`（资产名**不带** `festival-` 前缀） | `dsh-whale-state-valentine.webp` |

- 判定实现：纯函数 `core::festivalPoseOf(nowMs)`（`src/core/FestivalRules.h`，零 Qt、可脱界面单测），
  按**本地时区**自然日查表；非节日返回 `nullptr`。农历日期表 `kFestivalDays` 现含
  2026 / 2027 两年的春节与中秋；表内没有的年份只是「不换装」，不会误判成别的节日。
- **资源零新增**：5 张节日立绘**早已随 93 张清单落地**（`assets/poses/` + `PoseNames.h` + `assets.qrc`），
  本功能只是让它们**可达**（此前 `festival-*` / `valentine` 无任何代码路径输出）。

**静息态判定条件**（`PetStateMachine::contextPose()` 的最后一档）：

1. 非工作 busy 态：`m_workState` 不在 `workStateIsBusy()` 的忙集合内
   （`WorkState::Idle` 映射为默认待机立绘，视为静息，**参与**换装）；
2. 时段为**日间**：`daySlotOf(m_hour) == DaySlot::Day`
   （傍晚 → `night`，深夜 → `daily-pajama` / 唤醒窗口内 `night`，两者都优先于静息链，见 §1.2）；
3. 未进入思考 / 离开：`nowMs - m_lastInputMs < kThinkingMs`（≥ 时依次为 `thinking` / `afk`）；
4. 无一次性姿态、未拖拽（由 `handle()` 的既有优先级保证：一次性事件 > 工作态 > 时段态 > 挂机态 > 静息）。

满足以上后，命中节日 → 返回节日立绘；否则返回默认待机链（`idle < kWaitingMs` → `idle-cute`，
否则 `waiting`）。**`idle-cute` 与 `waiting` 两档都换装**（二者同属「未工作且未挂机」的静息区间），
`thinking`（思考）不换装。

**触发与切换规则**：

| 项 | 规则 |
|---|---|
| 触发时机 | 每 tick（`Tick` / `Clock` 事件）在静息态重新求值 → 跨零点会自动换装 / 脱下，无需额外定时器 |
| 与一次性姿态 | 点击 / 拖拽 / 投喂 / 关键词表情等一次性姿态优先；到期回落时**自动恢复**节日立绘 |
| 与工作态 | busy 态硬优先（`work-ram` / `thinking` / `work-debug` / `work-meeting` …），节日让位；`Idle` 属未工作 → 换装 |
| 与时段态（P8） | 傍晚 `night` / 深夜 `daily-pajama`（唤醒窗口内 `night`）优先，不换装 |
| 与挂机 | `afk` / `thinking` 优先，不换装 |
| 与贴边 | 贴边是表现层行为（`PRESENTATION.md §3.1`），贴边期间显示探头立绘，离开边框后恢复「此刻应有的姿态」（含节日） |
| 不换装时的兜底 | 非节日 / 表未覆盖年份 → 与 P7 之前完全一致（`idle-cute` / `waiting` / …），零回归 |

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
| 节日换装 | `festivalKey()` 一次判断，命中则 `showMood(pose, 7000)` **每日闪现 7 秒** | 节日立绘即**静息态立绘**（整日生效，跨零点自动换 / 脱），并显式以 `workStateIsBusy()` 表达「忙时让位」（见 §5.1） |
| 工作 / 未工作分类 | `BUSY_STATES = {thinking, tool, success, failure}`（内联字面量） | `core::workStateIsBusy()`（具名纯函数、可单测），`Idle` 对齐参考 `idle → idle-cute`（见 §1.1） |
