# 小游戏：插件化接入机制与扫雷（MINIGAME-INTERFACE）

> 本文件最初是「戳泡泡」小游戏的**预留接口**设计（P6 曾评估为不实现），随后
> 「戳泡泡」被**替换为扫雷**。本期进一步把整个小游戏模块**重构为插件化接入机制**：
> 扫雷成为第一个按插件规范接入的小游戏，新增小游戏无需修改核心逻辑。
>
> 文件名沿用历史名称（代码与文档多处引用），**内容以本文为准**。

---

## 1. 交付概览

> 已按插件规范接入的小游戏：**扫雷**（`minesweeper`，见 §3–§5）、
> **鲸鱼娘找小猫**（`kitten`，见 §10）与 **国际象棋**（`chess`，见 §11）。
> 下表为扫雷插件的交付概览。

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
   `emit gameFinished(result)`。若界面含**按行列网格生成的地图 / 棋盘控件**，
   其生成与尺寸逻辑**必须**遵循 `docs/mapinit.md`（尺寸由自身参数显式计算，
   **禁止**用布局返回值定尺寸）。
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
- **地图 / 棋盘类控件的尺寸必须按 `docs/mapinit.md` 显式计算**
  （`size = count * cellSize + (count - 1) * spacing`），
  **禁止**把 `sizeHint()` / `minimumSizeHint()` 等布局返回值交给 `setFixedSize()`；

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
| 找小猫插件（元数据 / 工厂 / 配置摘要） | `src/minigame/kitten/KittenPlugin.{h,cpp}` |
| 找小猫界面（地图网格 + 方向控制 + 难度切换 + 场景 / 进度状态栏） | `src/minigame/kitten/KittenView.{h,cpp}` |
| 找小猫纯逻辑（物体表 / 地图解析 / 移动与撞墙 / 物体交互 / 场景切换 / 结算折算） | `src/core/RobotKitten.{h,cpp}` |
| 找小猫外部资源（物体列表 / 地图 / 台词） | `assets/maps/kitten_objects.txt`、`assets/maps/kitten_*.txt`、`assets/lines/kitten.txt` |
| 找小猫难度配置持久化 | `SettingsData::kittenDifficulty`、`SettingsRepo`（`json_ext`：`kitten_difficulty`） |
| 找小猫地图样式 | `resources/qt-ui/project.qss`（`#KittenMap`） |
| 菜单入口与统一结算 | `PetWindow::setupMiniGames` / `configurePopupMenu` / `showMiniGame` / `settleMiniGame` |
| 设置页动态展示 | `SettingsDialog::buildMiniGameTab`（按注册表生成） |
| 立绘 / 台词广播 | `viewmodel::PetController::presentGame` |
| 台词语料 | `assets/lines/game.txt`、`assets/lines/kitten.txt`、`assets/lines/chess.txt`、`assets/lines/work.txt`（均在 `assets/assets.qrc` 登记、由 `PosePresenter::loadBundledLines` 加载） |
| 成就指标与服务上报 | `src/core/Achievements.h`、`viewmodel::AchievementService::reportMiniGame` |
| 结算奖励（档位 / 每日上限 / 个人最快 / 旧键迁移） | `src/viewmodel/MiniGameService.{h,cpp}`、`core/GrowthRules.h` 的 `kGame*` 常量 |
| 难度配置持久化 | `SettingsData` / `SettingsRepo`（`json_ext`：`minigame_preset`、`minigame_custom_*`） |
| 入口门控 | `PetWindow::applySettings`（`minigame_enabled`） |
| 棋盘样式 | `resources/qt-ui/project.qss`（`#MineBoard`） |
| 单测 | `tests/test_minesweeper.cpp`（纯逻辑 + 档位判定）、`tests/test_minigame.cpp`（通用结算 / 上限 / 分桶纪录 / 旧键迁移）、`tests/test_content.cpp`（成就上报） |

