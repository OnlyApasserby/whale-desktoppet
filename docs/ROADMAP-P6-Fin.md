# ROADMAP · P6 — 设置 / 打磨 / 测试 / 打包（含小游戏评估）

> ✅ 已完成（2026-10-01），全部验收通过。本阶段是 **P0–P6 的收尾阶段**；
> 后续工作见 `docs/ROADMAP-P7-Fin.md`（插件化 + 本地 Context API）。
>
> ⚠️ **本文件是 P6 当时的历史记录，其中的「小游戏不实现」结论已被 P6+ 推翻**：
> 小游戏后由「戳泡泡」改为**扫雷**，并按插件规范相继接入**找小猫**与**国际象棋**，
> 现有 **3 个小游戏插件**（`src/minigame/MiniGameRegistry.cpp`）。
> 详见 `MINIGAME-INTERFACE.md` 与 `docs/README.md` §二.3「P6+ 追加」。

## 阶段目标

收尾：完善设置面板、性能打磨、自研测试补齐、打包部署，并**评估是否实现「戳泡泡」小游戏**。

## 交付物

- `SettingsDialog`（按 `SETTINGS.md`）。
- 性能达标与「找不到看板娘」防护。
- 自研测试用例齐全（按 `TESTING.md`）。
- 可分发安装目录（windeployqt 部署，干净 PATH 可启动）。
- **小游戏评估结论**（实现 / 不实现）→ **不实现**（理由见 `MINIGAME-INTERFACE.md` §6）。
  *（P6 当时的结论；P6+ 已改为实现，见文首说明。）*

## 任务清单

1. ✅ 实现 `SettingsDialog`：陪伴表现 / 养成 / 成就墙 / 日记 / 数据与重置。
2. ✅ 唤回入口（托盘 + 左下角）+ 位置越界夹回。
3. ✅ 性能：沿用既有优化（立绘预缩放、空闲帧率降档、事件驱动落盘），并完成部署侧采样（见验证记录）。
4. ✅ 补齐并固化测试（CTest 超时 + `offscreen`）。
5. ✅ 部署：Debug/Release 分别 `windeployqt`，部署后干净 PATH 冒烟。
6. ✅ **小游戏评估**：决策为**不实现**——保持 `MiniGameRegistry` 为空，设置项以说明文案优雅降级。
   *（该决策已于 P6+ 被取代：`MiniGameRegistry` 现注册扫雷 / 找小猫 / 国际象棋 3 个插件。）*

## 验收标准

- [x] 设置面板全部开关生效并持久化。
- [x] 关闭显示后有唤回入口，位置不丢。
- [x] 全部自研测试通过（带超时）；无「伪装成功」。
- [x] Release 部署包在干净 PATH 下可启动。
- [x] 小游戏给出明确结论并记录。*（结论为「不实现」，后由 P6+ 取代。）*

## 依赖

- P1–P5 全部完成。

## 验证记录（2026-10-01）

环境：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**。

- **构建**：`--config Debug` / `--config Release` 均成功；Release 产物直接落在 `deploy-release/`。
- **测试**：`ctest -C Debug` → **9/9 passed**；`ctest -C Release` → **9/9 passed**
  （新增 `test_settings`；`test_database` 的设置往返扩展为「`json_ext` 语义往返 + 未知键保留」断言）。
- **部署 + 冒烟**：`windeployqt --release` 补齐依赖；干净 PATH
  （`C:\Windows\System32;C:\Windows`）启动成功，`deploy-release/data/whalepet.db` 存在；
  采样 `Threads=16`、`WorkingSet ≈ 109MB`。
  > 与 `BUILD.md` §9 旧基线（线程 ≈ 30 / WS ≈ 100MB）相比线程数偏低；判据以「副作用」
  > （数据库生成）为准，线程数差异**未定位、不作结论**。
- **未做项（如实说明）**：未做专门的 CPU 剖析/火焰图；「性能」项为**沿用既有优化**并做部署采样，
  非新增调优。
- **踩坑**：`traps-P6.md`（`TRAP-P6-001`/`002` 已解决；`TRAP-P6-003` 为**未复现的观察项**，不阻塞验收）。

## 完成标记

全部验收通过 → 本文件重命名为 `ROADMAP-P6-Fin.md`（P0–P6 各阶段至此完成；
后续阶段见 `ROADMAP-P7-Fin.md`）。
