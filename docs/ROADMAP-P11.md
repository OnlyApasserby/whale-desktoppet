# WhalePet · ROADMAP P11 · 绘画模块（工作台三页 + 屏幕取色）

> 阶段性质：**主阶段 P11**（新可交付里程碑，用户 2026-10-11 确认）。
> 设计真源：[`DRAWING.md`](DRAWING.md)。踩坑归档：`docs/pitfalls/p11/`。

---

## 1. 阶段目标

在**不修改宿主业务分支**的前提下，把「绘画」做成「工作台…」菜单路径下的一组可交付能力：

1. 把工作台面板从**单页哑面板**升级为**页签容器**，并新增**通用**的「工作台页」贡献点
   （`ContributionKind::WorkbenchPage`）—— 后续任何新页都不需要改宿主；
2. 抽象画生成器（随机配色 + QPainter 实时画布 + 连续刷新与视觉变化）；
3. 线条画生成器（基础平面几何线框组合 + 图形拼接 + 风格一致性）；
4. 简易画板（纯白画布 + 外围控件沿用全局样式 + **屏幕取色 / 放大镜**）。

---

## 2. 交付物

| # | 交付物 | 内容 |
|---|---|---|
| ① | **工作台多页化（架构变更）** | `ContributionKind::WorkbenchPage`；`UiContributionHost::appendToTabs(kind)`（默认参数保持 `SettingsTab` 原行为）；`WorkBenchPanel` 改为页签容器（第 1 页 = P10「干活」页）+ `FillPagesFn`；`BuiltinServiceHooks::workbenchFillPages` 与宿主接线 |
| ② | **抽象画生成器** | `core/DrawingPalette`（完全随机 RGB / 钳制 / 十六进制 / 感知亮度）+ `core/AbstractArt`（笔触序列、批次、上限）+ `view/ui/AbstractArtView`（离屏累积、33ms 刷新、暂停 / 清空 / 保存、满幅淡出重来） |
| ③ | **线条画生成器** | `core/LineArt`（九部位拓扑 + 拼接派生 + 风格共用 + 同种子可复现）+ `view/ui/LineArtView`（换一只 / 保存 / 种子读数） |
| ④ | **简易画板 + 屏幕取色** | `core/CanvasBoard`（笔触模型）+ `view/ui/DrawingBoardView`（纯白画布 / 工具 / 粗细 / 撤销 / 清空 / 保存 / 最近取色）+ `view/ui/ColorSwatch`、`DrawingColors`、`DrawingShapes` + `platform/Win32ScreenColorPicker`（1×1 BitBlt + 系统 Magnification API 放大镜，含降级）+ `view/ui/ScreenColorPickerOverlay`（全屏覆盖层、十字 / 取色框 / 读数 / 左键取色 · 右键 / ESC 取消）+ `view/ui/DrawingUiPlugin`（三个 `WorkbenchPage` 贡献点） |

**未改动项（零回归的实证）**：`IMiniGamePlugin` 一族、`MiniGameRegistry`、P10 的
`WorkService` / `WorkServicePlugin` 业务逻辑（只在懒建面板时多传一个钩子）、
`resources/qt-ui/*.qss`（**一个字节都没改**）、`assets/`。

---

## 3. 验收（A1 ~ A6）

| 项 | 判定 | 结果（2026-10-11） |
|---|---|---|
| A1 构建通过 | `cmake --build build --config Debug/Release --parallel` 退出码 0（复用 `build/`，未新建构建目录） | ✅ 两配置退出码 0 |
| A2 测试通过 | `ctest --test-dir build -C Debug/Release --output-on-failure --timeout ...` | ✅ **各 37/37**（新增 `test_drawing`；既有 36 项零回归） |
| A3 产物可运行 / 可部署 | 产物可运行：构建产物有界存活冒烟；可部署：本轮未要求重新发布（发布脚本回归属发布轮次，见 `TESTING.md` §8.2） | ✅ 有界存活冒烟通过（详见 §5 记录） |
| A4 文档刷新 | `DRAWING.md`（新增）、`ROADMAP-P11.md`（本文件）、`ARCHITECTURE.md` 附录 E、`docs/README.md` 索引与阶段对照、`docs/pitfalls/index.md` 阶段表 + P11 索引 | ✅ 已刷新 |
| A5 踩坑登记 | `docs/pitfalls/p11/`（`P-110` … `P-112`）并在入口补索引行 | ✅ 3 条 |
| A6 用户确认 | 目视验收建议见 §5 | ⏳ 待用户确认 |

---

## 4. 明确不做（本轮范围外）

- **不做**逐帧动画 / GIF 导出；不做图层、滤镜、选区、填充、文字工具（画板保持「简易」定位）；
- **不做**画笔压感 / 平滑（鼠标输入下无收益）；不做画板的「重做（redo）」；
- **不做**屏幕取色的全局热键唤起（只在画板内点击进入；热键留给后续阶段按需立项）；
- **不引入**任何第三方绘图 / 取色 / 主题库；新增链接库仅 `gdi32` / `magnification`（Windows 系统库，见 `DRAWING.md` §5.1）；
- **不改动** `default.qss` / `project.qss`（新页靠既有选择器与自绘取值达成一致外观）。

---

## 5. 验证证据与目视验收建议

**自动化证据**：Debug / Release 构建退出码 0；CTest 各 **37/37**；
`test_drawing` 覆盖随机配色口径、抽象画几何与确定性、线条画拼接（构造性关系）、
画板模型语义、工作台页通用分发、插件三贡献点与懒建、取色覆盖层的取色 / 确认 / 取消 / 失败保底。

**待用户在真机上目视确认的项（A6）**：

1. 右键 / 托盘「工作台…」→ 面板出现四个页签（干活 / 抽象画 / 线条画 / 画板），页签外观与设置页页签一致；
2. 抽象画页：打开即连续刷新、颜色杂乱且每帧都有新变化；「暂停 / 继续」「清空重来」「保存 PNG」有效；
3. 线条画页：每次「换一只」都是同一只鲸鱼娘的不同姿态（头 / 鳍 / 尾 / 眼 / 水柱**相接无断裂**）；
4. 画板页：画布**纯白**、可以画 / 擦 / 撤销 / 清空 / 保存；外围控件与其它页外观一致；
5. 「屏幕取色」：进入后**全屏可点**（不是只有十字线附近能点），十字线与取色框可见，
   放大镜跟随光标且**不遮挡光标点**；左键取色 → 画笔色与「最近取色」更新 + 剪贴板得到 `#RRGGBB`；
   右键 / ESC 取消；取到的颜色与系统取色工具读数一致（记账口径见 `DRAWING.md` §5.3：≤1 个色阶差异为**设计内**）；
6. 若本机 Magnification API 不可用：覆盖层底部出现「放大镜已降级」提示，放大镜仍可用（自绘）。

**已知设计取舍（不是缺陷）**：覆盖层极淡底导致的 ≤1 色阶偏淡（§5.3）；放大镜降级路径；
抽象画满幅后的淡出重来（而非停止增长）。
