# traps · P6 — 设置 / 打磨 / 测试 / 打包（真实踩坑记录）

> 对应 `ROADMAP-P6-Fin.md`。按 `README.md` §二.5 约定，**仅记录 P6 实施过程中真实复现**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P6-001` | 测试辅助 `qs()` 只有 `std::string` 重载，接不住 `matchKeyword()` 返回的 `nullptr` → `strlen(nullptr)` 崩溃 | 测试/UB | 已解决 |
| `TRAP-P6-002` | `HotwordRepo::upsert` 用「先删后插」，重绑关键词会给旧词分配新 id → 热词优先级顺序被打乱 | 逻辑/持久化 | 已解决 |
| `TRAP-P6-003` | `test_growth` 全量运行时一次性失败、**无任何断言输出**，随后不可复现 | 测试/时序 | 未定位（观察项） |
| `TRAP-P6-004` | PowerShell 5.1 把**无 BOM 的 UTF-8** `.ps1` 按 ANSI 解析，中文字面量乱码 → 脚本语法错误 | 脚本/编码 | 已解决 |
| `TRAP-P6-005` | 找小猫实测三连 Bug：方向键被难度下拉框吃掉（上下键变成换地图）、切换难度后地图被 `setFixedSize(0,0)` 锁死空白、角色移开后起点标记不消失 | 逻辑/按键路由 | 已解决 |
| `TRAP-P6-006` | 找小猫「隐形墙」：过门切换场景后**未按新场景重建网格**，新地图被按旧网格行列错位渲染 → 显示是空地、判定却是墙 | 逻辑/显示一致 | 已解决 |
| `TRAP-P6-007` | NSIS 打包三处缺陷：卸载清单漏 `LICENSE`/`README.md`/`stomach` 致残留；64 位程序未设 64 位注册表视图与「所有用户」快捷方式上下文；安装目录未授权致普通用户拖拽投喂不可用 | 打包/部署 | 已解决 |

---

## TRAP-P6-001 — `qs()` 缺 `const char*` 重载，`nullptr` 经隐式转换进 `strlen` 崩溃

**类别**：测试代码 / 未定义行为 ｜ **影响**：`test_hotword` 命中「自定义热词全为脏数据 → 无命中」
分支时进程访问违例（ctest 判定 `SEGFAULT`），P6 热词用例无法跑绿。

### 现象

可复现步骤：

```powershell
cd f:/develop/desktoppet
cmake --build build --config Debug --target test_hotword
ctest --test-dir build -C Debug -R test_hotword --output-on-failure
```

- 构建配置：Visual Studio（多配置）生成器，Config = `Debug`。
- 二进制：`f:\develop\desktoppet\build\Debug\test_hotword.exe`（同目录 `test_hotword.pdb`，带符号）。
- 结果：`1 tests failed out of 2 … 8 - test_hotword (SEGFAULT)`。

用户用 WinDbg 取 `kb`（输出留档 `tests/dump.txt`，不入库），关键帧：

```
00 ucrtbased!strlen+0x31
01 test_hotword!std::_Narrow_char_traits<char,int>::length+0x14   [__msvc_string_view.hpp @559]  ← 首参 = 0x0
02 test_hotword!std::basic_string<char,...>::basic_string<char const*>+0x33 [xstring @819]        ← 第 2 参 = 0x0
03 test_hotword!TestHotword::dirtyHotwordIsSkipped+0x5c4 [F:\develop\desktoppet\tests\test_hotword.cpp @ 120]
04 test_hotword!TestHotword::qt_static_metacall  [test_hotword.moc @136]
```

即：`basic_string(const char* = nullptr)` → `char_traits::length(nullptr)` → `strlen(nullptr)`。
帧 `03` 行号 `@120`，与堆栈上「空指针」一致；`qs` 因内联没有独立帧。

### 根因

`tests/test_hotword.cpp` 的辅助函数只有一个重载：

```cpp
QString qs(const std::string &s) { return QString::fromStdString(s); }
```

而第 120 行传入的是 `core::matchKeyword()` 的返回值，类型为 `const char*`，其 API **约定无命中时返回
`nullptr`**：

```cpp
const std::vector<CustomHotword> allDirty = { { "", "omg" }, { "开服", "nope" } };
QCOMPARE(qs(matchKeyword(QStringLiteral("今晚开服").toStdString(), allDirty)), QString());
```

两条热词全非法（空词 / 非法 id）→ `matchKeyword` 返回 `nullptr`。由于缺少 `const char*` 重载，
编译器改选 `qs(const std::string&)`，需要一次用户自定义转换 `const char* → std::string`
（`basic_string(const char*)`）；该构造函数的前提是**合法的非空 C 字符串**，内部直接
`char_traits::length(ptr)` → `strlen(ptr)`，传 `nullptr` 即 UB → 读地址 `0x0` → 访问违例。

要点：崩溃发生在 `qs` **函数体之前**（`QString::fromStdString` 从未执行），所以在既有重载体内判空无效；
`std::string` 本身不可能为「空指针」，既有重载对它的契约是安全的，缺的只是承接裸指针的那条路。

### 解决

为裸指针单独提供重载并显式判空（其余调用点不变）：

```cpp
QString qs(const char *s)          { return s ? QString::fromUtf8(s) : QString(); }
QString qs(const std::string &s)   { return QString::fromStdString(s); }
```

重载决议：`const char*` 实参精确匹配新重载（不再走 `std::string` 的用户自定义转换）；
`std::string` 实参仍走原重载。`nullptr` 被显式映射为 `QString()`，与断言里期望的空串一致。

### 影响与关联文档

- 关联：`docs/CHAT.md` §4（自定义热词）、`docs/DATA-MODEL.md` §3.9（`hotwords` 表）、`docs/TESTING.md`。
- 教训：测试辅助函数承接「可能返回 `nullptr` 的 C 接口」时，必须提供裸指针重载（或改用返回
  `std::string` 的接口），不能依赖 `const char* → std::string` 的隐式转换。

---

## TRAP-P6-002 — `upsert` 先删后插，重绑关键词会重排热词优先级

**类别**：逻辑 / 持久化 ｜ **影响**：用户修改某条热词绑定的关键词后，该热词的**优先级位置**
被无声挪到列表末尾，与「按录入顺序（`id` 升序）决定优先级」的产品语义不符；表现为「改完关键词后
这条热词不生效了」（被后面的热词抢先匹配）。

### 现象

`test_hotword` 用例 `repoKeepsInsertOrderAndRemoves()` 断言失败（Debug）：

```
FAIL!  : TestHotword::repoKeepsInsertOrderAndRemoves() Compared values are not the same
   Actual   (items[0].word)        : "\u4E59"      ← 乙
   Expected (QStringLiteral("甲")): "\u7532"       ← 甲
