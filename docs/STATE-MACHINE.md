# 状态机设计（STATE-MACHINE）

> 移植来源：`referances/dsh-whale-musume/assets/whale-moe-core.js`（纯函数、DOM-free）。
> 移植原则：**保持纯逻辑、零 Qt UI 依赖**，可脱离界面单测。

## 1. 状态集合（Pose）

| 语义 | pose 名 | 触发场景 |
|---|---|---|
| 待机 | `idle-cute` | 默认空闲 |
| 等待 | `waiting` | 等待用户操作 |
| 思考 | `thinking` | 思考/长时间无输入 |
| 工作 | `running` | 通用忙碌（本项目的「陪伴中」） |
| 成功 | `success` | 任务/成就达成 |
| 失败 | `failure` | 失败/受挫 |
| 好奇 | `curious` | 点击/悬停 |
| 逗弄 | `teasing` | 随机小动作 |
| 离开 | `afk` | 挂机超时 |
| 脸红 | `blush` | 摸头/夸夸 |
| 生气 | `angry` | 戳一下 |
| 吃 | `eat` | 投喂 |
| 星星眼 | `star` | 三连击 |
| 庆祝 | `celebrate` | 升级/里程碑 |
| 睡眠 | `sleep` | 深夜/长时间挂机 |
| 问候 | `greet` | 启动/回来 |
| 夜晚 | `night` | 深夜时段 |
| 眨眼 | `wink` | 随机 |
| 日常系列 | `daily-*`（咖啡/伸懒腰/吃东西…） | 待机随机小剧场 |
| 互动分区 | `react-head` / `react-belly` / `react-tail` | 点击不同部位 |
| 表情梗 | `meme-*`（13 种） | 关键词命中 |

完整清单与命名以 whale `assets/generated/` 的 92 张 webp 为准（见 `PRESENTATION.md`）。

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
| `TripleClick` | 三连击检测 | `star` + 庆祝特效 |
| `DragStart/DragEnd` | PetWindow | `pick-up` 立绘 + 惯性 |
| `Feed` / `Tease` / `Praise` | 右键菜单 | 投喂/生气/夸夸 |
| `LevelUp` / `AchievementUnlocked` / `QuestDone` | GrowthService | 庆祝/成功 |
| `IdleTimeout` | AFK 计时 | `afk` / `sleep` |
| `Clock(hour)` | 系统时间 | 问候/夜晚判定 |
| `KeywordHit(kw)` | ChatService | `meme-*` 表情 |

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

1. **互斥**：同一时刻仅一个主姿态；一次性姿态到期后回落到「上下文默认态」。
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
