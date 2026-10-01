# 小游戏：插件化接入机制与扫雷（MINIGAME-INTERFACE）

> 本文件最初是「戳泡泡」小游戏的**预留接口**设计（P6 曾评估为不实现），随后
> 「戳泡泡」被**替换为扫雷**。本期进一步把整个小游戏模块**重构为插件化接入机制**：
> 扫雷成为第一个按插件规范接入的小游戏，新增小游戏无需修改核心逻辑。
>
> 文件名沿用历史名称（代码与文档多处引用），**内容以本文为准**。

---

## 1. 交付概览

| 项 | 内容 |
|---|---|
| 玩法 | 经典扫雷：左键翻格、右键插旗，数字表示相邻雷数，翻开所有非雷格即通关 |
| 难度 | 内置 3 个预设 + **自定义尺寸与雷数**（由扫雷插件提供） |
| 入口 | 右键 / 托盘菜单的**「小游戏…」子菜单**（悬停展开、离开收起；支持键盘导航与点击展开），列表按已注册插件动态生成；设置面板「小游戏」页的「开始××」。受 `minigame_enabled` 统一门控 |
| 接入方式 | **插件化**：实现 `IMiniGamePlugin` + `MiniGameView`，在 `registerBuiltinMiniGames()` 注册一行 |
| 表现 | 开局 / 连翻 / 踩雷 / 通关 / 失败各切一次立绘并播报台词（`game.*`） |
| 结算 | 一局经通用契约 `core::MiniGameResult` 上报：档位奖励 + 每日上限 + 个人最快 |
| 成就 | 一局结算上报成就（7 项小游戏成就可解锁） |
| 依赖 | **零新增依赖**：Qt Widgets 自绘 + `core::Minesweeper` 纯逻辑 |

---

## 2. 插件化接入机制

### 2.1 分层与职责

| 层 | 文件 | 职责 |
|---|---|---|
| 通用契约（零 Qt） | `src/core/MiniGameTypes.h` | `GameGrade` / `MiniGameResult` / `gameGrade()`，所有插件与宿主之间的**唯一数据契约** |
| 插件接口（Qt Widgets） | `src/minigame/MiniGamePlugin.h` | `MiniGameContext`（宿主注入依赖）、`MiniGameInfo`（元数据）、`MiniGameView`（窗口基类）、`IMiniGamePlugin`（插件接口） |
| 注册表 | `src/minigame/MiniGameRegistry.{h,cpp}` | 装载内置插件；`registerBuiltinMiniGames()` 是**唯一注册点** |
| 具体插件 | `src/minigame/<game>/` | `XxxPlugin`（元数据 + 视图工厂）+ `XxxView`（界面）；玩法逻辑放 `core/`（零 Qt） |

宿主（`PetWindow` / `SettingsDialog`）与结算服务（`MiniGameService`）**只按接口驱动**，
不包含任何具体玩法的分支或字段。

### 2.2 新增一个小游戏（4 步）

1. **纯逻辑**：在 `src/core/` 实现玩法规则（零 Qt、可脱 UI 单测），把对局结果折算为
   `core::MiniGameResult`（`won` / `perfect` / `expert` / `maxChain` / `progress*` / `elapsedMs`）。
2. **界面**：继承 `MiniGameView`，实现 `reload()` / `setRewardText()`，一局结束时
   `emit gameFinished(result)`。
3. **插件**：继承 `IMiniGamePlugin`，提供 `info()`、`createView()`；可选实现
   `configSummary()`（设置页「上次配置」摘要）与 `legacyBestRecords()`（旧纪录迁移）。
4. **注册**：在 `MiniGameRegistry.cpp` 的 `registerBuiltinMiniGames()` 追加一行
   `registry.add(std::make_unique<XxxPlugin>());`。

完成以上步骤后，**菜单入口、设置页展示、结算链路、成就上报全部自动生效**，宿主无需改动。

### 2.3 宿主如何驱动插件

