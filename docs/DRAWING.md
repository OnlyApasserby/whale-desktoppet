# WhalePet · 绘画模块（P11）设计文档

> 需求来源：用户 2026-10-11 指示 —— 「新增绘画模块，集成到『工作台…』菜单路径下，包含三项核心功能：
> ① 鲸鱼娘抽象画生成器（随机数算法产生配色 + QPainter 实时画布，支持连续刷新与视觉变化）；
> ② 鲸鱼娘线条画生成器（圆 / 矩形 / 三角形 / 多边形等基础平面几何的**线框组合**构建角色轮廓，
> 需要图形拼接与风格一致性）；③ 用户交互式简易画板（**纯白画布**，外围控件严格遵循现有 qt-ui 样式，
> 并实现**屏幕取色**，参考 `references/src` 中 WinEase 项目的屏幕选色实现）」。
>
> **注（2026-10-11 同日）**：需求里点名的参考拷贝 `references/src`（WinEase）已按用户指示
> **从工作区整目录移除**（该参考为**用户本人的个人项目**）。本模块的实现为**独立编写**，
> 移除**不影响任何已交付代码** —— 口径与处置见 §5.1 与 [`ARCHITECTURE.md`](ARCHITECTURE.md) 附录 E.6。
>
> 本文件是该模块的**唯一设计真源**；实施进度与验收见 [`ROADMAP-P11.md`](ROADMAP-P11.md)。

---

## 1. 开工前确认的四项口径（用户已确认，2026-10-11）

| # | 决策项 | 结论 | 影响 |
|---|---|---|---|
| 1 | 集成方式 | **工作台多页化 + 新增「工作台页」贡献点**（`ContributionKind::WorkbenchPage`），绘画三功能作为「工作台…」面板的页签 | 属架构变更：`plugin/ui` 契约 + `UiContributionHost` + `WorkBenchPanel` + 宿主接线（§2） |
| 2 | 屏幕取色路线 | **完全照参考**：Win32 **1×1 BitBlt** 取色 + 系统 **Magnification API** 放大镜 | 需链接 `gdi32` / `magnification`（Windows SDK 系统库，见 §5.1 的依赖口径） |
| 3 | 抽象画配色 | **完全随机 RGB**（三通道各自独立取 [0,255]，不做色相簇限制、不做对比度过滤） | §3；对比度不作为约束，只保留 alpha 区间以免「看不见的一笔」 |
| 4 | 阶段归属 | **主阶段 P11**（新可交付里程碑） | 踩坑归档 `docs/pitfalls/p11/`、路线图 `ROADMAP-P11.md` |

其余细项（面板形态、刷新策略、画布尺寸、保存方式等）按开工前提问稿中的**建议默认值**执行，
在下文逐条写明。

---

## 2. 集成架构：工作台多页化

### 2.1 为什么不能「直接在既有面板里挂子入口」

P10 交付时，「工作台…」是 `builtin.work` 插件声明的**一个** UI 贡献点，点击后懒建
`ui::WorkBenchPanel`（顶层工具窗、**单页哑面板**）。原贡献点协议只有 3 种 kind
（`ContextMenu` / `TrayMenu` / `SettingsTab`），**没有任何机制能在已存在面板内部挂子入口**——
这正是本轮必须做的一次架构变更（已按规范在编码前向用户确认）。

### 2.2 变更后的结构

```
右键 / 托盘「工作台…」  ← builtin.work 的 ContextMenu / TrayMenu 贡献点（不变）
        │  createView() 懒建
        ▼
  ui::WorkBenchPanel（顶层工具窗，页签容器）
   ├─ QTabWidget #WorkBenchTabs
   │   ├─ Tab 0「干活」        ← P10 原有界面（本类自建，唯一内置页）
   │   ├─ Tab 1「抽象画」  ┐
   │   ├─ Tab 2「线条画」  ├─ 由 WorkbenchPage 贡献点动态追加（宿主通用分发）
   │   └─ Tab 3「画板」    ┘
   └─ FillPagesFn fillPages(QTabWidget*)   ← 宿主注入的分发回调
```

### 2.3 契约与接线（改动清单）

