# 小游戏地图控件的初始化与尺寸规范（mapinit）

> **本文是小游戏「地图 / 棋盘」类控件尺寸逻辑的唯一规范。**
> **今后所有需要加载地图（或任何按行列网格生成的棋盘）的小游戏插件，其地图生成逻辑
> 必须按本文实现**：尺寸由自身已知参数**显式计算**，**禁止**把布局的返回值
> （`sizeHint()` / `minimumSizeHint()`）交给 `setFixedSize()`。
>
> 本文由「扫雷棋盘重开后变 `0×0`」的实测 Bug 归纳而来，与找小猫实测 Bug
> （`docs/traps-P6.md` `TRAP-P6-005` 根因 B）**同源**。两处控件（`MineBoardWidget` /
> `KittenMapWidget`）已按本文统一修复。

---

## 1. 适用范围与强制程度

| 项 | 说明 |
|---|---|
| 适用对象 | 小游戏插件中**按行列网格生成子控件**的地图 / 棋盘控件（如 `MineBoardWidget`、`KittenMapWidget`） |
| 强制条款 | §4 的三条「必须 / 禁止」为**硬性要求**，新增地图类插件必须逐条满足 |
| 自动化守卫 | 每个地图类插件**必须**提供一条界面回归用例，断言「窗口显示后重建地图，尺寸仍非 0」（见 §6） |
| 不适用 | 不随地图规模变化的固定尺寸控件（普通设置栏、按钮等） |

---

## 2. 问题现象与成因

### 2.1 现象（用户实测）

扫雷窗口**在窗口已经显示之后重建棋盘**（点「重新开始」、切换难度预设、点「开始自定义」），
棋盘会整块变成 `0×0`（空白不可见）。由于 `setFixedSize()` 会把尺寸**永久写死**，
后续每次重建都同样算出 `0×0`，**只有完全退出并重启程序才能恢复**。

### 2.2 直接成因（一行代码）

`MineBoardWidget::rebuild()` 原先以**布局的即时返回值**作为固定尺寸：

```cpp
refresh();
m_grid->activate();
setFixedSize(m_grid->sizeHint());   // ← Bug
```

两个因素叠加，把「一次错误的临时值」变成「不可恢复的永久损坏」：

1. **取值来源不可靠**：`QGridLayout::sizeHint()` 依赖父控件的可见性与布局脏标记。
   **窗口已显示后**重建时，该返回值会退化为 `(0,0)`
   （本项目实测：同一时刻独立读取 `sizeHint()` 仍是正确值，说明是**调用时序**问题，
   而非布局本身算不出来）。
2. **`setFixedSize()` 是粘性的**：它**同时**写入 `minimumSize` 与 `maximumSize`。
   一旦写死 `(0,0)`，外层布局再也无法把控件撑开，且此后每次重建都会复现同样的 `(0,0)`
   —— 形成自我锁死。

**构造期之所以「看起来正常」**：窗口尚未显示时 `sizeHint()` 恰好返回正确值，
掩盖了这个写法的时序不确定性。因此该 Bug **只在运行期重建时暴露**，构造期单测（若只构造不 show）
无法发现。

### 2.3 触发条件（三者同时满足才必现）

1. 地图控件尺寸由 `setFixedSize(布局返回值)` 决定；
2. 地图控件所属窗口**已经 `show()`**（可见、几何已参与布局）；
3. 发生**重建**（换尺寸 / 换难度 / 重开一局 / 切场景）。

---

## 3. 涉及的控件初始化与尺寸设置逻辑

以扫雷 `MineBoardWidget`（`src/minigame/minesweeper/MinesweeperView.cpp`）为例，
一个地图控件由三段逻辑构成。

### 3.1 构造：建立网格容器

```cpp
MineBoardWidget::MineBoardWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("MineBoard")); // 供全局样式表定位棋盘格
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);     // 外边距必须为 0，尺寸才可由公式精确算出
    m_grid->setSpacing(1);                      // 间距必须显式设定，并参与尺寸公式
}
```

> 关键点：`contentsMargins == 0` 且 `spacing` 是**显式常量**，这样控件总尺寸才与
> 「行数列数 + 单元格尺寸 + 间距」这三个已知量形成确定的线性关系（§4 公式）。

### 3.2 重建：先摘除旧格，再按当前模型生成

```cpp
void MineBoardWidget::rebuild()
{
    // 清理旧格（先从布局摘除再延迟释放，避免事件处理中析构自身）
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    m_buttons.clear();

    if (m_game == nullptr) {
        return;
    }

    const int w = m_game->width();
    const int h = m_game->height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int index = y * w + x;
            auto *btn = new MineCellButton(
                index, [this](int i) { emit revealRequested(i); },
                [this](int i) { emit flagRequested(i); }, this);
            btn->setFixedSize(m_cellSize, m_cellSize); // 单元格尺寸是已知常量
            m_grid->addWidget(btn, y, x);
            m_buttons.append(btn);
        }
    }

    refresh();
    // ↓↓↓ 尺寸设置：见 §4 强制条款 ↓↓↓
    const int spacing = m_grid->spacing();
    const int boardW = w * m_cellSize + std::max(0, w - 1) * spacing;
    const int boardH = h * m_cellSize + std::max(0, h - 1) * spacing;
    setFixedSize(boardW, boardH);

    m_grid->activate(); // 让子控件几何按新尺寸立即生效
}
```

