# 小游戏：扫雷（MINIGAME-INTERFACE）

> 本文件原为「戳泡泡」小游戏的**预留接口**设计（P6 曾评估为不实现）。
> **现已实现**：预留的「戳泡泡」被**替换为扫雷**，并保留参考项目进行游戏时
> 「鲸鱼娘立绘变化 + 台词播报」的表现。
>
> 文件名沿用历史名称（代码与文档多处引用），**内容以本文为准**。

---

## 1. 交付概览

| 项 | 内容 |
|---|---|
| 玩法 | 经典扫雷：左键翻格、右键插旗，数字表示相邻雷数，翻开所有非雷格即通关 |
| 难度 | 内置 3 个预设 + **自定义尺寸与雷数** |
| 入口 | 右键菜单 / 托盘菜单「小游戏：扫雷」（受 `minigame_enabled` 门控）；设置面板「小游戏」页「开始扫雷」 |
| 表现 | 开局 / 连翻 / 踩雷 / 通关 / 失败各切一次立绘并播报台词（`game.*`） |
| 结算 | 一局结算上报成就（7 项小游戏成就已点亮） |
| 依赖 | **零新增依赖**：Qt Widgets 自绘 + `core::Minesweeper` 纯逻辑 |

## 2. 难度预设与自定义

内置预设（规格即需求）：

| 预设 | 网格 | 雷数 |
|---|---|---|
| 初级 | 9 × 9 | 10 |
| 中级 | 16 × 16 | 40 |
| 高级 | 30 × 16 | 99 |
| 自定义 | 宽 5–30 × 高 5–24 | 1 ~（格数 − 1） |

- 约束：至少留 1 个非雷格，保证「首点安全」成立。
- 界面**实时展示当前难度与参数**（如「当前难度：初级 · 9×9 · 10 雷」）。
- 支持在**预设与自定义之间切换**：选择预设即时开新局；切换到「自定义…」后展开
  宽 / 高 / 雷数输入框，点「开始自定义」校验并开局；参数非法时弹窗提示且不开局。
- 上次选择（预设或自定义参数）落库 `settings.json_ext`，下次打开沿用。

## 3. 纯逻辑规格（`src/core/Minesweeper.h`）

- **首点安全**：首次翻开的格子永不布雷——布雷延迟到首点之后进行，并在候选池中排除该格。
- 布雷：对「除首点外的所有格」做部分 Fisher–Yates，随机源为 `core::IRandom`（可注入，便于确定性单测）。
- 翻格：0 邻格触发**连通区展开**（flood reveal）；已翻开 / 已插旗的格子不再响应。
- 插旗：仅对未翻开的格子生效；`remainingMines = 总雷数 − 插旗数`。
- 胜负：翻开全部非雷格 → `Won`；踩雷 → `Lost` 且揭示全部雷（不改变判定）。
- 连翻：连续安全翻开的格数累加，踩雷清零；`maxChain` 保留本局峰值（用于成就与播报）。
- 结算 `MineSummary`：`won` / `perfect`（通关时插旗数 == 雷数，即**全对插旗**）/ `maxChain` / `mineCount`。

## 4. 与养成 / 成就的衔接

- **立绘 + 台词**（`PetController::presentGame`，`proactive=false`，不受深夜静默与节流限制）：

  | 时机 | 立绘 pose | 台词场景 |
  |---|---|---|
  | 开局 | `game-think` | `game.start` |
  | 连翻达标（每局一次） | `game-happy` | `game.chain` |
  | 踩雷 | `game-cheat` | `game.boom` |
  | 通关 | `game-win` | `game.win` |
  | 失败结算 | `game-lose` | `game.lose` |

  台词语料见 `assets/lines/game.txt`（外部资源，不硬编码进 C++）。

- **成就**：`AchievementService::reportMiniGame(won, expert, perfect, maxChain)`
  写 `meta.stat.mg_*` 计数器并判定，7 项小游戏成就由「预留」变为**可解锁**：

  | 成就 | 指标 | 阈值 |
  |---|---|---|
  | `game-first` 初次开玩 | `stat.mg_plays` | 1 |
  | `game-win` 首战告捷 | `stat.mg_wins` | 1 |
  | `game-combo10` 连翻达人 | `stat.mg_max_chain` | 10 |
  | `game-highscore` 高手认证 | `stat.mg_expert_wins` | 1 |
  | `game-play10` 十局纪念 | `stat.mg_plays` | 10 |
  | `game-perfect` 零失误 | `stat.mg_perfect` | 1 |
  | `game-daily3` 三局全清 | `stat.mg_plays_today` | 3 |

  单日局数按 `meta.stat.mg_day` 记录的自然日跨天清零（重启后同样生效）。