---

## 8. 验证

> ⚠️ **本节数字是「扫雷 + 找小猫」时期的实测记录**（当时 CTest 共 12 个目标）；
> 接入国际象棋与 P7 各阶段后，`CMakeLists.txt` 现注册 **22 个测试目标**（见 `TESTING.md` §2）。
> 下方 12/12 保留为历史留证，不改写。

- 干净构建（VS 2026 + Qt 6.8.4）`ctest -C Debug` / `-C Release` 各 **12/12 通过**
  （含 `test_minesweeper`、`test_kitten`、`test_minigame`；当时尚未有 `test_chess`）。
- 未删除任何断言、未注释失败用例、未放宽比较条件；重构后 `test_minigame` 的断言强度
  与重构前一致，并新增「不同游戏 / 难度纪录互不干扰」「旧版纪录键迁移」两组用例。
- `test_smoke` 校验「小游戏…」子菜单结构：入口文案为 `小游戏…`、以 `QMenu` 子菜单
  形式挂载（`menuAction()->menu()`，即悬停展开 / 键盘导航 / 点击展开的前提）、插件项文案与可用性。
- `test_smoke::kittenSceneChangeRebuildsGrid` 是场景切换的**界面回归守卫**（对应 TRAP-P6-006
  「隐形墙」）：用 BFS 走到海流后，断言「网格格子数 == 新场景格子数」，并**逐格比对**
  「显示状态（`cellState`）」与「core 判定数据（`kind`）」。修复前 FAIL（`88 vs 104`），
  且经反向验证确认断言非永真。
- `test_smoke::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty` 是找小猫的**界面回归守卫**
  （对应 TRAP-P6-005 的三条实测 Bug）：向焦点控件投递上下方向键后，断言「难度不得改变、地图不得重载」、
  「地图上恒只有 1 个角色标记」、「切换难度后地图尺寸不为 0」。修复前该用例 FAIL，修复后 PASS。
- `test_kitten` 覆盖找小猫纯逻辑 14 个用例（物体表解析与非法行跳过 / 内置兜底表 /
  地图解析与三类错误 / 场景数校验 / 移动与撞墙 / 物体一次性消费 / 场景切换 / 通关与结算快照 /
  主动结束 / 通用结算折算 / 难度表），并额外做两项**随包资源自洽性**校验：
  - `bundledMapsArePlayable`：真实解析 `assets/maps/*.txt`，用四方向 BFS 证明每个难度下
    「起点 → 出口 / 小猫」均可达（拦住手绘迷宫把目标围死的低级错误）；
  - `bundledLinesCoverObjectScenes`：物体表声明的每个台词场景 key 都必须在
    `assets/lines/kitten.txt` 中有候选，避免「走到物件上却一句话不说」的静默降级。
  - 这两项断言经**反向验证**确认非永真：临时把 `kitten_easy_1.txt` 里小猫两侧改成墙后
    `bundledMapsArePlayable` 立即 FAIL（`reachable(...) returned FALSE`），还原后恢复 PASS。
