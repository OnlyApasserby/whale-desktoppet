# MV/MZ 的 CDP 通道并非总可用（双通道路由决策）

> **原编号**：`TRAP-EX1-005`　**阶段**：EX1　**来源**：原按阶段聚合的 `traps-ex1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象/命题**：ROADMAP §2.6.2 方案 A 要求 MV/MZ 走 CDP（`--remote-debugging-port`）。
  但实测与常识：**并非所有 MV/MZ 用户可传启动参数**——启动器转发参数、加壳、
  或发行版把 NW.js 参数固定，都会导致调试端口未开启；此时若无回退，该游戏永远接不上。
- **根因**：CDP 依赖「用户能给 NW.js 传参 + Chromium 调试端口可绑」两个前提，二者不由本工具控制。
- **解决或规避（决策）**：EX1.3 采用**双通路 + 回退**：
  `createGameStateAdapter` 对 MV/MZ 先判 `RpgMakerCdpAdapter::supports()`（有 `cdpPort`/`wsUrl`）
  → 用 CDP；否则判 `RpgMakerBridgeAdapter::supports()`（有 `bridge`）→ 用桥接；
  两者都缺则返回 `nullptr` + 原因（提示加 `--remote-debugging-port` 或配 `bridge`）。
  RGSS（XP/VX/VX Ace）无 CDP，直接走桥接主路径。桥接输出格式与 CDP 探测结构对齐，
  故 `RpgMakerSpecialSceneDetector` 两通道共用一份判据/滞回实现。
- **影响与关联文档**：`src/gamestate/GameStateAdapterFactory.cpp`、`RpgMakerCdpAdapter.*`、
  `RpgMakerBridgeAdapter.*`、`docs/RPGMAKER-SOP.md` §2/§3/§5；关联 `docs/ROADMAP-ex1.md` EX1.3 交付物 3。
