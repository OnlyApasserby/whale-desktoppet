# traps · P2 — 立绘/动效/交互接线（真实踩坑记录）

> 对应 `ROADMAP-P2.md`。按 `README.md` §二.5 约定，**仅记录 P2 实施过程中真实复现**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P2-001` | `Event::keyword` 工厂函数与同名字段冲突 → MSVC **C2365** | 编译 | 已解决 |
| `TRAP-P2-002` | 测试中写 `core::` 无法解析 → **C2653**（`using namespace` 不引入命名空间名） | 编译 | 已解决 |
| `TRAP-P2-003` | 无控制台时 QTest 日志走 `OutputDebugString`，CTest/管道抓不到结果 | 验证方法 | 已解决 |
| `TRAP-P2-004` | 「一次性姿态」到期断言与 `kWaitingMs` 语义不符，误判为状态机有 bug | 测试设计 | 已解决 |
| `TRAP-P2-005` | 立绘路径按 `state-%1` 拼装，对 3 个 `*-peek` 不通用 | 数据/资产 | 已解决 |
| `TRAP-P2-006` | 过渡遮断在**起点**就换图 → 「下压→弹起」退化为「先换图再动画」 | 表现/动效 | 已解决 |
| `TRAP-P2-007` | Release 部署版偶发 `0xC0000409`，用户侧 **未能复现** | 运行期崩溃 | **未定位（暂缓）** |
| `TRAP-P2-008` | `ctest` 未注入 Qt `bin` → 用例全报 `0xC0000135`，形似崩溃 | 环境/验证 | 已解决 |
| `TRAP-P2-009` | 状态机每 tick 重推缓存结果 → 特效连播 ~10 次、台词每 200ms 换一句 | 表现/状态机契约 | 已解决 |
| `TRAP-P2-010` | 置顶立绘压住无边框 `QMenu`（右键菜单被遮挡） | 窗口层级 | 已解决 |

---

## TRAP-P2-001 — `Event::keyword` 工厂函数与同名字段冲突（C2365）

**类别**：编译 ｜ **影响**：`whalepet_core` 无法编译，P2 起步即卡住

### 现象

`PetTypes.h` 中 `Event` 既有数据成员 `std::string keyword;`，又提供了静态工厂：

```cpp
static Event keyword(std::string kw, qint64 nowMs);   // 与成员同名
```

MSVC 报：

```
error C2365: "whalepet::core::Event::keyword": 重定义；以前的定义是"数据成员"
```

### 根因

C++ 中同一作用域内**数据成员与成员函数不能同名**；`keyword` 作为成员名已被占用。

### 解决

工厂函数改名 `keywordHit`（语义也更清晰：「关键词命中」），并同步修改调用点与测试：

```cpp
static Event keywordHit(const std::string &kw, qint64 nowMs);
```

### 影响与关联文档

- 约定：`Event` 工厂命名一律用**动词/事件名**（`tick/click/…/keywordHit`），避免与字段名撞车。
- 关联：`docs/STATE-MACHINE.md`（事件清单）、`tests/test_state_machine.cpp`。

---

## TRAP-P2-002 — 测试里 `core::` 无法解析（C2653）

**类别**：编译 ｜ **影响**：`test_state_machine` / `test_line_table` 无法编译

### 现象

测试文件里用 `using namespace whalepet;` 后写 `core::PetStateMachine`，报：

```
error C2653: "core": 不是类或命名空间名称
```

### 根因

`using namespace` 只把**命名空间的成员**引入当前作用域，**不会**引入命名空间本身的名字。
因此 `core` 这个限定名依然不可见。

### 解决

显式加命名空间别名（README 允许，且比逐个 `using` 更不易污染）：

```cpp
namespace core = whalepet::core;
```

### 影响与关联文档

- 所有 P2+ 新增测试统一用命名空间别名，禁止依赖 `using namespace` 带来的间接可见性。
- 关联：`docs/TESTING.md` §3。

---

## TRAP-P2-003 — 无控制台环境下 QTest 结果「消失」

**类别**：验证方法 ｜ **影响**：用例结果无法被 CTest / 管道 / 重定向捕获，看起来像「测试没跑」

### 现象

`add_test(NAME test_state_machine COMMAND test_state_machine)` 手工执行无任何输出；
即使把 stdout 重定向到文件，文件也是空的，但进程退出码正常。
排查过程中还留下了 `-_out` 之类的空日志（已清理）。

### 根因

Windows 上 QTest 的默认日志器在**进程没有控制台**时（CTest 启动、隐藏窗口、
`Start-Process` 无 stdio 继承等场景）会退化为 `OutputDebugString` 输出，
因此标准输出/文件重定向都拿不到内容。

### 解决

给测试命令显式指定日志目标（stdout + txt 格式），CMake 中固化：

```cmake
add_test(NAME test_state_machine COMMAND test_state_machine -o -,txt)
add_test(NAME test_line_table    COMMAND test_line_table    -o -,txt)
add_test(NAME test_smoke         COMMAND test_smoke         -o -,txt)
```

### 影响与关联文档

- 后续新增测试目标**必须**带 `-o -,txt`，否则无法在 CI/CTest 中取证。
- 关联：`docs/TESTING.md` §1（禁止用「看不到结果」蒙混变绿）、`CMakeLists.txt`。

---

## TRAP-P2-004 — 一次性姿态到期断言写错，误判状态机有 bug

**类别**：测试设计 ｜ **影响**：1 条用例长期失败，浪费排查方向（怀疑状态机回落逻辑）

### 现象

`oneShotExpires` 断言「点击头部 → 姿态窗口到期后落回 `waiting`」：

```
FAIL!  : StateMachineTest::oneShotExpires()
   Actual   : "idle-cute"
   Expected : "waiting"