### 3.3 尺寸设置：显式计算（唯一正确写法）

尺寸只由**三个已知量**决定：模型的行列数（`w` / `h`）、单元格边长（`m_cellSize`）、
网格间距（`m_grid->spacing()`）。

```
宽 = w * cellSize + (w - 1) * spacing
高 = h * cellSize + (h - 1) * spacing
```

> `(w - 1)` 两两之间的间距；`std::max(0, w - 1)` 兜底 `w == 0` 的退化场景，
> 避免出现负数尺寸。

---

## 4. 强制条款（新增地图类插件必须遵守）

| # | 条款 | 说明 |
|---|---|---|
| **R1** | **必须**用「已知参数」显式计算控件尺寸：`size = count * cellSize + (count - 1) * spacing` | 数据源只能是**自己**的行列数、单元格尺寸、间距常量 |
| **R2** | **禁止**把 `QWidget::sizeHint()` / `QWidget::minimumSizeHint()` / `QLayout::sizeHint()` / `QLayout::minimumSizeHint()`**直接**传给 `setFixedSize()` / `setMinimumSize()` / `setMaximumSize()` | 布局返回值在运行期重建时不可靠（§2.2），`setFixedSize` 的粘性会放大后果 |
| **R3** | 若确需按内容自适应，**必须**先求值到局部变量，并做**非零下限**校验后再使用 | 例如 `const QSize s = ...; if (s.width() > 0 && s.height() > 0) setFixedSize(s);` |
| **R4** | 地图控件的 `contentsMargins` **必须**为 0，`spacing` **必须**是显式常量 | 否则 R1 的公式不再成立（尺寸会与布局实际摆放不符） |
| **R5** | 每次按新模型生成网格后，**必须**同步重算并设置控件尺寸（不要只 `refresh()` 外观） | 各难度 / 各场景行列数不同时，只刷外观会造成行列错位（另见 `TRAP-P6-006`「隐形墙」） |
| **R6** | **必须**提供界面回归用例，断言「`show()` 之后重建地图，控件 `minimumSize()`/`maximumSize()`/`size()` 均 > 0」 | 构造期不 `show()` 的用例无法覆盖本缺陷（§2.2） |

### 4.1 禁止写法（反例）

```cpp
// ❌ 禁止：把布局返回值直接写死为固定尺寸（运行期重建可能得到 (0,0)，且不可恢复）
setFixedSize(m_grid->sizeHint());
m_grid->activate();

// ❌ 禁止：先 activate 再取 sizeHint（取值来源同样不可靠）
m_grid->activate();
setFixedSize(m_grid->sizeHint());

// ❌ 禁止：用布局最小值/最大值兜底（同样是布局返回值）
setFixedSize(m_grid->minimumSize());
```

### 4.2 正确写法（唯一）

```cpp
// ✅ 尺寸由自身已知参数显式计算；布局只负责摆放，不参与定尺寸
const int spacing = m_grid->spacing();
const int w = rowsOrCols * m_cellSize + std::max(0, rowsOrCols - 1) * spacing;
const int h = ...;
setFixedSize(w, h);
m_grid->activate(); // 尺寸确定后再让子控件几何生效
```

---

## 5. 修复步骤（本次实践）

### 5.1 扫雷（`MineBoardWidget`，本次修复）

1. 打开 `src/minigame/minesweeper/MinesweeperView.cpp`；
2. 在 `rebuild()` 末尾，把 `setFixedSize(m_grid->sizeHint())` 改为按
   `w` / `h` / `m_cellSize` / `m_grid->spacing()` 显式计算（§3.2 代码）；
3. 调整顺序：先 `setFixedSize(...)`，后 `m_grid->activate()`；
4. 补 `#include <algorithm>`（`std::max`）；
5. 新增界面回归用例 `test_smoke::minesweeperViewRestartKeepsBoardSized`（§6）。

```cpp
// src/minigame/minesweeper/MinesweeperView.cpp（修复后）
refresh();

const int spacing = m_grid->spacing();
const int boardW = w * m_cellSize + std::max(0, w - 1) * spacing;
const int boardH = h * m_cellSize + std::max(0, h - 1) * spacing;
setFixedSize(boardW, boardH);

m_grid->activate();
```

### 5.2 找小猫（`KittenMapWidget`，同源问题，前序已按同一规则修复）

`src/minigame/kitten/KittenView.cpp` 采用完全一致的写法，可作为对照模板：

```cpp
const int spacing = m_grid->spacing();
const int w = room.width * m_cellSize + std::max(0, room.width - 1) * spacing;
const int h = room.height * m_cellSize + std::max(0, room.height - 1) * spacing;
setFixedSize(w, h);

m_grid->activate();
```