| 位置 | 改动 |
|---|---|
| `src/plugin/ui/PluginContribution.h` | `ContributionKind` 新增 `WorkbenchPage`；`createView(parent)` 的 `parent` 语义补充为「工作台页签容器」 |
| `src/view/ui/UiContributionHost.{h,cpp}` | `appendToTabs()` 增加 `kind` 形参（**默认 `SettingsTab`**，既有调用与测试行为不变），实现按 kind 过滤后 `addTab` |
| `src/view/ui/WorkBenchPanel.{h,cpp}` | 改为页签容器；新增构造参数 `FillPagesFn fillPages`（构造末尾调用一次）；新增 `tabs()` 只读访问器；第 1 页仍是 P10 界面 |
| `src/viewmodel/builtin/BuiltinServicePlugins.h` | `BuiltinServiceHooks` 新增 `workbenchFillPages`（`std::function<void(QTabWidget *)>`） |
| `src/viewmodel/builtin/WorkServicePlugin.cpp` | 懒建面板时把该钩子交给面板 |
| `src/view/PetWindow.cpp` | 在 `setupBuiltinServices()` 注入钩子：`m_uiHost->appendToTabs(m_plugins, tabs, ContributionKind::WorkbenchPage)`；在 `setupUiPlugins()` 注册 `ui::DrawingUiPlugin` |
| `src/view/PetWindow.h` | 记录 `m_drawingUiPlugin`（非拥有）+ 前向声明 |

**边界（不可越）**：宿主仍**不认识任何具体页面** —— 分发只按 kind 过滤，新增绘画页 / 后续任何页
都不需要改宿主；绘画插件也**不反向依赖**宿主内部实现（只依赖 `IPlugin` / `IPluginUiHost` /
贡献点纯数据结构）。

### 2.4 页面所有权

`WorkbenchPage` 的 `createView(parent)` 返回的页由**页签容器**接管（Qt 父子链），
插件只保留只读指针用于诊断与单测，**析构时不删除页面** —— 避免与容器双重释放
（与 `StatusPanelUiPlugin` 自持面板的做法不同，原因：状态面板是独立顶层窗、绘画页是容器子控件）。

---

## 3. 抽象画生成器（`core/AbstractArt` + `view/ui/AbstractArtView`)

| 项 | 口径 |
|---|---|
| 随机配色 | **完全随机 RGB**：`r/g/b` 各自独立取 `[0,255]`；alpha ∈ `[64, 220]`（半透明叠加形成层次，且保证每一笔看得见）。随机数算法全部经 `core::IRandom` 注入 ⇒ 同一随机源序列产生同一序列笔触 |
| 图元 | 圆 / 椭圆、矩形、三角形、多边形（5~7 边）、直线、弧线 —— 与线条画共用「基础平面几何」词汇 |
| 几何 | **归一化坐标 [0,1]**（中心 + 宽高 + 旋转角），与画布像素尺寸解耦；生成时即保证「整笔（含半宽半高）都在画布内」 |
| 绘制 | 每 tick 生成 `kAbstractBatchSize = 3` 笔 → QPainter 落到**离屏 `QImage`**（累积），`paintEvent` 只 `drawImage` |
| 刷新 | `QTimer` **33ms（≈30fps）**；打开页面即开始（「实时画布」），可「暂停 / 继续」 |
| 上限与「永续刷新」 | 累计笔触到 `kAbstractStrokeCap = 4000` 后改为**淡出**（每帧盖一层 `alpha=10` 的白，约 0.9s）回到白底后重新生长 —— 视觉上「呼吸 / 重来」，同时给内存与绘制量一个**硬上限** |
| 底色 | 纯白（调色板白 `ui::foreground`）；与画板白底保持同一观感 |
| 保存 | 「保存 PNG」经 `QFileDialog`（默认目录 = 系统「图片」），文件名 `whalepet-abstract-<yyyyMMdd-HHmmss>.png` |

**为什么累积位图而不是每帧重放全部笔触**：成本与笔触数解耦（每帧只补画新笔触），
4000 笔的上限下仍是 O(1)/帧，且「连续刷新」不会随运行时长劣化。

---

## 4. 线条画生成器（`core/LineArt` + `view/ui/LineArtView`）

### 4.1 拓扑固定的九个部位

| 部位 | 图元 | 拼接来源（**派生，不独立抖动**） |
|---|---|---|
| `Tail` 尾鳍 | 多边形（5 边） | 左顶点 = 躯干右边界；垂直中心 = 躯干中心 |
| `Body` 躯干 | 矩形 | **唯一自由锚点**（位置 ±0.015/±0.012、尺寸 ±0.02 抖动） |
| `Head` 头 | 圆 | 底部**插入**躯干上边 0.04（重叠 > 0 ⇒ 必然拼接） |
| `FinLeft` 左鳍 | 三角形 | 顶点 = 躯干**左上角** |
| `FinRight` 右鳍 | 三角形 | 底顶点 = 躯干**右下角** |
| `EyeLeft` / `EyeRight` | 圆 | 由头心派生（水平 ±0.055、纵向 −0.03），保证**完全落在头圆内** |
| `SproutLine` 水柱 | 直线 | 下端 = 头顶，竖直向上（`rotation = 90°`） |
| `Bubble` 气泡 | 圆 | **自由随机**（唯一不参与拼接的部位：装饰物，不影响轮廓识别） |