- 人工目视项（待用户复验）：在「小游戏…」上悬停展开列表、移开自动收起；方向键导航与回车进入
  子菜单；点击父项展开；扫雷棋盘可读性、右键插旗手感、立绘与台词播报节奏、预设 / 自定义切换与
  难度文案展示；找小猫的可玩性（方向键 / WASD / 点击相邻格三种操作、地图与物件辨识度、
  撞墙与捡物件的台词反馈节奏、海流切场景、小猫专属对话与通关庆祝）；设置页两个插件条目与
  「开始××」。

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
- **本期（第二个插件：鲸鱼娘找小猫）**：
  - 新增插件 `kitten`（`src/minigame/kitten/`）与纯逻辑 `core::RfkWorld`
    （`src/core/RobotKitten.{h,cpp}`），**未改动宿主与结算服务的任何一行**——
    插件机制按设计生效：菜单 / 设置页 / 结算 / 成就全部自动获得；
  - 物体列表、地图、台词全部是外部资源（`assets/maps/*.txt`、`assets/lines/kitten.txt`），
    新增 `assets/maps/` 资源目录并登记 `assets.qrc`；`PosePresenter` 台词加载列表追加
    `:/lines/kitten.txt`；
  - 难度落库新增 `json_ext` 键 `kitten_difficulty`（`SettingsData` / `SettingsRepo`），
    设置页「上次配置」摘要由插件自行生成；
  - 地图格样式新增 `project.qss` 的 `#KittenMap` 规则（取值全部取自 `default.qss` 调色板）；
  - 新增单测 `tests/test_kitten.cpp`（14 个用例 + 2 项随包资源自洽性校验），
    Debug / Release CTest 各 **12/12**。
- **本期（找小猫实测 Bug 修复）**：依据用户实测反馈修复三处问题，分两类根因：
  - **按键绑定 / 路由问题**：方向键被难度 `QComboBox` 消费（上下键 = 切换选项 = 换地图），
    致「上键换地图、下键在末档无响应」→ 对下拉框装 `eventFilter` 截获移动键并转发，
    `showEvent` 把焦点交回窗口；
  - **逻辑问题**：① `rebuild()` 用 `setFixedSize(m_grid->sizeHint())`，运行中该值退化为 `(0,0)`
    且被 `setFixedSize` 锁死 → 切换难度后地图永久空白、必须重启（改为显式计算尺寸）；
    ② `applyCell()` 把出生点格按角色渲染 → 移动后残留「鲸」标记（改为按地面渲染）。
  - 新增界面回归用例 `test_smoke::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty`
    （修复前 FAIL、修复后 PASS），完整记录见 `docs/traps-P6.md` TRAP-P6-005。
- **本期（找小猫「隐形墙」修复）**：过门切换场景后地图出现「看不见的墙」（显示是空地、
  走不过去）——**场景切换时只 `refresh()` 未按新场景 `rebuild()`**，新地图被按旧网格行列错位渲染
  （各场景宽高不同：11×8 → 13×8 → 13×9）。修复：
  ① 切换场景改走 `rebuild()` + `adjustSize()`；② `refresh()` 增加「网格数量与场景格数不符即
  自动 rebuild」的自愈防御；③ 顺带修复同类隐患——地图行解析不再 `trim`（行首空格是合法地面，
  被吃掉会让整行左移同样造成错位），新增 `forEachMapLine` 并补 `test_kitten::roomKeepsLeadingSpacesAsFloor`。
  新增回归用例 `test_smoke::kittenSceneChangeRebuildsGrid`（逐格比对显示与判定，经反向验证），
  完整记录见 `docs/traps-P6.md` TRAP-P6-006。
- **本期（移除方块文字）**：地图方块不再显示任何文字（原先绘制「贝 / 龟 / 星 / 珠 / 草 / 母 /
  瓶 / 靴 / 猫 / 门 / 鲸」），改为**纯 `cellState` 视觉表达 + tooltip 名称提示**，交互与配色样式
  完全不变；`applyCell()` 对文本**无条件清空**，保证移动 / 交互 / 场景切换 / 重建网格后无残留。
  物体表「显示文本」列保留但不再渲染；`project.qss` 的 `#KittenMap` 去掉已无意义的 `font-weight`
  （取值与配色未动）。回归守卫：`test_smoke` 断言「方块文字数恒为 0」（开局 / 移动后 / 切难度后 /
  切场景重建后四个时机）与「`player` 状态格恒为 1」。
