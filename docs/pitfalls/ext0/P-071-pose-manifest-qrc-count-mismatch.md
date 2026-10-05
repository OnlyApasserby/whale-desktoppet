# 新增贴边立绘只更新了清单的一半：`home-bottom` 未入 `assets.qrc`，且 `kPoseCount` 与清单条数不一致

> **原编号**：`TRAP-EXT0-003`　**阶段**：EXT0　**来源**：原按阶段聚合的 `traps-extend0.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：数据 / 资产（清单「三处对应」） ｜ **影响**：新增的贴边立绘
`dsh-whale-home-bottom.webp` 运行期**永远加载不到**（静默缺图）；依赖它的「上边框贴边」表现
（`DesktopEdge::Top` → `home-bottom`）退化为「保持常规显示」并只记一条 `qWarning`。
同一处还暴露：预载队列尾项被漏掉。

### 现象（可复现，已验证）

- `assets/poses/dsh-whale-home-bottom.webp` 存在（61 KB），`src/core/PoseNames.h` 的 `kPoses`
  也已登记 `{"home-bottom", "dsh-whale-home-bottom"}`；
- 但 `assets/assets.qrc` 里**没有**这一条（只有 `home-peek` / `settings-peek` / `workbench-peek`），
  即运行期不存在 `:/poses/dsh-whale-home-bottom.webp`；
- 复现：`rg "home-bottom" assets/assets.qrc` → **无匹配**；`assets.qrc` 的 `poses/` 条目数为 92，
  而 `assets/poses/*.webp` 为 93 个文件、`kPoses` 为 93 条。

### 根因

立绘资产的清单在工程里分散在 **3 处**，本次新增只更新了其中 2 处：

| # | 位置 | 作用 | 漏掉的后果 |
|---|---|---|---|
| 1 | `assets/poses/*.webp` | 实际文件 | — |
| 2 | `src/core/PoseNames.h`（`kPoses` / `kPoseCount`） | pose → 资源文件名映射、预载队列长度 | `poseFile()` 查不到 → 无法拼出资源路径 |
| 3 | `assets/assets.qrc` | 把文件编译进 `:/poses/` | 路径存在但**资源不存在** → 运行期缺图（编译期不报错） |

漏掉第 3 处时**编译通过、链接通过、单测也可能通过**，只在请求该 pose 时表现为缺图，
属于典型的「能跑但错」。同一处还暴露计数不一致：`kPoses` 实为 **93** 条而 `kPoseCount` 仍是 **92**，
`PoseLibrary` 按 `kPoseCount` 建预载队列，导致**尾项 `workbench-peek` 被排除在预载之外**
（首次显示时回落到「按需即时加载」，功能可用但不符合设计意图）。

### 解决

| 层 | 修复 |
|---|---|
| 资源 | `assets/assets.qrc` 补登 `poses/dsh-whale-home-bottom.webp`（立绘 92 → 93） |
| 清单 | `src/core/PoseNames.h`：`kPoseCount` 92 → **93**（与该表条数、与目录文件数一致） |
| 单测 | `test_state_machine::catalogCoversAllPoses` 期望值同步 92 → 93 —— 该断言是**清单完整性守卫**，同步后它**真正覆盖全部 93 项**（不是放宽条件） |
| 文字 | 「92 张立绘」→「93 张」同步于 `PoseNames.h` / `PetVisuals.h` / `PoseView.h` / `PoseLibrary.h` / `WorkState.{h,cpp}` / `PRESENTATION.md` / `ARCHITECTURE.md` / `README.md` |

### 验证

- `test_smoke::poseViewAttachesPeekPoseToDesktopEdge`：贴边立绘可加载，且左 / 右命中区互斥（贴合方向正确）；
- `test_smoke::petWindowReportsDesktopEdgeWhenMovedToBorder`：窗口移到边框 → 方向判定与表现联动；
- `test_state_machine::catalogCoversAllPoses`：`kPoseCount == 93`，逐项 `poseExists()` / `poseFile()` 通过；
- Debug / Release CTest 各 **17/17**（新增用例计入 `test_smoke`，测试目标数不变）。

### 影响与关联文档

- 关联：`assets/assets.qrc`、`src/core/PoseNames.h`、`src/view/PoseLibrary.cpp`、
  `docs/PRESENTATION.md` §1/§3.1、`tests/test_state_machine.cpp`、`tests/test_smoke.cpp`。
- 教训：**凡「文件 + 代码清单 + 资源清单」三处对应的资产，新增时三处必须同一次改到位**；
  建议把「`assets/poses/*.webp` 文件数 == `assets.qrc` 的 `poses/` 条目数 == `kPoseCount`」
  作为可自动核验的不变量——本次正是由该不变量的差值发现的问题。
  另外：`kPoses` 是**有序表 + 独立计数常量**，在中间插入一项会让尾项静默落到 `kPoseCount` 之外，
  新增时必须同步计数（或改为 `sizeof(kPoses)/sizeof(kPoses[0])` 直接推导）。

---