- **绘制顺序 = `LineArtPart` 枚举顺序**（拓扑固定，单测逐项核对）；
- 多边形「第一个顶点朝左（180°）」是本项目约定，尾鳍靠它**精确**切在躯干右边界
  （不是近似相切，见 `view/ui/DrawingShapes.h`）。

### 4.2 风格一致性

- 整张图共用**一份** `LineArtStyle`（线宽 + 主体色 + 次要色）⇒ 不存在逐形状样式漂移；
- 形状只用「主体 / 次要」两族色：颜色由**界面注入**（`ui::foreground` / `ui::muted`），
  `core` 里**没有任何颜色字面量**；
- 图元集合与部位种类固定 ⇒ 「换一只」只改姿态，不会变成另一个角色。

### 4.3 可复现与交互

- `makeLineArt(rng, style, seed)`：种子驱动 `SystemRandom(seed)` ⇒ **同种子同图**（种子显示在界面上，
  保存文件名也带种子）；「换一只」重新抽种子；
- 交互：点击生成（不做连续刷新 —— 「连续演化」是抽象画的职责，两者区分开）；
- 底色黑、线框白 / 灰（项目既有自绘口径），保存 PNG 时**按同一绘制函数**重画到 `QImage`（所见即所存）。

---

## 5. 屏幕取色（`platform/Win32ScreenColorPicker` + `view/ui/ScreenColorPickerOverlay`）

### 5.1 依赖口径（已由用户确认）

P11 开工时只读核对过一份 Windows 工具集项目（WinEase）的**部分源码拷贝**（原本位于
`references/src/`，148 个文件；**已按用户指示整目录移除** —— 该项目为**用户本人的个人项目**）：
其中**没有取色器本体**，只有底层件 `ScreenCapture::colorAt`（1×1 BitBlt）、`Magnifier`
（系统 Magnification API）、`OverlayWindow`、全局钩子 / 热键。因此本模块是「参考其技术路线 +
**独立实现**」。**移除该拷贝不影响任何已交付代码**（实现不依赖它；代码注释与本文档仅留作来源追溯）。

新增链接库为 **`gdi32`**（`CreateDCW` / `BitBlt` / `GetDIBits`）与 **`magnification`**（`Mag*`），
两者都是 **Windows SDK 随附的系统库**，不是第三方依赖 —— 该口径已向用户明确提示并获同意
（`cmake/Libraries.cmake` 的 WIN32 分支内就地注明）。

**部署含义（如实口径）**：这是**静态导入** ⇒ 目标系统必须提供 `gdi32.dll` / `Magnification.dll`
（Windows Vista 起的客户端版本均含；Win10/11 一定含），`windeployqt` 不复制、安装包不打包、
卸载无需删除，因此**不产生新的卸载条目**（登记见 [`packages.md`](packages.md) §2）。
极简版系统若确实缺 `Magnification.dll`，属「不支持的目标系统」（加载期失败），不可降级；
**可降级的是运行时**：`MagInitialize` 失败（旧系统 / 远程会话）→ 自绘放大镜（§5.5）。

### 5.2 取色路线（照参考的部分）