- **本期（扫雷棋盘尺寸修复 + `mapinit` 规范）**：扫雷 `MineBoardWidget::rebuild()` 与找小猫**同源**地
  用 `setFixedSize(m_grid->sizeHint())` 定尺寸——窗口显示后重开一局（或切难度 / 开始自定义）会算出
  `(0,0)`，被 `setFixedSize` 同时锁死 min/max → 棋盘空白，且**必须重启程序**才恢复。修复为按
  「行列数 × 单元格尺寸 + 间距」**显式计算**；新增界面回归用例
  `test_smoke::minesweeperViewRestartKeepsBoardSized`（修复前 FAIL、修复后 PASS），
  Debug / Release CTest 各 **12/12**（未删除断言 / 未放宽条件）。同时把该类问题的成因、控件初始化
  与尺寸逻辑、修复步骤与强制条款沉淀为 `docs/mapinit.md`——**今后所有需要加载地图的小游戏插件，
  其地图生成逻辑必须照该文档实现**（尺寸显式计算、禁止用布局返回值定尺寸、配套界面回归用例）。

---

## 10. 第二个插件：鲸鱼娘找小猫（`kitten`）

### 10.1 交付概览

| 项 | 内容 |
|---|---|
| 玩法 | Robot Finds Kitten 风格的地图探索：带鲸鱼娘在字符网格迷宫里四方向移动，绕过礁石、捡起沿途物件，顺着海流切换场景，在最深处找到小猫即通关 |
| 操作 | 方向键 / WASD；屏幕上的方向键按钮；点击与角色相邻的格子（三种方式等价） |
| 难度 | 浅滩（1 个场景）/ 珊瑚湾（2 个）/ 深海遗迹（3 个），难度在窗口内切换并即时开新局 |
| 物体 | 有趣物品（扇贝 / 海龟 / 海星 / 珍珠）、无关杂物（海草 / 水母 / 漂流瓶 / 破靴子）、障碍物（礁石）、出口（海流）、目标（小猫）——**列表与台词均由外部资源定义** |
| 差异化反馈 | 撞礁石 → `meme-shock` + `kitten.blocked`；有趣物品 → `curious` + 物体专属台词；杂物 → `meme-doubt` + 专属台词；捡到小猫 → `meme-kyun` + `kitten.found`（专属对话），1.5s 后补 `game-win` + `kitten.win`；主动结束 → `game-lose` + `kitten.lose` |
| 结算 | 与扫雷共用同一条链路：`core::MiniGameResult` → 档位奖励（每日 3 局共用额度）+ 成就上报 + 结算文案回填 |
| 依赖 | **零新增依赖**：Qt Widgets 自绘 + `core::RfkWorld` 纯逻辑 |

### 10.2 外部资源规格（可配置点）

**物体表** `assets/maps/kitten_objects.txt`：

```
; 单行格式：glyph|id|名称|类别|台词场景key|显示文本
o|shell|扇贝|toy|kitten.shell|贝
b|bottle|漂流瓶|junk||
```

- `glyph` 必须单字符且唯一（同 glyph 后者覆盖前者）；
- `类别` 取 `blocker` / `toy` / `junk` / `kitten` / `exit` / `player` / `floor`；
- `台词场景key` 可留空 → 回退到类别缺省（`rfkKindScene`：`kitten.blocked` /
  `kitten.item` / `kitten.junk` / `kitten.found` / `kitten.scene`）；
- `显示文本`：**当前界面不再渲染该列**（方块上不显示任何文字，见 §10.4）；
  解析仍保留该列以兼容既有物体表与地图，取值可留空；
- 以 `;` 开头的行与空行忽略；格式不合法的行**只跳过该行**，不让整张表失效；
- 资源缺失或解析为空时降级为内置兜底表 `rfkDefaultObjectTable()`（仍可玩）。

**地图** `assets/maps/kitten_<难度>_<序号>.txt`：

- 每行即一行地图；短行右侧按地面补齐，各行列数不必相等；
- **行首 / 行尾的空格会被保留**（空格是合法地面）：解析地图时不做 trim，只剥离 `\r`，
  否则行首空格被吃掉会让整行左移、与判定数据错位（见 `docs/traps-P6.md` TRAP-P6-006）；
