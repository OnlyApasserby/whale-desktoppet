# RPG Maker 游戏接入 SOP（EX1.3）

> 归属：`docs/ROADMAP-ex1.md` §2.6.2（CDP 方案 A）、§2.6.2.1（特殊场景）、§2.6.3（桥接方案 B）、EX1.3。
> 适用：RPG Maker **MV / MZ**（JavaScript / V8）与 **XP / VX / VX Ace**（Ruby / RGSS）**单机**游戏。
> **红线**：全程**只读**。不注入、不 hook、不反编译改写游戏、不向其写入任何字节、不在目标进程内执行代码。
> 面对联机/带反作弊的游戏一律不接入。

---

## 0. 一句话流程

判定版本 → **MV/MZ**：加 `--remote-debugging-port` 后用 CDP 只读求值（未开调试端口则回退桥接）；
**RGSS**：用户放置**只读**桥接脚本输出状态快照 → 都由 `whalepet_gamestate` 映射为 `GameSample`。

```
RPG Maker 游戏
  ├─ MV / MZ  ──(方案 A：CDP /json + Runtime.evaluate)──→ RpgMakerCdpAdapter ─┐
  │              └─(无调试端口 → 方案 B 回退)────────────────┐              │
  └─ XP/VX/VX Ace ─(方案 B：只读脚本 → 文件/回环 socket)────→ RpgMakerBridgeAdapter ┴→ GameSample
```

---

## 1. 判定版本

| 现象（游戏目录 / 进程） | 版本 | 语言 | 通道 | 适配器 |
|---|---|---|---|---|
| `www/js/rpg_core.js`、`nw.exe`/`Game.exe`，目录 `www/` | **MV** | JS | A（CDP），可回退 B | `RpgMakerCdpAdapter` |
| `js/rmmz_core.js`、`nwjs`，目录 `js/` + `nw.js` | **MZ** | JS | A（CDP），可回退 B | `RpgMakerCdpAdapter` |
| `Game.exe` + `Data/*.rxdata`，无 `www/` | **XP** | Ruby(RGSS1) | B | `RpgMakerBridgeAdapter` |
| `Game.exe` + `Data/*.rvdata` | **VX** | Ruby(RGSS2) | B | `RpgMakerBridgeAdapter` |
| `Game.exe` + `Data/*.rvdata2` | **VX Ace** | Ruby(RGSS3) | B | `RpgMakerBridgeAdapter` |

---

## 2. 方案 A：MV / MZ 走 CDP 只读求值

### 2.1 以调试端口启动

MV/MZ 基于 NW.js（Chromium），支持远程调试：

```
Game.exe --remote-debugging-port=9222
```

启动后浏览器访问 `http://127.0.0.1:9222/json` 应能看到目标列表（含 `webSocketDebuggerUrl`）。

> `CdpWebSocketClient::discoverWebSocketUrl(9222, &url, &err)` 即读取该 `/json`。
> 若游戏用启动器/加壳无法传参，请直接走 §3 桥接。

### 2.2 profile 配置（`rpgmaker` 段）

```json
{
  "engine": "rpgmaker-mv",
  "rpgmaker": {
    "cdpPort": 9222,
    "expressions": {
      "gold": "$gameParty._gold",
      "hp": "$gameParty.members()[0]._hp",
      "hpMax": "$gameParty.members()[0].mhp",
      "level": "$gameParty.members()[0]._level",
      "posX": "$gamePlayer.x",
      "posY": "$gamePlayer.y",
      "mapName": "$gameMap.displayName()"
    },
    "specialScene": {
      "expression": "",
      "sceneNames": ["Scene_CG", "Scene_Gallery"],
      "coverRatio": 0.6
    }
  }
}
```

- `wsUrl`（可选）可替代 `cdpPort`，直接给 `ws://127.0.0.1:9222/devtools/page/<id>`（跳过 `/json` 发现）。
- `expressions` 的 **key** 必须是被识别的字段名：`hp / hpMax / gold / level / posX / posY / mapName`。
- 每个表达式都经 `Runtime.evaluate(..., returnByValue=true)` 只读求值，**绝不**发送 `Input.*` /
  `Runtime.callFunctionOn` / `Page.*` 等可改动状态的域。

### 2.3 MZ 字段名差异

MZ 起 `mhp`（最大 HP）等属性改为访问器/对象属性，但 `$gameParty._gold`、`$gamePlayer.x/y`、
`$gameMap.displayName()` 仍通用。若某表达式在 MZ 报错，按 MZ 源码微调（如
`$gameParty.members()[0].mhp` → `$gameParty.members()[0].mhp` 或 `._paramPlus` 组合）。

---

## 3. 方案 B：桥接（RGSS 主路径；MV/MZ 无调试端口时回退）

桥接 = **用户侧只读脚本**把游戏状态**输出**为本地 JSON/JSONL 快照；本工具只**读**该快照。
脚本由用户自行放置；本工具**不注入、不改游戏文件**。

### 3.1 输出格式（快照）

```json
{"available":true,"hp":88,"hpMax":120,"gold":1234,"level":5,"posX":3.5,"posY":7,"mapName":"Fort","specialScene":0}
```

- 字段名同 §2.2（`hp / hpMax / gold / level / posX / posY / mapName / specialScene`），缺失即不改写。
- `specialScene` 为 `0 无 / 1 图片 / 2 专用场景 / 3 影片 / 4 对话演出`；可直接给整数，
  或改输出 `"probe": {…}` 由 `RpgMakerSpecialSceneDetector` 做滞回判定（见 §4）。
- `available:false` 表示目标不可用（**不伪造数据**）。

### 3.2 profile 配置（`bridge` 段）

```json
{
  "engine": "rpgmaker-rgss",
  "bridge": { "kind": "file", "path": "F:/Games/MyGame/whalepet_state.json", "format": "json" }
}
```