| 环节 | 做法 | 依据 |
|---|---|---|
| 像素获取 | `CreateDCW(L"DISPLAY") → CreateCompatibleDC/Bitmap → BitBlt(SRCCOPY \| CAPTUREBLT) → GetDIBits(32bpp, 负高度)`，**刻意不用 `GetPixel`** | 参考项目实测：DWM 合成 / 分层窗口下 `GetPixel` 会返回过期或错误颜色 |
| 坐标 | 一律**物理像素 + 屏幕坐标**（虚拟桌面原点可为负）；光标位置由 `GetCursorPos` 提供 | 与 BitBlt 同源，**不做逻辑 ↔ 物理换算**（避免多屏混合 DPI 下的换算陷阱） |
| 放大镜 | 宿主窗口（`WS_EX_TOPMOST｜TOOLWINDOW｜NOACTIVATE` + `WS_POPUP`，`WM_NCHITTEST → HTTRANSPARENT` 点击穿透）+ `WC_MAGNIFIER` 子窗口 + `MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE)` 排除自身 + `MagShowSystemCursor` | 与系统放大镜观感一致 |
| 放大镜摆位 | 默认光标右下（间隔 22px），靠边**翻到另一侧**并夹取在虚拟桌面内；**强制保证不覆盖光标点** | 见 §5.4 |
| 交互 | 全屏覆盖层（`FramelessWindowHint｜WindowStaysOnTopHint｜Tool` + `WA_TranslucentBackground`，覆盖 `primaryScreen()->virtualGeometry()` 的并集）；**左键取色**、**右键 / ESC 取消**、回车 / 空格亦可确认；光标移动 33ms 轮询刷新 | 与参考项目的「全屏悬浮层 + 光标跟随」一致 |
| 回传 | `colorPicked(QColor)` 信号 → 画板设为画笔色 + 进入「最近取色」；同时把 `#RRGGBB` 写入剪贴板 | 取色器惯例，不改变取色结果本身 |

### 5.3 覆盖层自身的「极淡底」与其代价（诚实口径）

Qt 半透明窗口在 Windows 上按**逐像素 alpha** 命中测试：**alpha = 0 的区域收不到鼠标事件**
（参考项目的实测结论同样如此）。因此覆盖层整屏铺一层 **alpha = 1/255** 的极淡底，
既保证「整屏可点」，又让肉眼不可见。

代价：被采样像素会被这层底**减淡 ≤1 个色阶**。选择**不做反向补偿**（补偿的取整误差更大：
`255→254→255`、`127→127→128` 这类一来一回会引入 ±1 的双向误差），并在本文件与代码注释中写明。

### 5.4 「覆盖层会采到自己」这一类陷阱（本模块的核心约束）

「覆盖层 + 取屏」组合最容易踩的坑：**覆盖层画在光标位置的像素会被 1×1 BitBlt 一起采到**。
本模块用**构造性约束**规避，而不是靠事后修补：

1. 十字线在光标周围留出 8px 空隙（四条射线，不经过光标点）；
2. 取色框是**空心方框**（中心那一格正是被采样的像素，不被覆盖）；
3. 读数块（颜色预览 + 十六进制 + 物理坐标）与放大镜一律摆在光标**之外**，并且摆位后
   **再校验一次「矩形是否包含光标点」**，包含则改位、仍包含则**不画**（宁可没有读数）；
4. 放大镜窗口在 `followCursor()` 内部强制避开光标点（屏幕太小时**本轮不摆位**，
   也不覆盖光标）。

### 5.5 优雅降级（可观测、不伪造）

| 条件 | 行为 | 观测点 |
|---|---|---|
| `MagInitialize` 失败（旧系统 / 远程会话） | 改走**自绘放大镜**：抓 30×30 物理像素区域（`captureScreenPatch`）+ `drawImage` 最近邻放大 5× | 覆盖层底部提示文案写明「放大镜已降级」+ `qInfo` 日志；`usingSystemMagnifier()` 单测可核 |
| 单次采样失败（越界 / BitBlt 失败） | **保留上一有效值**，不伪造颜色；状态文本写明失败原因（README 式提示） | 覆盖层读数区显示 `取不到像素：<原因>`；单测覆盖该路径 |
| 无显示 / 注入替身（单测） | 完全不创建原生窗口、不访问真实屏幕 | `setCursorSource()` 注入后 `usingSystemMagnifier() == false` |

---

## 6. 用户交互式简易画板（`core/CanvasBoard` + `view/ui/DrawingBoardView`）

### 6.1 纯白画布与 qt-ui 的兼容做法（**不写局部样式表**）

全局样式表里 `QWidget` 的背景是**黑**。本项目 qt-ui 规范同时明令：
**禁止在子控件上调用 `setStyleSheet()`**（全局样式表是唯一入口）。因此白底画布的做法是：

- 画布控件自持一张 `QImage`（`Format_ARGB32_Premultiplied`，`fill(ui::foreground(255))` = 调色板白），
  `paintEvent` 整块 `drawImage` —— **背景完全由内容覆盖**，全局样式表的黑底不会露出来；
- 不使用 `WA_StyledBackground`、不新增 `project.qss` 规则、不动 `default.qss`（本轮**未改任何样式文件**）；
- 外围控件（`QComboBox` 工具 / 粗细、`QPushButton`、`QLabel`、色块）全部沿用全局样式表既有取值：
  页签本身也自动被 `default.qss` 的 `QTabWidget::pane` / `QTabBar::tab` 覆盖。