- **养成奖励**（`MiniGameService::settle`，数值照搬参考项目 `applyGrowth` 的 `game-*` 分支）：

  | 档位 | 判定 | 心情 | 好感 |
  |---|---|---|---|
  | 通关 | `won` | +8 | +12 |
  | 及格 | 未通关但已翻开 ≥ 半数非雷格 | +2 | +3 |
  | 失败 | 其余 | −3 | 0 |
  | 刷新纪录 | 该难度个人最快通关用时被刷新（可叠加在上面三档） | — | +5 |

  - **每日上限 3 局**（照搬 `GAME.REWARDS_PER_DAY`）：局数照记，超出只计分不发奖；
    「刷新纪录」的加成同样受该上限约束（与参考项目 `settleGame` 一致）。
  - 奖励增量经 `GrowthService::grantReward` 落库，复用既有的夹取 / 升级 / 羁绊 / 落盘链路。
  - **成就计数不受每日上限约束**（与参考项目一致：成就独立判定）。
  - 个人最快纪录是按难度分桶的长期数据（`meta` 表 `game.best_ms_<preset>`），跨天不清零；
    每日奖励局数按 `game.reward_day` 跨天清零。

## 5. 约束

- 不引入任何第三方库；棋盘与交互均为 Qt Widgets。
- 界面外观只走全局样式表（`resources/qt-ui/default.qss` + `project.qss`），
  C++ 中**不写颜色字面量**；棋盘格状态经动态属性 `cellState` 由 `project.qss` 表达。
- 棋盘逻辑零 Qt 依赖，可脱离界面单测。

## 6. 实现落位

| 交付项 | 代码位置 |
|---|---|
| 纯逻辑（预设 / 校验 / 布雷 / 翻格 / 插旗 / 胜负 / 连翻） | `src/core/Minesweeper.{h,cpp}` |
| 棋盘控件 + 难度配置与展示 | `src/view/MinesweeperDialog.{h,cpp}` |
| 立绘 / 台词广播 | `viewmodel::PetController::presentGame` |
| 台词语料 | `assets/lines/game.txt`（`assets/assets.qrc` 登记） |
| 成就指标与服务上报 | `src/core/Achievements.h`、`viewmodel::AchievementService::reportMiniGame` |
| 结算奖励（档位 / 每日上限 / 个人最快） | `src/viewmodel/MiniGameService.{h,cpp}`、`core/GrowthRules.h` 的 `kGame*` 常量 |
| 难度配置持久化 | `SettingsData` / `SettingsRepo`（`json_ext`：`minigame_preset`、`minigame_custom_*`） |
| 入口与门控 | `PetWindow::showMiniGame` + `applySettings`（`minigame_enabled`） |
| 棋盘样式 | `resources/qt-ui/project.qss`（`#MineBoard`） |
| 单测 | `tests/test_minesweeper.cpp`（纯逻辑 + 档位判定）、`tests/test_minigame.cpp`（结算奖励与上限）、`tests/test_content.cpp`（成就上报） |

## 7. 验证

- `ctest -C Debug` / `-C Release` 各 **11/11 通过**（新增 `test_minesweeper`、`test_minigame`）。
- 未删除任何断言、未注释失败用例、未放宽比较条件。
- 人工目视项（待用户复验）：棋盘可读性、右键插旗手感、立绘与台词播报节奏、
  预设 / 自定义切换与难度文案展示。

## 8. 变更记录

- **本期（第一版）**：将原「戳泡泡」预留玩法**替换为扫雷**；`AchMetric::MiniGameReserved`
  更名为 6 个真实小游戏指标；`minigame_enabled` 默认由「关」调整为「开」
  （小游戏已实现，开启可获得入口；仍可关闭以隐藏入口）。
- **本期（追加）**：补上**养成数值奖励**（通关 +8/+12、及格 +2/+3、失败 −3、刷新纪录 +5，
  每日 3 局上限，见 §4）；应用图标改用 `assets/icon/whalepet.ico`；版本升至 0.2.0。