```

### 根因

**断言本身错了，不是状态机错了。** 窗口到期时距上次输入仅 `kCuriousWindowMs = 6s`，
而进入挂机态 `waiting` 需要 `kWaitingMs = 15s`（见 `PetStateMachine::contextPose()` 的优先级：
时段态 > 挂机态 > `idle`）。6s 时正确的返回值就是上下文空闲态 `idle-cute`。

### 解决

修正期望值，并**补充**一次推进到 `kWaitingMs` 的断言，把「两级回落」都钉住：

```cpp
// 到期回落上下文态：距上次输入仅 6s（< kWaitingMs），故为 idle-cute
QCOMPARE(..., QStringLiteral("idle-cute"));
// 距上次输入满 15s → waiting
QCOMPARE(..., QStringLiteral("waiting"));
```

### 影响与关联文档

- 教训：断言必须与**常量语义**对齐（`kCuriousWindowMs` ≠ `kWaitingMs`），
  断言失败先验证「期望值是否合理」，再怀疑实现。
- 关联：`docs/STATE-MACHINE.md`、`src/core/PetTypes.h`（挂机分级常量）。

---

## TRAP-P2-005 — 立绘路径拼装方式对 `*-peek` 不通用

**类别**：数据/资产 ｜ **影响**：3 张 `*-peek` 立绘永远加载不出来（静默缺图）

### 现象

P1 沿用的取图方式是「pose 名 → `state-<name>`」再拼资源路径。P2 接入全部 92 张时发现：
`home-peek` / `settings-peek` / 另一张 peek 的实际文件名是 `dsh-whale-home-peek.webp`，
**不含 `state-` 前缀**，按 `state-%1` 拼出来的路径不存在 → 命中「缺图 → 占位待图」分支。

### 根因

92 张资产并非**单一命名规则**：89 张为 `dsh-whale-state-*`，3 张为 `dsh-whale-*-peek`。
用规则推导路径在资产集合不齐整时必然漏项。

### 解决

放弃拼装，改为**显式清单映射**：由脚本扫描 `assets/poses/*.webp` 生成
`src/core/PoseNames.h`（`PoseEntry{key,file}` 表 + `kPoseCount`），取图走
`PoseCatalog::poseFile()`；`*.qrc` 同样由脚本生成，避免手写 92 项出错。

- 清单文件头部明确「本文件自动生成，禁止手改」。
- 单测补「清单完整性」用例：`kPoseCount == 92`、key 唯一、`poseExists()` 与状态机输出一致。

### 影响与关联文档

- 约定：**资产清单与路径一律来自生成文件/单一目录**，禁止在代码里用命名规则推导资源文件名。
- 关联：`src/core/PoseNames.h`、`src/core/PoseCatalog.h`、`src/view/PoseLibrary.cpp`、`docs/PRESENTATION.md`。

---

## TRAP-P2-006 — 过渡遮断在「起点」换图，动效退化成「先换图再动画」

**类别**：表现/动效 ｜ **影响**：切换姿态时看不到「下压 → 弹起」，观感像硬切

### 现象

立绘统一为 256×256 后，切换姿态的表现目标是：在当前画面上先**下压**（squash），
到最扁的瞬间**换图**，再**弹起**。实测却发现新立绘在动画一开始就出现，随后才走压缩/回弹，
即「先换图再动画」，遮断感全无。

### 根因

`PoseView::resolvePixmap()` 在过渡**开始时**就把解析结果直接写进了当前画面 `m_render`，
而换图时机与动画进度**无关**——换图发生在 t=0，而不是进度过半。

### 解决

引入「待换图」缓冲与显式换图时刻：

- `PoseView` 中新增 `m_staged`：过渡开始时只把目标位图准备好，**不**立刻生效；
- 帧循环里按进度换图：

```cpp
if (p >= kTransitionSwapAt && m_displayedPose != m_poseName) { /* m_render = m_staged; ... */ }
```

- 常量集中在 `src/common/PetVisuals.h`：

```cpp
inline constexpr qreal kTransitionSwapAt = 0.5;   // 换图时刻（进度比例）
```

- 另修一处相关逻辑：立绘**缺失**时不再把过渡目标写进画面，而是记录目标姿态，
  等 `PoseLibrary::poseReady` 补齐后**直接落图**（过渡已走完，不重放动画）。

### 影响与关联文档

- 约定：所有「视觉上要卡在某个时刻生效」的切换，都必须由**帧进度**驱动，不能由「开始时机」隐式决定。
- 关联：`docs/PRESENTATION.md`（过渡遮断）、`src/common/PetVisuals.h`、`src/view/PoseView.cpp`。

---

## TRAP-P2-007 — Release 部署版偶发 `0xC0000409`（**未定位，暂缓观察**）

**类别**：运行期崩溃 ｜ **状态**：**未定位** —— 用户调试**未复现**，按 `README.md` §六 暂缓

### 现象（已复现的事实，非推测）

在「部署版 + 干净 PATH + offscreen + 隐藏窗口 + 不重定向 stdio」条件下自动化观测时，
约 14 次运行中出现 2 次异常（另有一次为启动 3s 内退出、退出码 1，未确认是否同类）：

| 项 | 值 |
|---|---|
| 程序 | `deploy-release\WhalePet.exe`（Release） |
| 环境 | 干净 PATH、`QT_QPA_PLATFORM=offscreen`、`Start-Process -WindowStyle Hidden`、不重定向 stdio |
| 复现位置 | 启动后约 20s（立绘预载完成，WS 19.6MB→86.3MB、线程 4→17）再约 5s |
| 退出码 | `-1073740791` = **`0xC0000409`**（STATUS_STACK_BUFFER_OVERRUN / MSVC `__fastfail`） |
| 复现率 | 约 **2/14** |

### 已排除项（已验证）

- 单测层面无问题：`test_state_machine` / `test_line_table` / `test_smoke` 在 Debug 与 Release 均通过。
- 干净 PATH + offscreen 下启动、以及预载早期（WS ≈ 19.6MB 阶段）均稳定，可稳定运行 5s 以上。
- **Release 构建未生成 PDB**，无法直接符号化；Debug 构建有 PDB（`build\Debug\WhalePet.pdb`）。

### 用户调试结论

按上述步骤由用户执行：

- **Qt Creator（Debug 配置）**：未复现，无任何报错输出；
- **WinDbg**：未复现，无崩溃输出。

即：**当前无法稳定复现，根因未定位**。按约定，AI 不得继续自行插桩/试探定位，故本条**暂记为未定位**。

### 暂缓处理

- 不修改任何代码去「消除」一个无法复现的现象（禁止盲改）。
- 若后续在 Release 部署版**再次复现**，按 `README.md` §六 取证后再定位，建议：
  1. 配置 WER `LocalDumps`（或 `procdump -ma -e`）在崩溃时留存**完整转储**；
  2. 用**带符号的发布构建**（Release + `/Zi` + PDB）或 `RelWithDebInfo` 保证可符号化；
  3. 把转储 + PDB + `WhalePet.exe` 时间戳一并交回。
- 已知的**高风险观察点**（仅作为后续取证方向，**不是已确认根因**）：预载收尾 / `PoseLibrary::poseReady`
  回调链 / `idle→waiting` 切换附近；以及 offscreen 平台下的窗口（气泡）显示路径。

### 影响与关联文档

- 关联：`README.md` §六（崩溃一律交回用户调试）、`docs/BUILD.md`（构建配置与符号）、`ROADMAP-P2.md`（验收需真人桌面环境）。
- 结论：该现象**不阻塞** P2 的代码推进，但**在真实桌面上长时间挂机验收时应继续观察**。

---

## TRAP-P2-008 — 未注入 Qt `bin` 时 CTest 全部 `0xC0000135`，形似崩溃

**类别**：环境/验证 ｜ **影响**：3 个用例「全挂」且退出码形似崩溃，极易误判为代码回归

### 现象

在未把 Qt `bin` 加入 `PATH` 的 shell 中直接跑 CTest：

```
1/3 Test #1: test_smoke .........Exit code 0xc0000135***Exception:   3.04 sec
2/3 Test #2: test_state_machine .Exit code 0xc0000135***Exception:   1.02 sec
3/3 Test #3: test_line_table ....Exit code 0xc0000135***Exception:   0.99 sec
0% tests passed, 3 tests failed out of 3
```

### 根因

`0xC0000135` = **`STATUS_DLL_NOT_FOUND`**：测试可执行文件依赖 `Qt6Core.dll` 等 Qt 运行时，
而该 shell 的 `PATH` 中没有 `D:\Qt-debug\bin`，进程**在入口点之前**就被加载器终止。
CTest 把这类「加载失败」同样记为 `***Exception`，与真正的运行期崩溃在输出上难以区分。

### 解决

运行测试时显式注入 Qt `bin`（与 `TESTING.md` §5 的环境准备一致）：

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
$env:PATH = "D:\Qt-debug\bin;$env:PATH"
ctest --test-dir build -C Debug --output-on-failure --timeout 120
```

