# RPG Maker MV 发行版自带 NW.js 为「非 SDK 构建」，`--remote-debugging-port` 不监听（CDP 通道不可用）

> **原编号**：`TRAP-EX1-008`　**阶段**：EX1　**来源**：EX1.5 真机验收（2026-10-07）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/pitfalls/index.md`。

---

- **现象（可复现步骤 / 报错原文）**：
  1. 目标游戏 `G:\1\1935`：`www/js/rpg_core.js`（MV）、`.rpgmvp/.rpgmvo`（MV 加密格式）、
     `Game.exe` PE `Machine=0x014C`（**32 位**）、`nw.dll` 84 MB；原以 MTool 的 `inject.exe` 注入式启动（未带调试端口）。
  2. 关闭旧实例后带调试端口重启：
     `Start-Process -FilePath 'G:\1\1935\Game.exe' -ArgumentList '--remote-debugging-port=9222' -WorkingDirectory 'G:\1\1935'`。
  3. `Get-CimInstance Win32_Process` 确认**主进程命令行确含** `--remote-debugging-port=9222`；
     但 `netstat -ano | findstr :9222` **无任何监听**，且该进程组 5 个 `Game.exe` **均无任何 TCP socket**。
  4. `Invoke-WebRequest http://127.0.0.1:9222/json` → 报错原文：
     ```
     无法连接到远程服务器
     ```
  5. 追加 `--remote-allow-origins=* --enable-logging --log-file=<tmp>` 重启：Chromium 日志正常输出
     （如 `[INFO:CONSOLE] "data/Map002.json"`、`PixiJS 4.5.4` 等），但**全程无** `DevTools listening on ws://…`。
  6. 旁证：游戏目录**缺** `devtools_resources.pak`；`nw.dll` 字符串扫描 **不含** `devtools_resources.pak`
     与 `--remote-debugging` 变体（含 `remote-debugging-port`、`DevTools` 字样，但开关未生效）。
- **根因**：RPG Maker MV 官方发行版捆绑的是 **NW.js 普通（非 SDK）构建**。该构建**不启动** DevTools 远程调试服务——
  `--remote-debugging-port` 被命令行解析但不建立监听；CDP（`RPGMAKER-SOP.md` 方案 A）依赖 **SDK** 构建。
- **解决或规避（决策）**：本工具**不替换 / 不覆盖**游戏目录内的 NW.js（会修改发行版，触碰「不向游戏目录写入任何文件」红线），
  因此该类发行版 **CDP 通道不可用**，只能：
  1. 由**用户**自行以 NW.js **SDK** 构建替换游戏运行时（用户侧一次性操作，合规性自负）；
  2. 或改用**只读脚本桥接（方案 B）**：由**用户**自行在 `www/js/plugins/` 放置只读插件并在 `js/plugins.js` 启用，
     把状态写本机文件 / 回环 socket，WhalePet 侧只读该快照。
  > 另注：该游戏 `Game.exe` 为 **32 位**，即便走「外部只读内存」通道也与现行口径（x64 读 x64）不符（§4.2），
  > 故 MV 侧唯一可行通道仍只有 CDP 或桥接。
  - 结论：MV 真机 CDP 验收（`ACC-EX1-003`）在**原发行版**上**受阻**，如实登记，不顺延为「通过」。
- **影响与关联文档**：`src/gamestate/RpgMakerCdpAdapter.*`（仅在端点可用时工作）、
  `src/gamestate/GameStateAdapterFactory.cpp`（双通道路由）、`docs/RPGMAKER-SOP.md` §2/§5、
  `docs/ROADMAP-ex1.md` EX1.3 / EX1.5；**扩展** [`P-078`](P-078-rpgmaker-cdp-availability.md)
  （该条仅指出「需能给 NW.js 传参」；本条补充：**即使参数已传，非 SDK 构建仍不监听**）。
