# 工作台「学习」栏（EX8）

> 目标：在「工作台…」面板里增加一个**学习**页签 —— 把「干活」的功能块**手动导入**进来只读学习
> （条目信息 + 原样代码块正文），也可以粘贴 / 从文件导入外部代码；「拼接整脚本」按导入顺序拼出完整脚本，
> 并把**所有顶层 `import` 归位到文件头、去除重复**。
>
> 本文件是该模块的**唯一设计真源**；进度与验收见 [`README.md`](README.md) §二 的 EX8 条目。
> 关联：`ARCHITECTURE.md` 附录 D（P10 工作台）、[`DRAWING.md`](DRAWING.md) §2（工作台多页化与
> `WorkbenchPage` 贡献点）、[`SETTINGS.md`](SETTINGS.md)（`study_imported`）、[`TESTING.md`](TESTING.md) §2。

---

## 1. 开工前确认的口径（用户已确认，2026-10-11）

| # | 决策项 | 结论 | 影响 |
|---|---|---|---|
| 1 | 导入对象与展示 | 列出「干活」索引条目（标题 / 类别 / 说明 / 依赖）→ **手动勾选导入**；已导入列表选中后显示**条目信息 + 代码块正文（原样只读）**；另可把多条功能块**拼成完整脚本**阅读；再支持**手动粘贴 / 从文件导入外部代码文本** | §3 的页面与状态模型；外部条目**不参与拼接**（无 `run_<id>()` 入口） |
| 2 | 拼接后的 import 处理 | **把所有顶层 `import` 语句移到文件头，并去除重复语句** | 新增纯逻辑 `core::hoistImports`（§4）；**只作用于学习页的拼接预览**，P10 的干活产出脚本口径不变 |
| 3 | 清单持久化 | **持久化**（新增设置键 `study_imported`，json_ext 风格；宿主按既有 `applySettings` 约定注入） | §5；改 `SettingsData` / `SettingsRepo` / `PetWindow` 接线 |
| 4 | 阶段归属 | **扩展阶段 EX8**（主目标已交付、不涉及核心功能变动） | 踩坑归档 `docs/pitfalls/ex8/`；无独立 ROADMAP 文件（与 EX1～EX7 同口径） |

---

## 2. 集成架构：复用既有的「工作台页」贡献点

**不改模块边界**：学习页是**又一个** `WorkbenchPage` 贡献点，宿主（`PetWindow`）不新增任何业务分支，
也不认识这个页面 —— 与 P11 绘画三页走**完全相同**的通用分发链路（`DRAWING.md` §2.3）。

```
右键 / 托盘「工作台…」  ← builtin.work 的 ContextMenu / TrayMenu 贡献点（不变）
        │  createView() 懒建
        ▼
  ui::WorkBenchPanel（顶层工具窗，页签容器）
   ├─ QTabWidget #WorkBenchTabs
   │   ├─ Tab 0「干活」   ← P10 原有界面（唯一内置页）
   │   ├─ Tab 1「学习」   ← EX8：builtin.study 的 WorkbenchPage 贡献点（order = 5）
   │   ├─ Tab 2「抽象画」 ┐
   │   ├─ Tab 3「线条画」 ├─ P11：由 WorkbenchPage 贡献点动态追加（宿主通用分发）
   │   └─ Tab 4「画板」   ┘
   └─ FillPagesFn fillPages(QTabWidget*)   ← 宿主注入的分发回调（P11 引入，EX8 原样复用）
```

| 位置 | 改动 |
|---|---|
| `src/view/ui/StudyUiPlugin.{h,cpp}` | **新增**：`builtin.study` 插件，声明 1 个 `WorkbenchPage` 贡献点（`ui.study.page`，标题「学习」，`order = 5` → 紧随「干活」之后，先于绘画三页的 10 / 20 / 30）；不注册能力 |
| `src/view/ui/StudyPanel.{h,cpp}` | **新增**：页视图（两个列表 + 只读代码区 + 按钮），只读呈现 `StudyService`；**不自行设计样式** |
| `src/viewmodel/StudyService.{h,cpp}` | **新增**：已导入清单（有序）、导入 / 移出 / 清空、只读阅读、拼接预览、JSON 往返 |
| `src/core/PythonImports.{h,cpp}` | **新增**：`hoistImports()`（零 Qt 纯逻辑，§4） |
| `src/viewmodel/WorkService.{h,cpp}` | 新增只读访问器 `blockBody(id)`（按 id 读功能块正文，与产出**同源**） |
| `src/model/SettingsData.h` / `SettingsRepo.cpp` | 新增键 `study_imported`（§5） |
| `src/view/PetWindow.{h,cpp}` | `setupUiPlugins()` 注册 `ui::StudyUiPlugin`（注入窄回调 `workService` / `importedChanged`）；`applySettings()` 增加 `applyStudySettings()`；新增 `persistStudyImported()` |
| `cmake/Libraries.cmake` / `cmake/Tests.cmake` | 登记新源文件；新增测试目标 `test_study` |

