# MV「全屏图片」判据对角色立绘 / 图片化 UI 误判，`specialScene` 误报导致静默陪伴误触发

> **原编号**：`TRAP-EX1-012`　**阶段**：EX1（EX1.5 真机验收）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/pitfalls/index.md`。

---

- **现象（可复现步骤 / 报错原文）**：
  1. MV 游戏（`G:\1\1935`）在**普通地图**（`mapName=Map126`、`scene=Scene_Map`、可自由移动）下，
     桥接插件输出的诊断字段显示 `specialScene=1`（=`GameSpecialScene::Picture`）。
  2. 同一快照的诊断 `pics`（opacity≥200 的图片候选）实测原文（节选）：
     ```
     {"id":101,"n":"Guageback","op":255,"bw":278,"bh":60,"cover":0.021}
     {"id":150,"n":"actor01_pose01_body_0003","op":255,"bw":922,"bh":922,"cover":1.081}
     {"id":151,"n":"actor01_pose01_option_0036","op":255,"bw":922,"bh":922,"cover":1.081}
     {"id":157,"n":"actor01_pose01_cloth_0023","op":255,"bw":922,"bh":922,"cover":1.081}
     ...（共 ~12 张 922×922、cover 1.081 的立绘分层）
     ```
  3. 该游戏大量使用**图片化 UI / 立绘**插件（`PictureVariableSetting`、`Gauge`、`DTextPicture`、
     `BMSP_MapFog` 等），把角色立绘（body/cloth/face/option/semen 分层）与血条以**全屏尺寸**绘制。
  4. 结果：`context.gameState` 返回 `"specialScene":1,"silent":true` → 桌宠在普通地图上进入
     **静默陪伴**（不弹气泡、不主动播报），与「CG 才静默」的设计意图不符。
- **根因**：`RpgMakerSpecialSceneDetector`（`src/gamestate/RpgMakerSpecialScene.cpp`）的
  「全屏图片」判据为「任一图片 `opacity ≥ 200` **且** 覆盖率 ≥ `coverRatio`(0.6)」。
  该判据**只看尺寸与透明度**，无法区分「CG」与「角色立绘 / 图片化 HUD」：本案立绘位图 922×922
  相对 816×624 屏幕覆盖率达 1.081 ≫ 0.6，故必然命中。`docs/RPGMAKER-SOP.md` §2.6.2.1 已提示
  「需与图标 / UI 图区分」，但现行实现**没有**排除机制。
- **解决或规避（✅ 已修复 2026-10-07，走「可配置排除名单」口径）**：
  1. `RpgMakerSpecialSceneConfig` 新增 `ignoreNames`（精确匹配）与 `ignorePrefixes`（前缀匹配）；
     `classify()` 在「全屏图片」判据中**跳过**命中名单的图片。两者默认空 ⇒ **零回归**。
  2. 抽出**共用**配置解析 `specialSceneConfigFromJson()`（键：`rpgmaker.specialScene` 下的
     `sceneNames` / `coverRatio` / `ignoreNames` / `ignorePrefixes`），**CDP 与桥接两条通道共用**
     （此前桥接通道根本不读该配置），并删除 CDP 适配器里的重复实现，避免两份实现漂移。
  3. 桥接插件改为只输出**原始 `probe`**（scene/video/msg/face/sw/sh/pics），判据统一由 C++ 检测器
     给出（**单一真源、可单测**）；profile 置 `bridge.probe=true`。样例 profile：
     `rpgmaker.specialScene.ignorePrefixes = ["actor","Guage","DText"]`。
  - **验证（可观测）**：新增回归用例
    `test_rpgmaker_adapters::specialSceneIgnoresConfiguredPictureNames`（前缀/精确命中 → `None`；
    未命中 → 仍 `Picture`；profile 解析生效）；Debug / Release 各 **7/7** 受影响测试通过。
  - **真机复验**：目标 MV（立绘 922×922 / opacity 255 / cover 1.081）配置排除名单后，
    `context.gameState` 的 `specialScene` 由 **1 → 0**、`silent` 由 **true → false**（不再误入静默陪伴）。
- **影响与关联文档**：`src/gamestate/RpgMakerSpecialScene.cpp`、`src/gamestate/RpgMakerCdpAdapter.cpp`
  （`builtinProbeExpression`）、`RpgMakerBridgeAdapter`（`bridge.probe`）、`docs/RPGMAKER-SOP.md` §2.6.2.1 / §4；
  关联 `docs/ROADMAP-ex1.md` EX1.3（特殊场景 CG 检测）与验收项 `ACC-EX1-004`。