- `PetWindow::setupMiniGames()` → `registerBuiltinMiniGames(m_miniGames)`（必须早于构建菜单）。
- 右键 / 托盘菜单：各放一个「小游戏…」子菜单（`QMenu::addMenu`），子项按注册表以
  `MiniGameInfo::menuLabel` 生成，统一落到 `PetWindow::showMiniGame(pluginId)`；
  **悬停展开 / 离开收起 / 方向键导航 / 点击展开均由 `QMenu` 原生提供**，宿主不自行实现弹层；
  两个入口项（子菜单的 `menuAction()`）由 `minigame_enabled` 统一门控。
  父菜单与子菜单共用 `default.qss` 的 `QMenu` 规则，`project.qss` 仅补齐列表最小宽度。
- `showMiniGame(pluginId)`：`registry.find(id)` → `createView(ctx)`（按 id 懒创建并缓存，
  插件间互不影响）→ `reload()` → 显示。
- 结算：所有插件共用 `connect(view, &MiniGameView::gameFinished, this, &PetWindow::settleMiniGame)`；
  `settleMiniGame()` 依次完成「养成奖励 → 成就上报 → 结算文案回填」。
- 设置页：按注册表动态列出每个插件（显示名 / 上次配置 / 玩法说明 / 开始按钮），
  发出 `openMiniGameRequested(pluginId)`。

### 2.4 通用结算契约 `core::MiniGameResult`

| 字段 | 含义 |
|---|---|
| `gameId` | 插件稳定标识（如 `minesweeper`），用于纪录分桶与文案回填 |
| `difficultyId` | 难度稳定标识（如 `beginner` / `custom`），用于纪录分桶 |
| `difficultyLabel` | 难度展示文案（可选） |
| `won` / `perfect` / `expert` | 是否通关 / 完美通关 / 高难档（成就判定用） |
| `maxChain` | 连击 / 连翻峰值 |
| `progressDone` / `progressTotal` | 进度分子 / 分母，用于「及格档」判定 |
| `elapsedMs` | 本局用时（毫秒），用于个人最快纪录 |

档位判定（`gameGrade()`，纯函数）：通关 → `Win`；未通关但进度过半 → `Draw`；否则 `Lose`。
扫雷侧由 `core::mineGameResult(summary, presetIndex, elapsedMs)` 完成折算。

### 2.5 奖励与纪录的通用化

- **每日上限**：`GAME.REWARDS_PER_DAY = 3`，**所有小游戏共用**同一份每日额度
  （局数照记，超出只计分不发奖）。
- **个人最快**：按「游戏 + 难度」分桶，meta 键 `game.best_ms_<gameId>/<difficultyId>`。
  插件可通过 `legacyBestRecords()` 声明旧版键映射（`game.best_ms_<suffix>` → `difficultyId`），
  服务在加载时一次性迁移，保证升级不丢纪录。
- **成就**：`AchievementService::reportMiniGame(won, expert, perfect, maxChain)` 与玩法无关，
  由 `MiniGameResult` 直接映射。

---

## 3. 难度预设与自定义（扫雷插件）

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
- 上次选择（预设或自定义参数）落库 `settings.json_ext`，下次打开沿用；
  设置页的「上次配置」摘要同样由此生成（`MinesweeperPlugin::configSummary`）。

---

## 4. 纯逻辑规格（`src/core/Minesweeper.h`）

- **首点安全**：首次翻开的格子永不布雷——布雷延迟到首点之后进行，并在候选池中排除该格。
- 布雷：对「除首点外的所有格」做部分 Fisher–Yates，随机源为 `core::IRandom`（可注入，便于确定性单测）。
- 翻格：0 邻格触发**连通区展开**（flood reveal）；已翻开 / 已插旗的格子不再响应。
- 插旗：仅对未翻开的格子生效；`remainingMines = 总雷数 − 插旗数`。
- 胜负：翻开全部非雷格 → `Won`；踩雷 → `Lost` 且揭示全部雷（不改变判定）。
- 连翻：连续安全翻开的格数累加，踩雷清零；`maxChain` 保留本局峰值（用于成就与播报）。
- 结算 `MineSummary`：`won` / `perfect`（通关时插旗数 == 雷数，即**全对插旗**）/ `maxChain` / `mineCount`。
- 折算：`mineGameResult()` 把 `MineSummary + 预设 + 用时` 转为 `core::MiniGameResult`；
  `minePresetId()` 给出难度稳定标识（`beginner` / `intermediate` / `expert` / `custom`）。