- 全空白行忽略；**首个非空白字符为 `;`** 的行视为注释（允许缩进写注释）；
- 地图中出现的字符必须已在物体表中登记（`.` 与空格例外，
  一律按地面兜底，避免自定义物体表漏写时整图不可用）；
- 必须**恰好一个** `player` 类别字符（起点），否则该场景判定为配置错误；
- 非末场景必须有一个出口（`>`）；末场景必须有小猫（`k`）。

**台词** `assets/lines/kitten.txt`：场景 key 前缀 `kitten.*`，格式与其它语料一致
（`场景key|台词文本`）。已覆盖：`kitten.start` / `kitten.scene` / `kitten.blocked` /
`kitten.found` / `kitten.win` / `kitten.lose` / `kitten.item` / `kitten.junk`，
以及每个物件的专属 key（`kitten.shell` / `kitten.turtle` / `kitten.starfish` /
`kitten.pearl` / `kitten.seaweed` / `kitten.jellyfish` / `kitten.bottle` / `kitten.boot`）。
台词风格为鲸鱼娘第一人称、软萌爱撒娇、自称「鲸鱼娘」并称玩家为「主人」。

### 10.3 纯逻辑规格（`src/core/RobotKitten.h`）

- `RfkObjectTable::parse`：物体表解析；`rfkDefaultObjectTable()` 为兜底表。
- `rfkParseRoom`：地图文本 → `RfkRoom`；返回 `false` 时给出可读原因（未定义字符 /
  起点缺失或重复 / 地图为空）。
- `RfkWorld::load(table, difficulty, roomTexts, error)`：按难度所需场景数载入；
  任一场景失败则整体不生效（不留下半成品世界）。
- `RfkWorld::move(dx, dy)` / `moveDir(RfkDirection)`：四方向移动；
  - 越界或撞 `blocker` → `blocked`（带障碍物名称），累计 `blockedCount`，当前连击清零；
  - 走到 `exit` → `sceneChanged`，角色落到下一场景起点；
  - 走到 `toy` / `junk` → `interacted`（物体一次性消费，之后变成空地，避免来回刷台词）；
  - 走到 `kitten` → `won`，本局结束且不再接受操作；
  - 斜向 / 原地 / 步长越界的参数一律忽略（返回空结果）。
- 结算快照 `RfkSummary`：`won` / `perfect`（找到小猫且**全程未撞墙**）/ `expert`（深海遗迹）/
  `maxChain`（连续顺畅移动峰值）/ `steps` / `blockedCount` / `visitedCells` / `floorCells` /
  `roomsVisited`。
- `RfkWorld::abandon()`：主动结束本局（未找到小猫），使 `won == false`，供「结束本局」按钮
  产生 Lose / Draw 档结算。
- 折算：`rfkGameResult(summary, difficulty, elapsedMs)` → `core::MiniGameResult`
  （`gameId = "kitten"`、`difficultyId` 取 `shallow` / `coral` / `abyss`、
  `progress = visitedCells / floorCells`、`maxChain` 供连击成就）。

### 10.4 界面规格（`src/minigame/kitten/KittenView.{h,cpp}`）

- 地图控件 `KittenMapWidget`：按当前场景生成格子按钮，`objectName = KittenMap`；
  格子状态经动态属性 `cellState`（`floor` / `wall` / `player` / `exit` / `toy` / `junk` /
  `kitten`）由 `project.qss` 表达，**C++ 不写颜色字面量**；
- **方块上不渲染任何文字**：物体 / 角色 / 出口一律只用 `cellState` 的配色与边框表达
  （原先绘制的「贝 / 龟 / 星 / 珠 / 草 / 母 / 瓶 / 靴 / 猫 / 门 / 鲸」全部移除），
  名称改由 **tooltip** 提供（悬停可见，信息不丢）；`applyCell()` 对文本做**无条件清空**
  （而非按分支设置），因此移动、捡走物件、场景切换、重建网格之后都不会残留旧内容；
  物体表的「显示文本」列随之不再参与渲染（解析保留，向后兼容）。