F:\develop\desktoppet\tests\test_hotword.cpp(197) : failure location
Totals: 11 passed, 1 failed
```

用例：依次录入 甲 / 乙 / 丙，再把「甲」重绑为 `sike`（第 194 行），
期望 `loadAll()[0]` 仍是「甲」（覆盖不改变位置），实际变成「乙」。

### 根因

原实现为规避 `ON CONFLICT` 兼容问题，采用**先删后插**：

```cpp
DELETE FROM hotwords WHERE word = :w;   // 删掉旧「甲」（id=1）
INSERT INTO hotwords(...) VALUES(...);  // 重新插入「甲」→ AUTOINCREMENT 分到新 id=4
```

`hotwords.id` 是 `INTEGER PRIMARY KEY AUTOINCREMENT`，删除后再插入必然拿到**更大的新 id**；
而 `loadAll()` 以 `ORDER BY id ASC` 表达优先级，于是「甲」被排到「乙」「丙」之后。
即：把「更新」实现成了「删除 + 追加」，主键身份没保住。

### 解决

`upsert` 改为**先 UPDATE、命中即返回**，未命中才 INSERT，从而保住原 `id`：

```cpp
UPDATE hotwords SET keyword_id = :k, created_ms = :ms WHERE word = :w;
if (upd.numRowsAffected() > 0) { return true; }   // 既有记录：id 不变
// 否则 INSERT ...
```

同样不使用 `UPSERT` / `ON CONFLICT`，对老 SQLite 驱动仍兼容；归一化与 `keywordIdValid`
校验逻辑不变。

### 影响与关联文档

- 关联：`docs/CHAT.md` §4（自定义热词，优先级=录入顺序）、`docs/DATA-MODEL.md` §3.9（`hotwords` 表）。
- 教训：凡用「自增主键顺序」承载业务次序（优先级 / 排序）的表，**更新**语义必须原地 `UPDATE`，
  不得用「删 + 插」代替，否则主键（= 次序）会被悄悄改写。

---

## TRAP-P6-003 — `test_growth` 全量运行时一次性失败、无断言输出，随后不可复现

**类别**：测试 / 时序 ｜ **影响**：P6 首次 Debug 全量 `ctest` 时 `test_growth` 判定 Failed，
但**无任何 QTest 断言文本**；随后同二进制单独运行、以及连续 3 次全量运行**均通过**。

### 现象

```powershell
cd f:/develop/desktoppet
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Debug --output-on-failure --timeout 120
```

- 首次：`5 - test_growth ... ***Failed 0.45 sec`，`1 tests failed out of 8`；
  尽管启用了 `-o -,txt`，CTest 仍**未打印任何断言文本**（该失败不具备可诊断输出）。
- `ctest -R test_growth`（单独）→ **Passed**。
- 随后连续 **3 次**全量 `ctest -C Debug` → **每次 100% passed（8/8）**。
- 加入新增 `test_settings` 后，Debug / Release 全量（9 项）→ **均 100% passed**。

### 状态

**未定位 / 暂缓（不可复现）**。按 `README.md` §六，异常一律交回用户调试、AI 不自行插桩排查；
本项非崩溃（进程正常退出，仅测试判 Failed），且无输出，无法据此定位。

**已排除项（已验证）**：

- 与本次 P6 改动无关：本次未触碰 `GrowthService` / `core::GrowthRules` / `PetStateRepo`；
  改动集中在 `SettingsRepo`（`json_ext` 扩展键）、`PetStateMachine`（`night_quiet` 开关）、
  视图层（`PoseView` / `SpeechBubble` / `ContentPanel` / 新增 `SettingsDialog`）与 `PetWindow`。
- 非插件问题（已验证）：`build/Debug` 未执行过 `windeployqt`，Qt 回退到前缀
  `D:/Qt-debug/plugins`，`qoffscreend.dll` 可用——同一批次其余 GUI / 逻辑测试均正常。

**推测（未验证，仅供参考）**：可能与该用例内部的时间/临时目录时序有关，但**未取得证据**，不作结论。

### 影响与关联文档

- 关联：`docs/TESTING.md`（`test_growth`）、`docs/BUILD.md` §9。
- 与 `TRAP-P2-007` 同类：**未复现的长期观察项**，不阻塞 P6 验收。若再次出现且**带断言输出**，
  按 `traps-Pn.md` 规范补记根因。

---

## TRAP-P6-004 — PowerShell 5.1 按 ANSI 解析无 BOM 的 UTF-8 脚本，中文乱码致语法错误

**类别**：脚本 / 文件编码 ｜ **影响**：打包脚本 `packaging/make-package.ps1` 首次执行即失败，
`powershell -File` 直接报解析错误，脚本**一行都没执行**（非运行期问题，是词法阶段）。

### 现象

可复现步骤：

```powershell
cd f:/develop/desktoppet
& powershell -NoProfile -ExecutionPolicy Bypass -File .\packaging\make-package.ps1
```

报错原文（节选）：

```
At F:\develop\desktoppet\packaging\make-package.ps1:51 char:1
+ } else {
+ ~
Unexpected token '}' in expression or statement.
At ...:70 char:85
+ ... '    娓呯悊璋冭瘯鏂囦欢锛? + (($junk | ForEach-Object { $_.Name }) -join ', '))
The string is missing the terminator: '.
```

关键观察：报错正文里的中文字面量呈**乱码**（`娓呯悊璋冭瘯鏂囦欢` = UTF-8 字节被按 GBK 解码的结果）；
失配的是 `{ }` 与成对引号，即**解析**层面。

### 根因

**Windows PowerShell 5.1 对没有 BOM 的 `.ps1` 文件按系统 ANSI 代码页（本机 GBK）解码**，而非 UTF-8。
脚本以 UTF-8（无 BOM）保存，其中的中文（注释与字符串字面量）被逐字节按 GBK 解释产生乱码；
乱码序列中出现引号 / 特殊字符，破坏了字符串与语句块的配对，于是解析失败。

要点：**与脚本逻辑无关**——同一文本以「UTF-8 带 BOM」或「ANSI(GBK)」保存可正常解析，
但两种编码在代码页不同的机器上又会各自出问题。

### 解决

把 `make-package.ps1` 改为**纯 ASCII**（英文注释与输出），不依赖任何代码页，5.1 与 7.x 均可解析。

```powershell
Write-Host '==> [1/4] Configure release build (WHALEPET_PACKAGE=ON, output dist/WhalePet)'
```

对照：NSIS 脚本 `whalepet.nsi` **保留中文**，但走另一条路——`makensis /INPUTCHARSET UTF8` 显式声明
源文件编码，故不受系统 ANSI 影响。

### 影响与关联文档

- 关联：`packaging/make-package.ps1`、`docs/BUILD.md` §10、`docs/README.md` §六。
- 教训：**交给 Windows PowerShell 5.1 的脚本，要么纯 ASCII，要么显式写 UTF-8 BOM**；
  需要中文输出时优先 BOM，且不得依赖「运行机器的默认代码页」。

---

## TRAP-P6-005 — 找小猫三连 Bug：方向键被下拉框吃掉 / 地图被 `setFixedSize(0,0)` 锁死 / 起点标记不消失

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

## TRAP-P6-006 — 找小猫「隐形墙」：场景切换后未按新场景重建网格，行列错位致显示与判定不符

**类别**：逻辑 / 显示与判定一致性 ｜ **影响**：小游戏「鲸鱼娘找小猫」过门进入新场景后，地图
**看起来是空地却走不过去**（用户称「隐形墙」）；第一个场景正常，切换场景后才出现，且无法判断
其它位置是否也有同类错误，游戏体验直接失效。

### 现象（用户实测）

- 在「深海遗迹」（3 个场景）中，**第一个场景没有隐形墙**；
- 经过海流切换地图后，**向左走三格、向右走三格，下方都撞上「隐形墙」**（视觉是空地）；
- 向右三格后是唯一向下的通道，无法继续确认是否还有其它隐形墙。

### 复现方式（已自动化）

`tests/test_smoke.cpp::kittenSceneChangeRebuildsGrid`：

1. 选「深海遗迹」（3 个场景，尺寸依次为 **11×8 → 13×8 → 13×9**，格子数 88 → 104 → 117）；
2. 用 BFS 从起点求出到海流的最短路径并投递按键（不硬编码地图走法）；
3. 切场景后用 core 解析 `kitten_expert_2.txt` 作为「判定真源」，
   断言 `网格格子数 == 场景 2 格子数`，并**逐格比对**「显示状态（`cellState=="wall"`）」与
   「判定数据（`kind==Blocker`）」。

修复前的实测输出：

```
Actual   (grid.size())      : 88      ← 仍是场景 1 的 11 列网格
Expected (room2.cellCount()): 104     ← 场景 2 实际是 13 列
```

### 根因

#### 主因——场景切换后只 `refresh()`，没有按新场景 `rebuild()`

场景切换发生在 `RfkWorld::move()` 内部（玩家踩到出口格时 `m_room++`、坐标改写为新场景起点），
而 View 侧 `KittenView::onMoveRequested()` 对**所有**移动都只做：

```cpp
m_map->refresh();
```

`KittenMapWidget::refresh()` 按 `m_cells`（**上一次 rebuild 建立的按钮列表**）逐个 `applyCell(index)`，
而 `applyCell` 却读 `m_world->currentRoom().cells[index]`（**已经是新场景**）。于是：

- 网格仍是旧场景的行列（11 列 × 8 行 = 88 个按钮）；
- 却把新场景的 cells[0..87] 按 11 列摆放 —— 而新场景是按 13 列解释的，
  第 `y` 行显示的是新场景第 `y*11` 起的 11 格，与判定所用的第 `y*13` 起完全错位；
- 结果：**看到的地图 ≠ 被判定的地图**。视觉上连续的通道，在判定里是礁石（或反之）
  → 「隐形墙」；
- 第一个场景之所以正常：初始 `rebuild()` 就是按它建的网格，行列本来就对。

#### 同类隐患——地图行解析做了 `trim`，行首空格被吃掉会让整行左移

`rfkParseRoom` 复用了物体表那套 `forEachMeaningfulLine`，其中对每行做 `trim`。
而空格按设计是**合法地面**（与 `.` 一样兜底为 floor）：

- 行首空格被吃掉 → 该行整体**左移一格** → 与其它行错位 → 同样是「显示 ≠ 判定」；
- 行尾空格被吃掉 → 该行变短 → 右侧被补成地面，列宽语义与「所见即所得」不符。

随包 6 张地图首尾都是 `#`，未触发；但用户自定义地图时极易踩中，属于同一类缺陷。

#### 放大因素——越界保护会掩盖症状

`applyCell()` 开头有 `if (index >= room.cellCount()) return;`。新旧场景尺寸不同时，
多出来的旧按钮会**保留上一次绘制的内容**，让错位更难一眼看出（而不是留白提示）。

### 解决

| 层次 | 修复 | 位置 |
|---|---|---|
| 主因 | 场景切换（`move.sceneChanged`）时改走 `m_map->rebuild()`，并按新地图尺寸 `adjustSize()`；非切换路径仍走轻量的 `refresh()` | `KittenView::onMoveRequested` |
| 防御 | `refresh()` 检测「网格按钮数 ≠ 当前场景格子数」时**自动 rebuild 自愈**，把错位从隐蔽显示 bug 变为不会发生 | `KittenMapWidget::refresh` |
| 同类隐患 | 地图行改用专用解析：**保留行首 / 行尾空格**，只剥离 `\r`；全空白行与「首个非空白字符为 `;`」的注释行仍跳过（物体表继续沿用带 trim 的解析） | `core/RobotKitten.cpp`（新增 `forEachMapLine`） |

### 验证

- 修复后：`grid.size() == 104`，且场景 2 的**每一格**「显示状态」与「判定数据」完全一致；
  状态栏同步显示「场景 2/3 · 步数 5」。
- **反向验证（证明断言非永真）**：临时回退「切换即 rebuild」与「refresh 自愈」两层修复，
  该用例立即 FAIL（`88 vs 104`）；恢复后 PASS。
- `test_kitten` 新增 `roomKeepsLeadingSpacesAsFloor`：`" @.k"` 这类带前导空格的地图行，
  起点必须落在第 2 列（`startIndex == 1*4+1`）、空格按地面处理；缩进的 `;` 注释行仍被忽略。
- 全量：`ctest -C Debug` / `-C Release` 各 **12/12 通过**（未删除断言、未放宽比较条件）。

### 影响与关联文档

- 关联：`docs/MINIGAME-INTERFACE.md` §10.2（地图格式）、§10.4（界面规格）、
  `src/core/RobotKitten.cpp`、`src/minigame/kitten/KittenView.cpp`、`tests/test_smoke.cpp`。
- 教训 1（一致性）：**凡是「控件网格」承载「逻辑网格」的界面，切换数据源时必须同步重建控件结构**；
  只刷新内容而结构尺寸变了，必然错位。数据源切换点要显式 `rebuild()`，并保留一处
  「数量不一致即重建」的防御。
- 教训 2（解析）：**用同一个函数解析多种文本格式时，要先确认它做的规范化对每种格式都成立**。
  `trim` 对 `key|value` 语料正确，对「空格是内容的字符网格」却是破坏性的；
  应当为地图单独提供保留空白的逐行解析。
- 教训 3（测试）：**断言要打在「显示与判定的一致性」上，而不是只数数量**。
  本例中场景 1（浅滩）与场景 1（深海遗迹）格子数恰好都是 88，只断言数量无法暴露错位；
  逐格比对「界面状态 ↔ 逻辑数据」才能抓住它。

---

## TRAP-P6-007 — NSIS 打包：卸载残留 + 64 位安装视图不一致 + 安装目录无写权限致拖拽投喂不可用

**类别**：打包 / 部署 ｜ **影响**：`dist/WhalePet-Setup-<版本>.exe` 实测两处问题——
（A）安装到非系统盘（如 `D:\`）后**拖拽投喂功能不可用**（投喂动画照常，文件不落盘）；
（B）卸载后 `LICENSE`、`README.md`、`stomach/` **不删除**，且整个安装目录删不掉（`RMDir "$INSTDIR"` 因目录非空而失败）。
维护规范与同步清单见 `docs/packages.md`。

### 现象（用户实测）

- 用 NSIS 安装包安装到 `D:\...` 后，把文件拖到桌宠上：有投喂反馈，但 `stomach/` 里没有文件。
- 卸载完成后，安装目录里仍留有 `LICENSE`、`README.md`、`stomach/`（以及目录本身）。

### 根因

**（B）卸载残留 —— 已验证（逐行核对 `packaging/whalepet.nsi` 即可复现）**

原卸载 Section 只删了 `WhalePet.exe`、`*.dll`、`*.pdb` 与各 Qt 插件目录，**没有** `LICENSE`、`README.md`、
`stomach/` 的删除条目。而 `RMDir "$INSTDIR"` 仅在目录为空时生效，故这些残留必然留下，并连带整个目录删不掉。

**（A）投喂不可用 —— 高置信推断（修复不依赖推断是否成立）**

`StomachService::ingest()` 第一步 `ensureStomachDir()` 失败即**直接返回 0**（仅 `qWarning`），
而调用方 `PetWindow::dropEvent` 仍照常播放投喂动画 → 表现为「有动画、没落盘」。
`stomach/` 不做用户目录降级（需求固定安装目录为唯一落点），故安装目录对其**必须可写**；
而安装目录由提权安装程序创建，其 ACL 取决于目标卷/目录的继承权限：

- **已验证（本机 `icacls`）**：`C:\Program Files` → `BUILTIN\Users:(RX)`（无写权限）；
  `D:\` 根在该机恰含 `NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)`（可继承修改权限）。
  → **可写性随目标卷/目录而变，安装程序不能假定可写**。
- **未在本机复现安装过程**：用户机器上「装到 D 盘不可用」**最可能**是目标目录未继承写权限，
  导致 `stomach/` 创建/写入失败。**标注为推断，待用户机器复核**。

**（附带缺陷，均已验证）**：脚本未设 `SetRegView`（64 位程序卸载项落到 `WOW6432Node`）、
未设 `SetShellVarContext`（快捷方式只装给安装者，换用户卸载删不到）、
`MessageBox` 在 `/S` 静默卸载下无法正常返回（可能误删存档）、
运行中的程序占用 exe/dll 令卸载失败、`/x` 只排了 `data\*.*` 未排目录本身。

### 解决

`packaging/whalepet.nsi`（编译基线：NSIS **3.12**，`/INPUTCHARSET UTF8`）：

| 根因/缺陷 | 修复 |
|---|---|
| 卸载残留 | 卸载 Section 补 `Delete "$INSTDIR\LICENSE"`、`Delete "$INSTDIR\README.md"`、`RMDir /r "$INSTDIR\stomach"`，并补非递归 `RMDir` 兜底 |
| 投喂不可用 | 安装期 `CreateDirectory "$INSTDIR\stomach"` + `nsExec::ExecToLog '"$SYSDIR\icacls.exe" ... /grant *S-1-5-32-545:(OI)(CI)M'`（系统自带 icacls，SID 授权、与系统语言无关、失败只告警） |
| 64 位视图 | `.onInit` 与安装/卸载 Section 均 `SetRegView 64`（`SetRegView` **不能**写在 Section/Function 之外，否则 `Error: command SetRegView not valid outside Section or Function`） |
| 快捷方式上下文 | `.onInit` 与安装/卸载 Section 均 `SetShellVarContext all`（同上，卸载器不执行安装器 `.onInit`，必须重设） |
| 静默卸载 | `IfSilent KeepUserData` 直接走「保留」分支，不弹窗 |
| 程序占用 | 卸载开头 `nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM "WhalePet.exe" /F'` |
| 静默卸载入口 | 注册表补 `QuietUninstallString` |
| 打包排除 | `File /r` 补 `/x "data" /x "stomach" /x "stomach\*.*"` |
| 升级预填目录 | `InstallDirRegKey` 改为 `.onInit` 显式读（先 64 位视图，空则回退 32 位视图，兼容历史 WOW6432Node 登记） |

