# 桥接快照无「新鲜度」校验：游戏退出后桌宠持续展示陈旧数据

> **原编号**：`TRAP-EX1-011`　**阶段**：EX1（EX1.5 真机验收）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/pitfalls/index.md`。

---

- **现象（可复现步骤 / 报错原文）**：
  1. MV 游戏在跑、桥接插件持续写快照 `G:/1/whalepet_state.json`；WhalePet 开「游戏陪玩」，
     经命名管道查询 `context.gameState` 得 `{"available":true,"mood":"normal","confidence":0.9,...}`（正确）。
  2. **关闭游戏进程**（`Stop-Process`），等待 5 秒后再次查询：
     ```
     {"available":true,"confidence":0.9,"engine":"rpgmaker-mv","mood":"normal","silent":true,
      "sinceMs":1791380411550,"specialScene":1}
     ```
     快照文件的 `ts` 已停在 `1791380397296`（游戏退出前），但桌宠**仍然报 available=true 且沿用旧情绪**。
  3. 作为对照：把快照文件**移走**后 5 秒查询 → 正确降级
     `{"available":false,"mood":"unknown","confidence":0,"silent":false}`；文件还原且游戏重启后
     → 正确恢复 `available:true`（坐标与 `ts` 均刷新）。
- **根因**：`RpgMakerBridgeAdapter::read()`（`src/gamestate/RpgMakerBridgeAdapter.cpp:241-302`）
  只按固定键读取当前文件内容，**完全不校验时间戳 / 新鲜度**：文件只要有合法 JSON 就当作有效样本
  （且 `available` 键缺失时**默认 true**）。桥接插件在游戏退出后停止写文件，但**旧文件仍在**，
  于是「游戏已死」被读成「游戏正常」。
- **解决或规避（✅ 已修复 2026-10-07）**：
  1. **读取端新鲜度校验**：`RpgMakerBridgeAdapter::read()` 遇到快照含 `ts`（毫秒墙钟）时，
     若 `|now - ts| > kBridgeSnapshotMaxAgeMs`（新增常量，**5000 ms** = 10× 桥接默认写入周期）则判
     `available=false` 并返回失败（error 含偏差毫秒数）。比较用 `double` 且先 `std::isfinite` 过滤，
     避免 NaN / 超范围值强转 `qint64` 的未定义行为（参见 P-058）；无 `ts` 的旧快照**向后兼容**。
  2. **写入端持续心跳**：桥接插件改为**独立 `setInterval` 定时器**写快照（不再只挂
     `Scene_Map.update`），使菜单 / 战斗 / 对话等场景下 `ts` 仍持续刷新，避免误判「游戏已退出」。
  - **验证（可观测）**：新增回归用例
    `test_gamestate_boundaries::bridgeSnapshotRejectsStaleTimestamp`（新鲜可用 / 陈旧不可用且带原因 /
    无 `ts` 兼容 / 非数值 `ts` 不崩）；Debug / Release 各 **7/7** 受影响测试通过。
  - **真机复验**：关闭游戏 8 s（> 5 s 上限）后 `context.gameState` 变为
    `{"available":false,"mood":"unknown"}`；重新启动游戏后自动恢复
    `{"available":true,"mood":"normal"}`。
- **影响与关联文档**：`src/gamestate/RpgMakerBridgeAdapter.cpp:241-302`、`docs/RPGMAKER-SOP.md` §3.1/§6.3、
  `docs/ROADMAP-ex1.md` §2.9（失效自检）、EX1.4 验收标准「目标进程退出 / 桥接断开时回到正常陪伴态」；
  关联 `docs/TESTING.md`（`test_gamestate_boundaries` 现覆盖「文件为空 / 截断 / 被替换」，**未覆盖「内容陈旧」**）。