- 难度下拉切换即落库（`kitten_difficulty`）并开新局；「重新开始」按当前难度重开；
  「结束本局」放弃并按已探索进度结算（结算后按钮禁用）；
- 状态栏实时显示「场景 i/n · 步数 · 已探索 x/y · 用时」；结算文案由宿主回填；
- 首次移动时启动计时，切换难度 / 重开都会复位；
- **方向键归移动逻辑**：难度下拉框默认会把上下方向键当成「切换选项」，
  故对其安装 `eventFilter` 把方向键 / WASD 截获并转发给移动（`showEvent` 里另把焦点交回窗口本体
  作双保险）；键位语义集中在 `handleMoveKey()` 一处，`keyPressEvent` 与 `eventFilter` 共用。
  见 `docs/traps-P6.md` TRAP-P6-005（实测 Bug：上下键变成换地图）；
- **地图尺寸显式计算**（`width*cellSize + (width-1)*spacing`），**不用** `m_grid->sizeHint()`：
  运行中重建时它会退化为 `(0,0)`，而 `setFixedSize()` 同时锁死 min/max → 地图永久空白、
  必须重启才恢复（同 TRAP-P6-005）；该规则已提炼为**强制规范 `docs/mapinit.md`**，新增地图类插件必读；
- 非当前格的 `player` 类别（出生点）按**地面**渲染，避免角色移开后残留「鲸」标记；
- **场景切换必须重建网格**：各场景宽高不同（深海遗迹 11×8 → 13×8 → 13×9），
  走到海流时 `onMoveRequested` 走 `rebuild()` 并 `adjustSize()`（而非 `refresh()`），
  否则新场景的格子会被按旧网格行列错位显示 —— 视觉是空地、判定却是墙，即「隐形墙」
  （同 TRAP-P6-006）；`refresh()` 另带「网格数量 ≠ 场景格数即自动 rebuild」的自愈防御。

---

## 11. 第三个插件：国际象棋（`chess`）

### 11.1 交付概览

| 项 | 内容 |
|---|---|
| 玩法 | 玩家与**外部 UCI 象棋引擎**对弈：**点击或拖动**走子（点击棋子高亮全部合法落点；点击落点或把棋子拖到落点即走子，落在非法格一律不移动）；玩家可将死 / 被将死 / 逼和 / 和棋 / 认输 |
| 对手 | **外部引擎**（如 Stockfish），程序**不自带棋力**；经 `QProcess` 启动并按 **UCI 协议**通信（`uci` / `isready` / `position` / `go` / `bestmove`） |
| 规则 | 本程序侧实现并校验：合法着法、王车易位、吃过路兵、兵升变、将军 / 将死 / 逼和 / 50 回合 / 子力不足；**引擎返回的着法同样复核**后才落盘 |
| 配置 | 引擎路径（指定或回退 `engine/` 目录）、三档棋力（入门 / 普通 / 困难 = UCI `Skill Level` + 思考时间）、执白 / 执黑；均落库 |
| 表现 | 开局 / 吃子 / 将军 / 胜 / 负 / 和 各切一次立绘并播报台词（`chess.*`）；引擎缺失时提示「引擎不可用」 |
| 结算 | 与其它插件共用同一条链路：`core::MiniGameResult` → 档位奖励（每日 3 局共用额度）+ 成就上报 + 文案回填 |
| 依赖 | **零第三方依赖**：Qt 官方 `QProcess`（`Qt6::Core`）+ `core::ChessGame` 纯逻辑 |

### 11.2 引擎配置（用户自备）

