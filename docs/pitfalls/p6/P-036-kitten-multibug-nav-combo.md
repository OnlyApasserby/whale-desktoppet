# 找小猫三连 Bug：方向键被下拉框吃掉 / 地图被 `setFixedSize(0,0)` 锁死 / 起点标记不消失

> **原编号**：`TRAP-P6-005`　**阶段**：P6　**来源**：原按阶段聚合的 `traps-P6.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：按键路由（输入分发）+ 逻辑（尺寸约束、格子渲染） ｜ **影响**：小游戏「鲸鱼娘找小猫」
（P6+ 追加插件，见 `docs/MINIGAME-INTERFACE.md` §10）实测不可玩——上下方向键不是移动而是换地图，
切过一次难度后地图永久空白（必须重启程序），角色走开后地图上残留出生点标记。

### 现象（用户实测，四条）

1. **下方向键无响应**：按下方向键角色不移动。等价条件：难度下拉框停在**最后一档**时，
   按 Down 不改变当前项 → 不产生任何事件。
2. **上方向键异常**：按下方向键后角色不移动，而是**切到上一个难度并重载地图**，
   看起来像「退出当前地图、进入选择地图界面」。
3. **地图加载失败**：切换难度后新地图不显示，界面处于异常状态；**必须完全关闭并重新打开程序**
   才能恢复地图加载。
4. **左右方向键正常**，但移动后**初始位置的「鲸」标记没有清除**。

### 复现方式（已自动化，非推测）

在 `tests/test_smoke.cpp::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty` 中构造
`KittenView`（`MiniGameContext` 的 controller / db 留空），把难度置为最后一档后向**焦点控件**
投递上下方向键。修复前的实测输出（`QT_QPA_PLATFORM=offscreen`，Debug）：

```
[尺寸] 初始        size=340x247 min=340x247 max=340x247 buttons=88   ← 构造期 rebuild 正常
[尺寸] setIndex(最后) size=0x0   min=0x0     max=0x0     buttons=88   ← 运行中 rebuild 后锁死
focusWidget = QComboBox        atLastIndex=2  afterUp=1  afterDown=2   ← 方向键被下拉框消费
whaleAfterMove = 2                                                    ← 起点与角色同时显示
sizeAfterSwitch = QSize(0, 0)                                         ← 地图不可见
```

### 根因

#### 根因 A（**按键绑定 / 路由问题**）——方向键被难度 `QComboBox` 消费

`keyPressEvent` 的键位映射本身**没有错**（Up→`(0,-1)`、Down→`(0,1)`、Left/Right→`(±1,0)`，
见 `KittenView::handleMoveKey`）。问题出在**分发**：

- 窗口打开后焦点落在**难度下拉框**（`buildConfigBar` 里第一个可接受焦点的控件，
  `QComboBox` 默认 `WheelFocus`），实测 `view.focusWidget() == QComboBox`；
- Qt 的非可编辑 `QComboBox` 会自行处理上下方向键：**Up/Down = 切换当前项**，
  并在切换时发 `currentIndexChanged` → `KittenView::applyDifficulty` → **重载地图**；
- 于是「上移」变成了「上一个难度」；而当难度已在**最后一档**时，Down 无法再切换 →
  既不移动也无事件 → 表现为「下键无响应」。两个方向的现象不同，仅仅取决于**当前难度下标**，
  本质是同一个错误（方向键被非移动控件截获）。
- `Qt::Key_Up/Down` 根本没机会到达 `KittenView::keyPressEvent`；左右键因为 `QComboBox`
  不消费，才「看起来正常」。

#### 根因 B（**逻辑问题**）——`setFixedSize(m_grid->sizeHint())` 在运行中算出 `(0,0)` 并被永久锁死

`KittenMapWidget::rebuild()` 原先以布局的即时常量作为固定尺寸：

```cpp
refresh();
m_grid->activate();
setFixedSize(m_grid->sizeHint());   // ← Bug
```

- `QLayout::activate()` 依赖父控件的可见性与布局脏标记；**运行中**（窗口已显示）重建时，
  该返回值会退化为 `(0,0)`（同一时刻 `sizeHint()` 独立读取仍是 `340x247`，说明是调用时序
  而非布局本身错误）；
- `QWidget::setFixedSize(w,h)` 是**粘性**的：它同时写入 `minimumSize` 与 `maximumSize`；
  写死 `(0,0)` 后布局再也无法把控件撑开 → 控件尺寸恒为 `0×0` → **地图不可见**；
- 之后每次 `rebuild()` 都同样算出 `(0,0)`（自我锁死），**只有重新构造窗口（重启程序）才能恢复**
  —— 与现象 3「必须完全关闭并重新打开」完全吻合。
- 构造期之所以「正常」：那时窗口尚未显示，`sizeHint()` 恰好给出 `340x247`，掩盖了该写法的不确定性。

#### 根因 C（**逻辑问题**）——`applyCell()` 把 `RfkKind::Player` 当作地形渲染

地图里的 `@` 被解析为 `RfkKind::Player`（用于「必须恰好一个起点」的校验）。格子渲染时：

```cpp
if (index == m_world->playerIndex()) { state = "player"; text = "鲸"; }   // 当前所在格
...
case core::RfkKind::Player: state = "player"; break;                      // ← 出生点也画成角色
if (cell.kind != core::RfkKind::Floor) { text = cell.display; }           // ← 于是「鲸」被画上
```

角色走开后，出生点那一格的 `kind` 仍是 `Player`，被判为「角色格」并输出显示文本 `鲸`，
于是地图上同时出现两个「鲸」（实测 `whaleAfterMove = 2`）。**该格只是出生点，语义上就是海床**。

### 解决（三处最小修复，均未改动 core 玩法逻辑）

| 根因 | 修复 | 位置 |
|---|---|---|
| A 按键路由 | 给难度下拉框装 `eventFilter`，把方向键 / WASD 在到达下拉框**之前**截下来交给移动逻辑并吞掉事件；同时 `showEvent` 里 `setFocus()` 把焦点交回窗口本体（双保险） | `KittenView::eventFilter` / `showEvent` / `handleMoveKey` |
| B 尺寸锁死 | 改为**显式计算**尺寸，不再依赖布局返回值：`w = width*cellSize + (width-1)*spacing`（高同理），随后再 `activate()` | `KittenMapWidget::rebuild()` |
| C 起点标记 | 非当前格的 `RfkKind::Player` 一律按 `floor` 渲染，且不输出显示文本 | `KittenMapWidget::applyCell()` |

`handleMoveKey(QKeyEvent*)` 被 `keyPressEvent` 与 `eventFilter` 共用，保证「同一套键位语义」
在任意焦点下一致，不会出现两份键位表漂移。

### 验证（修复后实测，同一用例）

```
[尺寸] 初始/setIndex/按键/移动/切换 全程 min=max=340x247（不再出现 0×0）
focusWidget = whalepet::KittenView   afterUp=2(=难度未变)  afterDown=2(=难度未变)
whaleAfterMove = 1（起点标记已清除）  sizeAfterSwitch = QSize(340, 247)（地图恢复显示）
```

- 回归守卫固化在 `tests/test_smoke.cpp::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty`：
  断言「方向键不得改变难度、不得重载地图」「地图上恒为 1 个角色标记」「切换难度后地图尺寸非 0」；
  修复前该用例 FAIL（`afterUp: 1 vs 2`），修复后 PASS。
- 全量：`ctest -C Debug` / `-C Release` 各 **12/12 通过**（未删除任何断言、未放宽比较条件）。

### 影响与关联文档

- 关联：`docs/MINIGAME-INTERFACE.md` §10（找小猫插件规格）、`tests/test_smoke.cpp`、
  `src/minigame/kitten/KittenView.cpp`。
- 教训 1（按键）：**凡窗口内存在会消费方向键的控件（`QComboBox` / `QAbstractItemView` /
  可编辑控件）时，不能只在 `keyPressEvent` 里绑方向键**——必须明确「方向键归谁」：
  要么把焦点从这些控件上挪开，要么对其安装 `eventFilter` 截获并吞掉，否则按键语义会随焦点漂移。
- 教训 2（尺寸）：**`setFixedSize()` 不要接布局的返回值**（`sizeHint()` / `minimumSizeHint()`）；
  运行期重建控件时该值可能为 0，而 `setFixedSize` 会同时锁死 min/max 造成不可恢复的
  `0×0`。控件尺寸应由自身已知参数算得，布局只用于摆放。
- 教训 3（渲染）：**「角色 / 起点」这类位置型状态不应编码进地形类型**。`@` 只提供出生点坐标，
  渲染必须以「当前所在格」为准，其余同类格按地形（地面）处理。

---