### 5.3 新增地图类插件时的清单（Code Review 自检）

- [ ] 网格容器的 `contentsMargins == 0`、`spacing` 为显式常量（R4）；
- [ ] 尺寸由模型行列数 + 单元格尺寸 + 间距**显式计算**（R1）；
- [ ] 全文件搜索 `sizeHint()` / `minimumSizeHint()`，确认**没有**任何一个被传入
      `setFixedSize()` / `setMinimumSize()` / `setMaximumSize()`（R2/R3）；
- [ ] 每次按新模型生成网格后都重算并设置尺寸（R5）；
- [ ] 存在「`show()` 后重建 → 尺寸仍 > 0」的界面回归用例（R6）。

---

## 6. 回归验证

### 6.1 守卫用例

`tests/test_smoke.cpp::SmokeTest::minesweeperViewRestartKeepsBoardSized`
（与找小猫的 `kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty` 同构）：

1. 构造 `MinesweeperView`（`MiniGameContext` 的 controller / db 留空）并 `show()`；
2. 找到 `objectName == "MineBoard"` 的棋盘控件，断言开局 `minimumSize() > 0`；
3. 点击「重新开始」按钮（= **窗口显示后重建棋盘**，即 Bug 触发路径）；
4. 断言重建后 `minimumSize()` / `maximumSize()` / `size()` 均 > 0，且格子数不变。

### 6.2 修复前（复现，实测输出）

```text
FAIL!  : SmokeTest::minesweeperViewRestartKeepsBoardSized() 'board->minimumSize().width() > 0 && board->minimumSize().height() > 0' returned FALSE. (重开后棋盘 min 尺寸为 0（setFixedSize(0,0) 锁死）)
F:\develop\desktoppet\tests\test_smoke.cpp(392) : failure location
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 28ms
```

### 6.3 修复后（实测输出）

```text
PASS   : SmokeTest::minesweeperViewRestartKeepsBoardSized()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 33ms
```

### 6.4 复现 / 验证命令（原命令，可复制）

前置：`QT_QPA_PLATFORM=offscreen`、构建目录 `build`（VS 2026 x64，`CMAKE_PREFIX_PATH=D:/Qt-debug`）。

```powershell
# 单用例复现 / 验证（把日志写入文件后读取）
$env:QT_QPA_PLATFORM='offscreen'; $env:Path="D:/Qt-debug/bin;$env:Path"
.\build\Debug\test_smoke.exe minesweeperViewRestartKeepsBoardSized -o smoke_out.txt,txt
Get-Content .\smoke_out.txt

# 更贴近日用的一条命令（CTest）
$env:QT_QPA_PLATFORM='offscreen'
ctest --test-dir build -C Debug -R "^test_smoke$" --output-on-failure --timeout 120
```

### 6.5 回归范围（实测）

> ⚠️ 下列数字是**当时的实测留证**（当时 CTest 共 12 个目标）；接入国际象棋与 P7 各阶段后，
> `CMakeLists.txt` 现注册 **22 个测试目标**（见 `TESTING.md` §2）。此处不改写历史值。

- Debug：`ctest --test-dir build -C Debug` → **12/12 通过**；
- Release：`ctest --test-dir build -C Release` → **12/12 通过**；
- 未删除任何断言、未注释失败用例、未放宽比较条件。

---

## 7. 教训

1. **不要用布局的返回值定尺寸**：`setFixedSize()` 接收的应当是「控件的应有尺寸」，
   而布局的 `sizeHint()` 是「布局对当前内容的估计」，两者在运行期重建时**不等价**，
   且前者一旦被写死 `(0,0)` 就**不可恢复**。
2. **尺寸公式要能自证**：控件的尺寸应当能由自身参数（行列数、单元格、间距）**手工算出并复核**；
   凡是「算不出来、只能问布局」的写法，都藏着运行期不确定性。
3. **回归用例必须覆盖运行期路径**：只构造不 `show()` 的用例无法覆盖本缺陷，
   必须走一遍「显示 → 重建」的真实路径。
4. **同类问题一次收敛**：同一类 Bug 在第二个插件（找小猫）复现过一次后，
   第三次（扫雷）再出现说明**规则没有沉淀成规范**——这正是本文存在的意义。

---

## 8. 关联文档

- `docs/traps-P6.md` `TRAP-P6-005`（找小猫三连 Bug：其中根因 B 即本文所述尺寸锁死）
  与 `TRAP-P6-006`（场景切换未重建网格的「隐形墙」，对应本文 R5）；
- `docs/MINIGAME-INTERFACE.md`（小游戏插件化接入机制；§2.2 新增插件步骤、§6 约束、§10 找小猫规格）；
- `src/minigame/minesweeper/MinesweeperView.cpp`（`MineBoardWidget`，本次修复）；
- `src/minigame/kitten/KittenView.cpp`（`KittenMapWidget`，同规则的对照实现）；
- `tests/test_smoke.cpp`（界面回归守卫）。