---

## 5. 与养成 / 成就的衔接

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

- **养成奖励**（`MiniGameService::settle(MiniGameResult)`，数值照搬参考项目 `applyGrowth` 的 `game-*` 分支）：

  | 档位 | 判定 | 心情 | 好感 |
  |---|---|---|---|
  | 通关 | `won` | +8 | +12 |
  | 及格 | 未通关但已翻开 ≥ 半数非雷格 | +2 | +3 |
  | 失败 | 其余 | −3 | 0 |
  | 刷新纪录 | 该游戏该难度个人最快通关用时被刷新（可叠加在上面三档） | — | +5 |

  - **每日上限 3 局**（照搬 `GAME.REWARDS_PER_DAY`）：局数照记，超出只计分不发奖；
    「刷新纪录」的加成同样受该上限约束（与参考项目 `settleGame` 一致）。
  - 奖励增量经 `GrowthService::grantReward` 落库，复用既有的夹取 / 升级 / 羁绊 / 落盘链路。
  - **成就计数不受每日上限约束**（与参考项目一致：成就独立判定）。
  - 个人最快纪录是按「游戏 + 难度」分桶的长期数据，跨天不清零；
    每日奖励局数按 `game.reward_day` 跨天清零。

---

## 6. 约束

- 不引入任何第三方库；棋盘与交互均为 Qt Widgets。
- 界面外观只走全局样式表（`resources/qt-ui/default.qss` + `project.qss`），
  C++ 中**不写颜色字面量**；棋盘格状态经动态属性 `cellState` 由 `project.qss` 表达。
- 棋盘逻辑零 Qt 依赖，可脱离界面单测。
- 宿主与结算服务**不得**出现具体游戏的分支或字段（新增游戏无需改核心逻辑）。
- 菜单外观只走全局样式表：父菜单与「小游戏…」子菜单共用 `default.qss` 的 `QMenu` 规则
  （黑底白字、悬停反色、1px 边框、同样的项内边距），`project.qss` 仅补列表最小宽度，
  C++ 中不写颜色字面量；子菜单与主菜单统一经 `PetWindow::configurePopupMenu` 装配置顶。

---

## 7. 实现落位

| 交付项 | 代码位置 |
|---|---|
| 通用结算契约（零 Qt） | `src/core/MiniGameTypes.h` |
| 插件接口 / 视图基类 / 宿主依赖注入 | `src/minigame/MiniGamePlugin.h` |
| 插件注册表与内置插件注册 | `src/minigame/MiniGameRegistry.{h,cpp}` |
| 扫雷插件（元数据 / 工厂 / 配置摘要 / 旧纪录迁移） | `src/minigame/minesweeper/MinesweeperPlugin.{h,cpp}` |
| 扫雷界面（棋盘控件 + 难度配置与展示） | `src/minigame/minesweeper/MinesweeperView.{h,cpp}` |
| 扫雷纯逻辑（预设 / 校验 / 布雷 / 翻格 / 插旗 / 胜负 / 连翻 / 折算） | `src/core/Minesweeper.{h,cpp}` |
| 菜单入口与统一结算 | `PetWindow::setupMiniGames` / `configurePopupMenu` / `showMiniGame` / `settleMiniGame` |
| 设置页动态展示 | `SettingsDialog::buildMiniGameTab`（按注册表生成） |
| 立绘 / 台词广播 | `viewmodel::PetController::presentGame` |
| 台词语料 | `assets/lines/game.txt`（`assets/assets.qrc` 登记） |
| 成就指标与服务上报 | `src/core/Achievements.h`、`viewmodel::AchievementService::reportMiniGame` |
| 结算奖励（档位 / 每日上限 / 个人最快 / 旧键迁移） | `src/viewmodel/MiniGameService.{h,cpp}`、`core/GrowthRules.h` 的 `kGame*` 常量 |
| 难度配置持久化 | `SettingsData` / `SettingsRepo`（`json_ext`：`minigame_preset`、`minigame_custom_*`） |
| 入口门控 | `PetWindow::applySettings`（`minigame_enabled`） |
| 棋盘样式 | `resources/qt-ui/project.qss`（`#MineBoard`） |
| 单测 | `tests/test_minesweeper.cpp`（纯逻辑 + 档位判定）、`tests/test_minigame.cpp`（通用结算 / 上限 / 分桶纪录 / 旧键迁移）、`tests/test_content.cpp`（成就上报） |