- 程序**不分发任何引擎**；`engine/` 目录随包创建为空（见 `docs/packages.md` §3.1、`README.md`
  「国际象棋引擎」）。用户把 UCI 引擎 `.exe` 放进去，或在窗口内用「浏览…」指定任意路径。
- 默认查找顺序（`defaultChessEnginePath()`）：`<程序目录>/engine`（含一层子目录，优先名字含
  `stockfish`）→ 开发环境向上查找 `dummy/stockfish`。
- 引擎启动前/后不阻塞界面：握手与思考全部异步（`UciEngine::ready` / `bestMove` / `failed` 信号）。

### 11.3 纯逻辑规格（`src/core/Chess.{h,cpp}`）

- `ChessGame`：局面表示（FEN 字符，`0 = a1`）、`fen()` / `loadFen()`、合法着法生成与过滤
  （走后自家王被将军一律剔除）、`makeMove` / `makeUciMove`、`inCheck`、`status()`。
- 特殊规则：王车易位（易位权 + 路径空 + 王与经过格不被攻击）、吃过路兵（目标格与双步推进）、
  兵升变（后 / 车 / 象 / 马四选一）、50 回合规则、子力不足和棋。
- `ChessMove` 与 UCI 串互转：`chessMoveToUci` / `chessMoveFromUci`（`"e7e8q"`）。
- 统计：`capturedValue(byWhite)`（子力点值 P1 N3 B3 R5 Q9）、`maxCaptureStreak(white)`（连续吃子峰值）。
- 折算 `chessGameResult(game, humanIsWhite, levelIndex, elapsedMs)`：
  - `won` = 将死且「被将死方（= 该走棋方）」是引擎；`perfect` = 胜且全程没丢子；
  - `expert` = 困难档；`progress = 玩家吃掉的子力点值 / 39`（供及格档判定）；`maxChain = 玩家吃子连击峰值`。
- 引擎棋力档位 `kChessLevels`：`beginner`(Skill 0 / 300ms) · `intermediate`(10 / 800ms) · `expert`(20 / 1500ms)。

### 11.4 引擎通道（`src/minigame/chess/UciEngine.{h,cpp}`）

- `start(path)`：`QProcess` 启动 → 发 `uci` → 收 `uciok` → 发 `isready` → 收 `readyok` → 发一次 `ready()`；
- `setOption` / `newGame` / `setPosition`（直接下发完整 FEN，含易位权 / 过路兵 / 走子方 / 回合数）/
  `goMoveTime`；输出按行解析，`bestmove` 经 `parseBestMove()` 提取（`(none)` → 空串）；
- 生命周期：`stop()` 先 `quit` 再 `kill` 兜底；启动失败 / 进程异常退出经 `failed()` 上报，
  绝不崩溃、绝不静默。

### 11.5 界面规格（`src/minigame/chess/ChessView.{h,cpp}`）

- 棋盘控件 `ChessBoardWidget`（`objectName = ChessBoard`）：8×8 格子，按「rank8 在上」布局；
  棋子用 Unicode 棋符渲染；格子外观由动态属性 `cellState`（`light` / `dark`）与
  `moveHint`（`none` / `selected` / `target` / `lastmove` / `check`）驱动，取值全部来自
  `project.qss` 的项目专属 `#ChessBoard` 规则（仅黑 / 白 / 灰，不自行设计）；
- **尺寸显式计算**（`8*cellSize + 7*spacing`），遵循 `docs/mapinit.md`，禁止用布局返回值定尺寸；
- **鼠标统一在棋盘控件处理**：格子按钮设为 `WA_TransparentForMouseEvents`（对鼠标透明），
  避免子控件抢走 press/move/release 导致跨格拖动断续；