修复后 Debug / Release 均 **3/3 Passed**。

### 影响与关联文档

- **判据约定**：`0xC0000135` 先判为**环境问题（DLL 缺失）**，与 `0xC0000409` 那类运行期崩溃区别对待；
  排查顺序是 `PATH` / 部署目录 → 之后才怀疑代码。
- 关联：`README.md` §六（崩溃判定）、`TESTING.md` §5、`TRAP-P1-003`（windeployqt 部署）。

---

## TRAP-P2-009 — 状态机每 tick 重推缓存结果，下游「按字段判断」导致特效连播、台词狂换

**类别**：表现/状态机契约 ｜ **影响**：目视验收第 4、6 项不通过（粒子迸发过于频繁、台词切换过于频繁）

### 现象

真人目视（Release 部署版）观测到：

- 三连击后星星/碎钻**持续迸发约 2s**（不是一次），夸夸的爱心持续约 6s；
- 气泡里的台词**每 200ms 换一句**，且同一句不会重复，看起来在「刷屏」；
- 连点不同部位时，只有**第一次**点击有台词（与「只显示最后一次操作的台词」相反）。

### 根因

`PetController::onTick()` 每 `kTickMs`（200ms）都会 `present()` 两次，
其中 `present(m_sm.current())` 推的是状态机的**缓存结果**，而缓存结果在一次性姿态存续期内
**始终携带同一个 `fx` 与 `lineKey`**：