### 验证

- **已验证**：`makensis 3.12` 编译通过（`Install 4 pages, 1 section` / `Uninstall 2 pages, 1 section`，无 warning/abort）；
  期间修正了一处脚本错误——`SetRegView`/`SetShellVarContext` 首次被写在 Section 之外，被编译器直接拒绝。
- **待人工验收**（真机）：安装到非系统盘 → 普通用户拖拽投喂文件出现在 `<安装目录>\stomach\`；
  卸载（选「是」）后安装目录清空、无 `LICENSE`/`README.md`/`stomach` 残留。步骤见 `docs/packages.md` §6。

### 影响与关联文档

- 关联：`packaging/whalepet.nsi`、`packaging/make-package.ps1`、`docs/packages.md`（新增，打包唯一维护指南）、
  `docs/BUILD.md` §10、`src/viewmodel/StomachService.cpp`、`docs/DATA-MODEL.md` §1。
- 教训 1（卸载）：**安装清单与卸载清单必须逐条对应**，并维护成一张表（`docs/packages.md` §2）；
  `RMDir "$INSTDIR"` 只是「空则删」的兜底，不是清理手段。
- 教训 2（64 位）：**64 位、按机器安装的程序，注册表要显式 64 位视图、快捷方式要显式「所有用户」上下文**；
  这两条指令只能写在 Section/Function 内（卸载器与安装器互不继承）。
- 教训 3（权限）：**装在 Program Files 下的程序，其安装目录对普通用户默认只读**——
  凡是「运行期要写进安装目录」的数据路径，要么有降级通道（如 `data/` 的 `DataPaths` 三级降级），
  要么由安装程序显式授权（如 `stomach/` 的 `icacls`），**不能依赖目标卷的默认 ACL**。
- 教训 4（静默）：`MessageBox` 类交互在 `/S` 静默模式下不可依赖，必须用 `IfSilent` 给非交互默认值。