### 6.2 笔触模型（纯逻辑，可脱界面单测）

`core::CanvasModel`：`begin`（落笔）→ `extend`（移动，可多次）→ `end`（抬笔，进历史）；
`undo`（撤销最近一笔，**不破坏进行中的一笔**）、`clear`、`boundsOf`（含线宽半径的包围盒）。

| 语义 | 取值 |
|---|---|
| 越界点 | **钳制**到 `[0, width-1] × [0, height-1]`（画布外的输入不产生越界数据） |
| 同像素重复点 | 不入库（鼠标停留会刷出成百上千个同点，白白放大内存与重绘量） |
| 线宽 | 钳制到 `[1, 64]`；界面预设 1 / 2 / 4 / 8 / 16 / 32 px |
| 单点笔触 | `begin` 后直接 `end` 也成笔（点一下就有一个圆点） |
| `begin` 时已有进行中的笔 | 先自动 `end`（不丢数据） |

### 6.3 渲染与工具

- **增量绘制**：`mouseMove` 只补画「上一采样点 → 当前点」这一小段并 `update(该段包围盒)`；
  撤销 / 清空按模型**整体重放**（`renderAll()`）；
- 橡皮 = 用**调色板白**覆盖（画布本来就是白底 ⇒ 不需要透明通道，画布始终不透明）；
- 工具：画笔 / 橡皮（`QComboBox`）、粗细（`QComboBox`）、当前色（自绘 `ColorSwatch`）、
  「屏幕取色」、「最近取色」5 格（点击回填）、撤销 / 清空 / 保存 PNG；
- **颜色的合法来源**：默认笔色 = 调色板黑；取色 / 选色得到的是**画面内容颜色**（数据），
  经 `view/ui/DrawingColors.h` 的 `toQColor() / fromQColor()` 与 `core::RgbColor` 互转。

---

## 7. 样式与颜色的总口径（qt-ui 合规说明）

| 类别 | 取值来源 | 说明 |
|---|---|---|
| 界面控件外观（按钮 / 下拉 / 标签 / 页签 / 面板） | **全局样式表**（`default.qss`，本轮未改动） | 无任何局部 `setStyleSheet`、无 `QPalette` 私自改色 |
| 自绘的界面装饰（十字线 / 取色框 / 读数块 / 色块边框 / 文字） | `src/common/UiPalette.h` | 本轮为该文件补了 `kBackground` / `background(alpha)`（`#000000`，**来自 default.qss 既有调色板**，不是新造颜色） |
| 画面内容颜色（随机笔触 / 取色结果 / 笔色） | `core::RgbColor`（随机或用户取色） | 属**数据**不属界面设计：抽象画为「完全随机 RGB」（用户口径），线条画为界面注入的调色板白 / 灰，画板为取色或调色板黑 |

---

## 8. 测试与验收

新增测试目标 **`test_drawing`**（`cmake/Tests.cmake`，`TIMEOUT 120`），覆盖：
随机配色口径 / 抽象画几何与确定性 / 线条画**拼接关系**（头插入躯干、鳍贴角点、尾切右边界、
眼在头内、水柱接头顶）/ 风格共用与可复现 / 画板模型语义（含越界钳制与包围盒）/
**工作台页的通用分发**（`WorkbenchPage` → 页签，且 `appendToTabs` 默认参数仍保持 `SettingsTab` 原行为）/
绘画插件三贡献点与懒建幂等 / 抽象画连续刷新与暂停 / 线条画计划注入 /
画板取色回填与「最近取色」/ **取色覆盖层在注入替身下的取色 → 确认 / 右键 / ESC 取消 / 采样失败保底**
（全程不触碰真实屏幕、不创建原生放大镜）。

验收：**Debug / Release CTest 各 37/37**（既有 36 项零回归，2026-10-11）。

---

## 9. 关联文档

- 路线图与验收：[`ROADMAP-P11.md`](ROADMAP-P11.md)
- 架构裁决：[`ARCHITECTURE.md`](ARCHITECTURE.md) 附录 **E**、附录 D（P10 工作台）
- 插件化与贡献点：[`PLUGIN-ARCHITECTURE.md`](PLUGIN-ARCHITECTURE.md)、`src/plugin/ui/PluginContribution.h`
- 构建基线：[`BUILD.md`](BUILD.md)（Qt 6.8.4 + MSVC + CMake 4.4.2；本轮复用 `build/`，未新建构建目录）
- 踩坑：`docs/pitfalls/p11/`（`P-110` / `P-111` / `P-112`）