**页面所有权**：`createView(parent)` 返回的页由页签容器接管（Qt 父子链），插件只保留只读指针，
**析构时不删除页面**（与 `DrawingUiPlugin` 同口径，见 `DRAWING.md` §2.4）。

---

## 3. 三层落位与职责

| 层 | 文件 | 职责（边界） |
|---|---|---|
| `core`（零 Qt） | `core/PythonImports` | import 归位与去重的**纯文本口径**（可脱界面单测） |
| `viewmodel`（Qt Core） | `viewmodel/StudyService` | 清单状态与编排：导入 / 移出 / 清空、条目解析（功能块元数据**实时**取自内容层）、只读阅读、拼接预览、`study_imported` JSON 往返；**不直接读写数据库** |
| `view`（Widgets） | `view/ui/StudyPanel`、`view/ui/StudyUiPlugin` | 页面与贡献点；插件只做「宿主注入清单 ↔ 清单变化回写」的转接 |

### 3.1 条目模型（`StudyItem`）

| 字段 | 功能块条目（`kind = "block"`） | 外部代码条目（`kind = "external"`） |
|---|---|---|
| `id` | 内容层块 id（如 `read-csv`），**唯一持久化字段** | 生成 id `external:<n>`（n 单调递增、不重号） |
| `title` / `category` / `summary` / `requires` | **不落盘**：每次读取时按**当前**唯一索引解析（内容层更新后学习页随之更新；源消失则标为「已失效」） | `title` 落盘（空则自动命名「外部代码 N」）；类别固定「外部」 |
| `body` | **不落盘**：实时读 `:<blocksRoot>/<id>.txt`（与干活产出**同源**，同一个 `WorkService::blockBody`） | `body` 落盘（**原样**，上限 256 KiB） |

**规则**：

- 导入幂等：未知 id（不在唯一索引里）与已导入的 id **跳过**，返回新增条数；
- 失效源**不从清单里静默移除**：仍列出（便于用户自行移出），但不参与拼接；
- 外部条目**不参与拼接**（没有 `run_<id>()` 入口，进分发器会产出跑不通的脚本）。

### 3.2 页面交互

| 控件（`objectName`） | 行为 |
|---|---|
| `StudyAvailableList` + `StudyImportSelected` / `StudyImportAll` | 候选区（内容层唯一索引；已导入的带「（已导入）」后缀）→ 手动导入；导入后**自动选中最新一条并展示其正文**，候选区保留原有选中（便于连续导入） |
| `StudyImportedList` + `StudyRemoveSelected` / `StudyClearAll` | 已导入清单；选中即展示正文；「清空」有二次确认（外部条目会丢失） |
| `StudyImportExternal` | 弹出「导入外部代码」对话框（标题 + 正文 + **从文件载入…**，限 256 KiB），正文为空即放弃 |
| `StudyPreviewScript` | 拼接整脚本（§4）→ 只读代码区 |
| `StudyCodeView` | 只读 `QPlainTextEdit`：条目信息（注释头）+ 正文，或拼接预览 |
| `StudyMessage` | 操作反馈（导入 N 条 / 已拼接 N 个功能块 / 失败原因） |

页面**不写任何局部样式**：全部控件沿用全局 `resources/qt-ui/default.qss`（与 P11 同口径，本轮未改动任何 qss）。

---

## 4. import 归位与去重口径（`core::hoistImports`）

学习页的「拼接整脚本」= **既有组装口径**（`core::assembleScript`：头注释 + 逐块原样 + 末尾分发器，
与干活产出同一真源）→ 再套一次 `core::hoistImports`：

| 规则 | 取值 | 为什么 |
|---|---|---|
| 搬哪些语句 | **顶层**（行首无空白）且**单行完整**的 `import …` / `from … import …` | 缩进的 import 属函数 / 类体；跨行语句（括号未闭合、反斜杠续行）**保持原位** —— 宁可少搬也不破坏语法 |
| 归位位置 | **注释头之后**（shebang / `# -*- coding -*-` / 生成说明 / 前导空行留在最上方），即 import 成为文件的第一批语句 | 不破坏 shebang 与生成说明的可读性 |
| 去重判据 | 去掉**行尾注释**并**折叠空白**后比较；**保留首次出现的原文**（含其注释） | `import os` / `import os  # 注释` / `import\tos` 视为同一条；`import a` 与 `import a as b` 是**不同**语句，都保留 |
| 无 import | **原样返回**（零改动） | 幂等 + 不引入无谓改动 |
| 幂等 | 再跑一次结果不变 | 面板可反复点、脚本可反复生成 |
| 边界 | 整行删除（不留空行）；其余行顺序不变；由删除产生的**开头空行**清掉 | 保持正文结构（块内 PEP8 空行不动） |

> 口径理由：这是「给人读的拼接预览」，不是格式化器 —— 因此**不重排 import、不合并语句、不改写块正文**，
> 只做「归位 + 去重」两件确定的事。
>
> **不作用于干活产出**：`WorkService::produce()` 的落盘脚本仍按 P10 口径（逐块原样 + 分发器），
> 本轮**未改动**该路径（`test_python_blocks` 全绿为证）。