- **交互状态机**（清晰 / 可预测，见 `ChessBoardWidget` 私有 `DragState`）：
  - `Idle ──press(己方棋子)──▶ Pressed ──拖动超过 startDragDistance──▶ Dragging ──release──▶ Idle`；
  - press 己方棋子 → 选中该格并高亮**全部合法落点**（`moveHint=selected` / `target`）；再次点击同一棋子取消选中；
  - press 合法落点（已有选中）→ 松开即走子（点击走子）；press 非法格 → 取消选中；
  - `Dragging`：跟随光标显示棋子浮影（`#ChessDragGhost`，原格暂时清空），松开时**落点合法才提交**，
    落点非法（或落在棋盘外）→ 不触发移动、原格棋子恢复（棋子回到原格）、取消选中；
  - **只有合法着法才经 `moveRequested(from,to)` 上报**，随后由宿主走「升变选择 → `core::ChessGame::makeMove` →
    UCI `position` + `go`」链路；引擎回合 / 对局结束时 `setInteractive(false)` 会清空选中与拖动（状态切换在 `ChessView::updateBoard` 一处收口）；
- 升变由宿主弹出选择框（`askPromotion`）；引擎回合自动 `position` + `go`，`bestmove` 回来自校验后落盘；
  引擎不可用时提示并等待用户处理；
- 「新局」重开，「认输」按失败结算；结算文案由宿主回填。

### 11.6 实现落位

| 交付项 | 代码位置 |
|---|---|
| 国际象棋纯逻辑（规则 / FEN / 着法 / 判定 / 折算） | `src/core/Chess.{h,cpp}` |
| UCI 引擎通道（QProcess + 协议 + 默认路径定位） | `src/minigame/chess/UciEngine.{h,cpp}` |
| 国际象棋插件（元数据 / 工厂 / 配置摘要） | `src/minigame/chess/ChessPlugin.{h,cpp}` |
| 国际象棋界面（棋盘控件 / 引擎配置 / 状态栏） | `src/minigame/chess/ChessView.{h,cpp}` |
| 引擎 / 难度 / 执子持久化 | `SettingsData::chessEnginePath` / `chessDifficulty` / `chessHumanIsWhite`、`SettingsRepo`（`json_ext`） |
| 棋盘样式 | `resources/qt-ui/project.qss`（`#ChessBoard`） |
| 台词语料 | `assets/lines/chess.txt`（`assets/assets.qrc` 登记、`PosePresenter` 加载列表追加） |
| 引擎目录与打包 / 卸载 | `packaging/make-package.ps1`、`packaging/whalepet.nsi`、`docs/packages.md` §3.1 |
| 单测 | `tests/test_chess.cpp`（12 类用例，零 Qt UI 依赖） |

### 11.7 约束与验证

- **不自带棋力 / 不分发引擎**：引擎是用户自备的第三方程序；程序只做协议通道与规则校验。
- 引擎缺失 / 启动失败 / 返回非法着法 → **优雅降级并提示**，不崩溃、不静默、不污染棋局。
- 宿主与结算服务**不得**出现国际象棋的分支或字段（新增游戏无需改核心逻辑）。
- `tests/test_chess.cpp` 覆盖：FEN 往返、初始 20 着、UCI 串解析、双步与吃过路兵、王车易位
  （含路径被攻击的拒绝）、升变四选一、将死 / 逼和 / 和棋、非法着法拒绝、结算折算与难度表；
- `test_smoke::chessBoardDragEmitsMoveOnlyOnLegalTarget` 是**棋盘交互回归守卫**：向棋盘控件投递
  真实鼠标事件，断言「点击棋子 → selected 且全部合法落点为 target（非法落点不高亮）」「拖动到合法落点 →
  恰好上报一次 `moveRequested(from,to)`」「拖动到非法落点 → 不上报、棋子回到原格、取消选中」
  「点击走子与拖动等价」，并断言棋盘只上报着法、**不改动规则状态**（真正落子由宿主完成）；
- `test_plugin_registry` 断言内置插件数为 3 且 `minigame.chess` 出现在能力总线上。