1. `PosePresenter::present()` 只要 `result.fx != Fx::None` 就 `playFx()` → 特效按 tick 重放
   （三连击 `Fx::Particle` 存续 `kSuccessWindowMs`=2s ≈ 10 次 × 26 粒；夸夸 `Fx::Heart` 存续 6s ≈ 30 次 × 6 粒）。
2. 台词同理，且 `LineTable::pick()` 是**播放时**才随机取句，所以每次重放都会**换一句**。
3. `PetStateMachine::makeLine()` 的 6s 节流只在**生成 `lineKey` 那一刻**生效，对「缓存态重放」毫无约束 → 节流形同失效；
   同时该节流也作用于用户交互（`proactive=false`），于是连点只有第一次能拿到台词。

### 解决

把「新的一次表现」变成**显式语义**，而不是让下游从字段值去猜：

```cpp
struct PoseResult {
    ...
    std::uint32_t fxSerial = 0;    // 只在产生新特效时自增
    std::uint32_t lineSerial = 0;  // 只在产生新台词时自增
};
```

- 状态机侧：所有产出路径统一走 `compose()`；重复重放缓存态与回落（`fx=None`）时**不自增**；`reset()` 刻意不清零，保持单调。
- `PosePresenter` 侧：持有 `m_lastFxSerial` / `m_lastLineSerial`，只有序号变化才 `playFx()` / `startStream()`。
- 台词节流范围收窄为**只约束主动说话**（用户交互不再被吞），「连点不刷屏」改由「序号去重 + 流式打断」保证。