---

## 8. 验证

- 干净构建（VS 2026 + Qt 6.8.4）`ctest -C Debug` / `-C Release` 各 **11/11 通过**
  （含 `test_minesweeper`、`test_minigame`）。
- 未删除任何断言、未注释失败用例、未放宽比较条件；重构后 `test_minigame` 的断言强度
  与重构前一致，并新增「不同游戏 / 难度纪录互不干扰」「旧版纪录键迁移」两组用例。
- `test_smoke` 新增用例校验「小游戏…」子菜单结构：入口文案为 `小游戏…`、以 `QMenu` 子菜单
  形式挂载（`menuAction()->menu()`，即悬停展开 / 键盘导航 / 点击展开的前提）、插件项文案与可用性。
- 人工目视项（待用户复验）：在「小游戏…」上悬停展开列表、移开自动收起；方向键导航与回车进入
  子菜单；点击父项展开；棋盘可读性、右键插旗手感、立绘与台词播报节奏、预设 / 自定义切换与
  难度文案展示、设置页插件条目与「开始××」。

---

## 9. 变更记录

- **第一版**：将原「戳泡泡」预留玩法**替换为扫雷**；`AchMetric::MiniGameReserved`
  更名为 6 个真实小游戏指标；`minigame_enabled` 默认由「关」调整为「开」。
- **追加**：补上**养成数值奖励**（通关 +8/+12、及格 +2/+3、失败 −3、刷新纪录 +5，
  每日 3 局上限，见 §5）；应用图标改用 `assets/icon/whalepet.ico`；版本升至 0.2.0。
- **本期（插件化重构）**：
  - 新增通用契约 `core::MiniGameTypes`（`MiniGameResult` / `GameGrade` / `gameGrade`），
    结算服务 `MiniGameService::settle()` 改为只接受通用结果，与具体玩法解耦；
  - 新增插件接口 `IMiniGamePlugin` / 视图基类 `MiniGameView` / 注册表 `MiniGameRegistry`，
    宿主（`PetWindow` / `SettingsDialog`）改为按注册表动态驱动（菜单入口与设置页均不再硬编码游戏）；
  - 扫雷迁移为独立插件 `src/minigame/minesweeper/`（`MinesweeperDialog` →
    `MinesweeperPlugin` + `MinesweeperView`），玩法与交互保持不变；
  - 个人最快纪录键改为 `game.best_ms_<gameId>/<difficultyId>`，并提供
    `legacyBestRecords()` 旧键迁移（`game.best_ms_<preset>` 自动继承，升级不丢纪录）；
  - `mv src/view/MinesweeperDialog.* → src/minigame/minesweeper/MinesweeperView.*`，
    CMake 目标 `whalepet_view` 同步纳入 `src/minigame/`。
- **本期（菜单入口）**：
  - 右键 / 托盘菜单不再逐游戏平铺入口，改为单个「小游戏…」子菜单，列表按注册表动态生成；
    悬停展开、离开收起、方向键导航、点击展开全部由 `QMenu` 原生提供，宿主不自行实现弹层；
  - 子菜单与父菜单共用 `default.qss` 的 `QMenu` 规则（`project.qss` 仅补列表最小宽度），
    主菜单与托盘子菜单统一经 `PetWindow::configurePopupMenu` 装配置顶（README / PRESENTATION 同步）；
  - `MiniGameInfo::menuLabel` 语义改为「子菜单内显示名」（扫雷为 `扫雷`）；
  - `test_smoke` 增加子菜单结构用例（入口文案 / 挂载方式 / 插件项）。
