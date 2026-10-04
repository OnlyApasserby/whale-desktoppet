# ROADMAP · P1 — 工程骨架与桌宠外壳

> 未完成阶段，文件名不含 `Fin`；完成后重命名为 `ROADMAP-P1-Fin.md`。
>
> **状态说明（文档整理时按源码静态核对，2026-10-02）**：本文件的「任务清单」与「验收标准」
> 是**规划期**的原始文本，未随实施更新（下方复选框因此全部是未勾选的历史占位，**不代表当前未实现**）。
> 实际状态以 `docs/README.md` §二.3 为准：**实现与自动化验证完成，人工目视项待复验 → 暂不含 `Fin`**。
>
> 静态可见的落地证据：
>
> | 交付物 | 代码 / 测试位置 |
> |---|---|
> | CMake 工程与预设 | 顶层 `CMakeLists.txt` + `cmake/`（模块拆分）+ `CMakePresets.json` |
> | 透明置顶外壳 / 拖拽 / 右键菜单 / 托盘 | `src/view/PetWindow.{h,cpp}`、`src/app/main.cpp` |
> | 立绘显示（webp） | `src/view/PoseView.{h,cpp}`、`src/view/PoseLibrary.{h,cpp}`、`assets/poses/`（93 张） |
> | 自动化验证 | `tests/test_smoke.cpp`（目标 `test_smoke`，已注册 CTest） |
>
> 是否补做人工验收并改签为 `ROADMAP-P1-Fin.md`，由项目 owner 决定；
> 本次文档整理**不代为判定验收通过**。

## 阶段目标

在固定基线上跑通一个「透明、无边框、置顶、可拖拽」的桌宠外壳，并能显示一张 webp 立绘。

## 交付物

- 可运行的 `WhalePet.exe`：透明置顶窗口 + 静态立绘显示 + 拖拽 + 右键菜单 + 退出。
- `CMakeLists.txt` / `CMakePresets.json`（按 `BUILD.md` 基线）。
- 最小目录骨架（`src/{app,view,viewmodel,core,model,common}`）。

## 任务清单

1. 建立 CMake 工程：`qt_standard_project_setup()`，链接 `Core Gui Widgets Sql Test`。
2. 确认 `qwebp` 图像插件可用（否则按 `BUILD.md` 报错）。
3. 实现 `PetWindow`：透明背景 + 无边框 + 置顶 + `Qt::Tool`。
4. 实现 `PoseView`：加载并显示单张 webp 立绘（默认 `idle-cute`）。
5. 实现拖拽（移动 > 4px 才进入拖拽）。
6. 实现右键菜单骨架：投喂 / 戳一下 / 夸夸 / 回原位 / 设置 / 退出（先接空实现）。
7. 位置持久化到临时配置文件（P3 换 SQLite）。
   *（**该需求后已废止**：P3 先迁到 SQLite，再由 `traps-extend0.md` `TRAP-EXT0-002` 取消位置持久化
   ——现为「每次启动居中 + 运行期夹回可见区域」，`pos_x/pos_y` 列保留但不再读写。）*
8. 系统托盘图标 + 退出。

## 验收标准

- [ ] 双击 exe 后桌面出现鲸鱼娘，无方框、置顶、不抢焦点。
- [ ] 可拖拽移动，松手位置可保存并在重启后恢复。
      *（**该条已废止**：见任务 7 说明与 `traps-extend0.md` `TRAP-EXT0-002`；
      现行判据是「启动恒居中且不越界」，由 `test_smoke::petWindowStartsCenteredOnPrimaryScreen` 守卫。）*
- [ ] 右键菜单可用，退出正常。
- [ ] 空闲 CPU < 5%、内存 < 30MB、启动 < 0.5s（初步达成）。
- [ ] Debug/Release 均可构建并运行（windeployqt 部署后干净 PATH 可启动）。

## 依赖

- P0（设计文档）。
- `BUILD.md` 基线（Qt 6.8.4 / MSVC / CMake 4.4.2）。

## 踩坑记录

本阶段实施中**真实遇到并解决**的问题记录于 `traps-P1.md`（命名约定见 `README.md` §二.5）。
截至当前已记录：`Q_INIT_RESOURCE` 命名空间问题、`AUTORCC` 未开启、`windeployqt --dir` 不复制 exe 等。

## 完成标记

人工验收通过后 → `ROADMAP-P1-Fin.md`（当前仍未改签：人工目视项待复验，
见 `docs/README.md` §二.3）。