### 影响与关联文档

- 表现层另有 `kFxMinGapMs` = 500ms 强制间隔（**共用**、丢弃不排队），见 `PRESENTATION.md §2.1`。
- 台词改为流式输出且**打断式**（只保留最后一次），见 `PRESENTATION.md §4.1`。
- 契约变化：`STATE-MACHINE.md` §4/§5/§7、`TESTING.md` §2；单测新增 `serialsMarkOnlyNewOutcomes`、
  `speechThrottleAppliesToProactiveOnly`（原 `speechIsThrottled` 按新语义改写）、
  `fxSerialPlaysOnceAndRespectsGap`、`lineSerialDedupesAndStreamInterrupts`。

---

## TRAP-P2-010 — 置顶立绘压住无边框 `QMenu`，右键菜单被遮挡

**类别**：窗口层级 ｜ **影响**：右键菜单被立绘遮住、难以正常使用（目视验收范围外的测试项）

### 现象

在真实桌面右键立绘：菜单弹出后被立绘窗口**盖住一部分**（现象确认为「视觉遮挡」，非点击穿透）。

### 根因

`PetWindow` 的窗口标志是 `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool`（置顶），
而 `QMenu` 默认是 `Qt::Popup` —— 两者 Z 序比较时菜单不在上层。

### 解决

菜单与立绘**同级置顶**，并在显示时再抬一次层级：

```cpp
m_menu->setWindowFlag(Qt::WindowStaysOnTopHint, true);
m_menu->installEventFilter(this);   // Show 事件里 menu->raise()
```

- 托盘菜单（另一个 `QMenu` 实例）同样处理，避免同类问题漏网。
- 未采用「菜单打开期间临时取消立绘置顶」的备选方案：会触发窗口标志重建、有闪烁风险，且本例只需置顶即可。

### 影响与关联文档

- 约定：**新增任何菜单/弹出窗口都要显式置顶**，否则会被置顶立绘吃掉。
- `offscreen` 平台会打印 `This plugin does not support raise()`（该平台无窗口管理），属预期噪声，不代表失败。
- `PRESENTATION.md §3`（窗口行为表已补「菜单层级」一行）。

---

## P2 实测结论（截至本次记录）

| 验收项 | 结果 | 说明 |
|---|---|---|
| Debug 可构建 | ✅ | `cmake --build build --config Debug --parallel` |
| Release 可构建 | ✅ | `cmake --build build --config Release --parallel` |
| `test_state_machine` | ✅ | 上下文态 / 交互 / 时序 / 概率 / 关键词 / 清单完整性 |
| `test_line_table` | ✅ | 解析、取用、最近去重、与状态机输出场景一致性 |
| `test_smoke` | ✅ | offscreen 下创建窗口并切换姿态；特效序号去重 + 500ms 间隔（含「丢弃不补播」）；台词序号去重 + 流式打断 |
| CTest 汇总 | ✅ | Debug 与 Release 均 **3/3 Passed**（需先注入 Qt `bin`，见 `TRAP-P2-008`）；`test_smoke` 共 6 个用例通过 |
| 92 张立绘入资源 | ✅ | 统一 256×256，清单由脚本生成（见 `TRAP-P2-005`） |
| 程序化动效（呼吸/摇摆/惯性/点击反馈/过渡遮断/粒子） | ✅ 代码完成，⏳ 待真人目视 | 需真实桌面确认观感（`offscreen` 无法判定） |
| 偶发 `0xC0000409` | ⚠️ **未定位** | 见 `TRAP-P2-007`：仅在自动化 offscreen 场景复现，用户调试未复现，暂缓观察 |

> 结论：P2 的**功能实现与自动化验证已跑通**；立绘动效观感与上述偶发崩溃需在真实桌面继续观察，
> 故 `ROADMAP-P2.md` **暂不追加 `Fin`**。