| 键 | 取值 | 说明 |
|---|---|---|
| `kind` | `file` \| `socket` | 文件（轮询整文件）/ 回环 socket（`host:port`，按行读取） |
| `path` | 文件绝对路径 或 `127.0.0.1:port` | 快照位置 |
| `format` | `json` \| `jsonl` | `jsonl` 时取**最后一行**非空 JSON |
| `probe` | `true` \| 省略 | 快照含 `probe` 对象时启用滞回检测（§4） |

### 3.3 RGSS 只读桥接脚本样例（用户自行放置）

RGSS 无内建 JSON 库的版本较多，样例**手动拼串**以避免依赖；仅读取全局对象，**不修改任何游戏数据**。

```ruby
# WhalePetBridge.rb —— 只读桥接：仅在 Scene_Map#update 中读取并写出状态快照
module WhalePetBridge
  PATH     = Dir.pwd + "/whalepet_state.json"
  INTERVAL = 15   # 每 15 帧写一次

  def self.esc(s)
    s.to_s.gsub('\\', '\\\\').gsub('"', '\\"')   # mapName 的 JSON 转义
  end

  def self.dump
    return unless $game_party && $game_player
    a = $game_party.actors[0]
    json = sprintf(
      '{"available":true,"gold":%d,"hp":%d,"hpMax":%d,"level":%d,"posX":%d,"posY":%d,"mapName":"%s","specialScene":0}',
      $game_party.gold,
      (a ? a.hp : 0), (a ? a.maxhp : 0), (a ? a.level : 0),
      $game_player.x, $game_player.y,
      esc($game_map ? $game_map.name : "")
    )
    File.open(PATH, "w") { |f| f.write(json) } rescue nil
  end
end

# 逐帧挂钩（alias 不改变原逻辑，只在更新前计数）
class Scene_Map
  alias whalepet_update update
  def update
    @whalepet_frames = (@whalepet_frames || 0) + 1
    WhalePetBridge.dump if (@whalepet_frames % WhalePetBridge::INTERVAL).zero?
    whalepet_update
  end
end
```

放置形态（三选一，按用户接受度）：游戏自带扩展点 / 脚本追加 / 重新打包 `Data/*.rxdata*`。
**本工具不代做任何打包或注入**。

> MV/MZ 若无调试端口，也可用 MV/MZ 插件（`js/plugins/`）等价的只读脚本写同一份 JSON，
> 然后把 profile 的 `engine` 设为 `rpgmaker-mv` / `rpgmaker-mz` 并只配 `bridge`——工厂会自动
> 选择桥接适配器（见 §5）。

---

## 4. 特殊场景（CG）判定口径（§2.6.2.1）

统一由 `RpgMakerSpecialSceneDetector` 判定（CDP 探测与桥接 `probe` 共用），**优先级**：

1. **影片**：探测 `"video": true`（`SceneManager._scene` 构造名含 `Video`）→ `3`
2. **专用场景名单**：`"scene"` 命中 `sceneNames`（如 `Scene_CG`）→ `2`
3. **全屏图片**：任一图片 `opacity ≥ 200` 且覆盖率 `(bw*sx/100)*(bh*sy/100)/(sw*sh) ≥ coverRatio` → `1`
4. **对话演出**：`msg` 忙且有 `face` 立绘名 → `4`
5. 否则 `0`

**滞回**：连续 `kGameSpecialSceneEnterFrames`(=3) 帧成立才**置位**；
连续 `kGameSpecialSceneExitFrames`(=3) 帧不成立才**复位**（抗抖，避免一闪而过误触发静默）。

CDP 下 `probe` 由内置只读表达式产出（`RpgMakerCdpAdapter::builtinProbeExpression()`），
不需用户编写；若需自定义，写入 `rpgmaker.specialScene.expression` 覆盖即可（返回同一结构）。

非 0 → 桌宠进入**静默陪伴**（不弹气泡、不主动播报，仅保留立绘与既有点击交互）。

---

## 5. 降级与排错

| 现象 | 原因 | 处理 |
|---|---|---|
| `attach` 报「缺少 CDP 端点」 | 未配 `cdpPort/wsUrl` 且无 `bridge` | 加调试端口启动，或改用桥接 |
| `attach` 报「连接失败」+ 提示加 `--remote-debugging-port` | 端口未监听 / 地址无效 | 核对 §2.1；确认 `/json` 可访问 |
| 连续失败后报「档案已失效」 | 引擎无响应（切场景/卡死/关闭） | 恢复后调用 `RpgMakerCdpAdapter::reconnect()` |
| 桥接读不到数据 | 路径/端口错、脚本未放置、格式不匹配 | 核对 §3.2；确认快照文件内容为合法 JSON |
| `specialScene` 误判 | `coverRatio`/名单不合 | 调整 `sceneNames` 与 `coverRatio`；确认滞回帧数 |

**优雅降级**：任何通道失败都返回 `available=false` + 明确原因（`QString *error`），
**不崩溃、不影响桌宠**；工厂在两端点都缺时返回 `nullptr` + 原因。

---

## 6. 只读红线自检（人工验收前逐条确认）

- [ ] 未向游戏目录写入任何文件（方案 A 全程只读；方案 B 只**读**用户脚本产物）。
- [ ] CDP 只发 `Runtime.evaluate`，未发 `Input.*` / `Page.*` / `Runtime.callFunctionOn`。
- [ ] 未对 V8 / Ruby 堆做原始内存扫描（方案 C 未启用）。
- [ ] 桥接脚本仅读取全局对象，无任何赋值/调用会产生副作用的接口。
- [ ] 关闭游戏陪玩功能时不打开任何进程、不建立任何连接（零开销）。