---

## 5. 持久化（`study_imported`）

**链路**（与 `work_output_dir` 同口径：**写库由宿主负责**，服务只持有状态）：

```
学习页导入 / 移出 / 清空
   → StudyService::changed()（面板刷新）
   → StudyUiPlugin 经 Hooks::importedChanged(json) 回传宿主
   → PetWindow::persistStudyImported()：SettingsRepo load + 只改 study_imported + save
启动 / 设置变更
   → PetWindow::applySettings() → applyStudySettings() → StudyUiPlugin::setImportedJson(json)
   → StudyService::loadFromJson()（与当前一致则不动，**抑制回声写库**）
```

**落盘格式**（JSON 数组文本，Compact）：

```json
[{"id":"read-csv","kind":"block"},
 {"id":"external:1","kind":"external","title":"片段","body":"print(1)\n"}]
```

| 约定 | 取值 |
|---|---|
| 键 | `study_imported`（`settings.json_ext`；空串 = 尚未导入过任何条目） |
| 容错 | 条目缺 `id` / 未知 `kind` / 重复 `id` / 外部正文为空 → **逐条跳过**（宁缺毋滥）；**非空但解析失败或不是数组 → 保持现状**（不因一份坏文本静默清空用户的清单） |
| 上限 | 外部正文单条 ≤ **256 KiB**（对话框与文件载入处拒绝，不截断） |
| 读写方 | 页面 / 服务**不直接碰数据库**；`work.*` 与 `study_imported` 均只经宿主 `applySettings` 链路 |

---

## 6. 测试与验收

新增测试目标 **`test_study`**（`cmake/Tests.cmake`，`TIMEOUT 60`，`WHALEPET_BLOCKS_DIR` 直读磁盘内容层），
覆盖 5 组（共 14 例）：

| 组 | 覆盖 |
|---|---|
| `core::PythonImports` | 归位到注释头之后 / 去重（含行尾注释与空白差异）/ 缩进与跨行语句保持原位 / 无 import 零改动 / 幂等 / 空串 |
| `WorkService::blockBody` | 按 id 读正文（与产出同源）/ 非法 id 不拼路径 / 缺失返回空 |
| `StudyService` | 导入（未知 id 与重复跳过、一次变化一次信号、元数据实时解析）/ 外部条目与空正文拒绝 / JSON 往返与「一致即不动」「坏文本不清空」「空文本清空」/ 拼接（顺序 = 导入顺序、分发器仍在末尾、`import argparse` 只留一条且在正文之前、外部条目不入拼接）/ 失效源仍列出但不进拼接 |
| `StudyUiPlugin` | 贡献点唯一且为 `WorkbenchPage`（标题「学习」、`order < 10`）/ 经 `UiContributionHost` 通用分发成第 2 页 / 懒建幂等 / 宿主注入清单不回写 / 导入回写一次 / 注入同一份不再回写 |
| `StudyPanel` | 候选区 = 内容层条目数 / 「导入选中」→ 清单与列表同步、自动展示正文、候选项标「（已导入）」/ 「拼接整脚本」→ `import argparse` 只留一条 / 「移出选中」 |

**验收**：Debug / Release 构建退出码 0，**CTest 各 38/38**（新增 `test_study`，既有 37 项零回归）；
发布脚本回归（`cmake/*` 改动触发）按 `TESTING.md` §8.2 第 1～9 项通过、第 10 项（安装包静默往返）
按 §四.6 **待人工验收**；踩坑见 `docs/pitfalls/ex8/`。

---

## 7. 明确不做

- **不执行、不联网、不写文件**：学习页是只读阅读器；外部代码文本原样保存（不参与跑脚本）。
- **不改干活产出**：P10 的 `WorkService::produce()` 与 `core::assembleScript` 口径一行未改（import 归位只用于学习页预览）。
- **不做语法高亮 / 编辑器**：只读纯文本；不做保存 / 导出（用户可自行复制）。
- **不做外部代码的持久化瘦身**：不压缩、不裁剪正文（超上限即拒绝导入）。
- 不新增第三方依赖、不改 qss、不新增资源文件。

---

## 8. 关联文档

| 文档 | 关联点 |
|---|---|
| [`README.md`](README.md) | 文档索引、EX8 交付状态、测试目标总量 |
| [`SETTINGS.md`](SETTINGS.md) | `study_imported` 与设置分组归属 |
| [`TESTING.md`](TESTING.md) | `test_study` 覆盖说明与发布脚本回归清单 |
| [`DRAWING.md`](DRAWING.md) §2 | 工作台多页化与 `WorkbenchPage` 贡献点（本模块复用的扩展点） |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) 附录 D | P10 工作台与内容层（`WorkService` / 唯一索引） |
| [`pitfalls/ex8/`](pitfalls/ex8/) | EX8 踩坑条目 |
