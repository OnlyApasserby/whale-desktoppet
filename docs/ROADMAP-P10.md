# ROADMAP · P10 —— 工作能力插件（预制 Python 块工作台 + 戳一戳彩蛋迁移）

> 阶段性质：**主阶段（可交付里程碑）**；裁决与架构口径见 [`ARCHITECTURE.md`](ARCHITECTURE.md) **附录 D**
> （2026-10-11 经用户确认）。
> 阶段归档：[`pitfalls/p10/`](pitfalls/p10/)；本文件在阶段收尾时按项目约定决定是否追加 `-Fin`。

## 1. 阶段目标（可交付）

桌宠获得一个可交互的「工作」能力：**勾选预制 Python 功能块 → 桌宠工作一段时间 → 到点把拼装好的
脚本写到用户指定目录**；同时把「戳一戳」彩蛋**迁移**进该能力（工作期间**至多一次**，把俏皮话写进
**我们自己生成的脚本**，不再改写用户源码）。构建与部署目录同时收敛为单一 `build/`。

## 2. 范围与红线（摘要，全文见附录 D.2）

- **新增**：`src/core/PythonBlocks.{h,cpp}`、`src/core/MenuInteraction.h`、
  `src/viewmodel/WorkService.{h,cpp}`、`src/viewmodel/builtin/WorkServicePlugin.{h,cpp}`、
  `src/view/ui/WorkBenchPanel.{h,cpp}`、`assets/python/blocks/index.txt` + 9 个块正文、
  `tests/test_python_blocks.cpp`。
- **改动**：`core/PetTypes.h`、`core/PetStateMachine.{h,cpp}`、`viewmodel/PetController.{h,cpp}`、
  `view/PetWindow.{h,cpp}`、`model/SettingsData.h`、`model/SettingsRepo.cpp`、
  `view/SettingsDialog.{h,cpp}`、`viewmodel/builtin/BuiltinServicePlugins.{h,cpp}`、
  `assets/assets.qrc`、`assets/lines/lines.txt`、`cmake/Libraries.cmake`、`cmake/Tests.cmake`、
  `tests/{test_state_machine,test_service_plugins,test_code_easter_egg}.cpp`。
- **退役**：`viewmodel/EasterEggService.{h,cpp}`、`viewmodel/builtin/EasterEggServicePlugin.{h,cpp}`
  （整体删除）；`core/CodeEasterEgg` 瘦身为「话池 + `eggCommentLine()`」；
  设置键 `code_egg_enabled` / `code_egg_workspace` 停用（仅保留在库中）。
- **红线**：不引入 Python 解释器依赖、不执行生成的脚本、不联网、不再改写**用户源码工作区**；
  不改 `context.workState` 语义（感知 ≠ 自产）；不改小游戏插件族与结算；不新增 qrc 之外的随包清单。

## 3. 交付物与状态

| # | 交付物 | 状态 | 证据 |
|---|---|---|---|
| ① | 构建与部署目录收敛（固定 `build/`；删除 `build-package/`、`deploy-release/`） | ✅ 2026-10-11 | 发布脚本复用 `build` 并重出三产物（§8.2 第 1～9 项）；踩坑 `P-105` / `P-106` |
| ② | 状态机「桌宠互动（菜单选项）」通道 + 插件接口 | ✅ 2026-10-11 | `contextPose()` 第 1 档（**仅低于深夜**）；`PetController::handleMenuInteraction()`；`test_state_machine::menuInteractionOutranksWorkAndSlot` / `menuInteractionYieldsToLateNight` |
| ③ | 工作能力插件（块库 / 工作台面板 / 会话 / 产出） | ✅ 2026-10-11 | `core::PythonBlocks` + `assets/python/blocks/`（唯一索引 + 9 块）+ `viewmodel::WorkService` + `builtin::WorkServicePlugin`（`service.work`）+ `ui::WorkBenchPanel`；`tests/test_python_blocks` |
| ④ | 戳一戳彩蛋迁移 + `work.*` 设置键 | ✅ 2026-10-11 | `WorkService::onPoke()`（**会话内至多一次**）+ 产出时插 `whalepet-egg` 注释；旧服务 / 插件删除、话池瘦身；`work_enabled` / `work_output_dir` / `work_duration_min`（`SETTINGS.md` §2） |

## 4. 验收条件（A1~A6）

| 项 | 判据 | 结果 |
|---|---|---|
| A1 构建通过 | Debug 与 Release 构建退出码均 0（复用既有 `build/`，不新建） | ✅ 2026-10-11 |
| A2 测试通过 | Debug / Release CTest 各 **36/36**（新增 `test_python_blocks`，既有 35 项零回归） | ✅ 2026-10-11 |
| A3 产物可运行 / 可部署 | 发布脚本回归：三产物重出（portable / zip / setup），`TESTING.md` §8.2 第 1～9 项通过（干净 PATH + 真实平台存活 12s 且生成 `data/whalepet.db`）；**第 10 项安装包静默往返需 UAC → 待人工验收** | ✅ / ⏳ |
| A4 文档与踩坑已刷新 | 本文件 + `ARCHITECTURE.md` 附录 D.5 + `STATE-MACHINE.md` §1.2 + `SETTINGS.md` §2 + `TESTING.md` §2 + `PLUGIN-ARCHITECTURE.md` 服务清单 + `CONTEXT-API.md` 能力表 + `README.md`（根 / `docs/`）+ `release.md` | ✅ |
| A5 踩坑已登记 | `docs/pitfalls/p10/`（`P-105` … `P-109`）并刷新 `docs/pitfalls/index.md`（阶段表 + 索引 + 合计 109） | ✅ |
| A6 用户确认 | **待用户目视验收**：工作台面板勾选 / 产出脚本内容与目录 / 工作期常驻立绘 / 工作期间戳一戳插俏皮话（至多一次）/ 深夜让位行为 | ⏳ |

## 5. 明确不做（本阶段）

- **运行 / 调试生成的脚本**：需要外部 Python 解释器（`QProcess` 范式可参考国际象棋 UCI 引擎），
  若要做需重新裁决并新增依赖口径；
- **功能块的外部自定义 / 在线更新**：当前块库编译进 qrc（唯一索引 + 正文），用户侧不可增删；
- **工作会话与养成系统联动**（奖励 / 成就 / 每日次数上限）：本阶段未纳入。
