# 设置面板设计（SETTINGS）

> 去宿主化：whale 的「注入式设置 slot」**重做**为独立 Qt 设置对话框。

## 1. 形态

- 独立非模态 `QDialog`（`SettingsDialog`），由右键菜单 / 托盘打开。
- 分组折叠或标签页布局，样式遵循工作区 Qt UI 规范。

## 2. 设置项清单

| 分组 | 设置项 | 键 | 默认 |
|---|---|---|---|
| 陪伴表现 | 桌宠显示开关 | `pet_enabled` | 开 |
| | 台词气泡 | `bubble_enabled` | 开 |
| | 粒子/特效 | `particles_enabled` | 开 |
| | 关键词感知 | `keyword_aware` | **关** |
| | 深夜静默 | `night_quiet` | 开 |
| | 拖拽惯性 | `drag_inertia` | 开 |
| | 回收站清理提醒（`sweep` 立绘；随机轮询，非空时气泡 + 托盘提醒，见 `README.md`） | `recycle_bin_reminder_enabled` | 开 |
| | 立绘尺寸 | `pose_size` | 200 |
| 养成与日常 | 每日任务 / 周签到 / 称号（展示 + 领取） | — | — |
| 成就墙 | 39 项成就展示（高亮/灰显） | — | — |
| 成长日记 | 最近 12 条，倒序 | — | — |
| 小游戏 | 全局开关（关闭后隐藏所有游戏入口） | `minigame_enabled` | 开 |
| | 已注册插件列表 + 各插件「上次配置」摘要 | `json_ext` | 初级 |
| 智能感知（P7） | 工作状态感知（前台应用 + 输入活跃度，**只计数不含内容**） | `work_aware_enabled` | **关** |
| | 本地 Context API 总开关（关闭时不监听任何端口、不注册上下文能力） | `context_api_enabled` | **关** |
| | Context API 端口（0 = 系统分配；始终只绑定 127.0.0.1） | `context_api_port` | 0 |
| | Context API 令牌（**HTTP 通道强制非空**；空 = 只启用命名管道、不监听 HTTP 端口。组合根在启用时自动生成并落盘） | `context_api_token` | 空（启用时自动生成） |
| | ACP / IDE 显式信号（关闭时**不轮询**信号文件，零开销） | `acp_enabled` | **关** |
| | ACP 信号文件路径（空 = 数据目录下 `acp-signals.jsonl`） | `acp_signal_path` | 空 |
| | ACP：dsh 入口（`lib/bin.js` 绝对路径；空 = **不启动** ACP 子进程） | `acp_dsh_path` | 空 |
| | ACP：dsh profile 名（空 = `acp`，其 ACP 走 stdio） | `acp_profile` | 空 |
| | ACP：会话工作目录（空 = 数据目录） | `acp_workspace` | 空 |
| 预设问答（P8） | 「我可以提问（主人的问题）」低频提醒开关（仅在静息时弹出，见 `DIALOGUE.md` §5） | `dialogue_enabled` | 开 |
| | 彩云天气 API key（**空 = 完全不联网**） | `weather_key` | 空 |
| | 天气城市（城市名如「上海」，或经纬度如 `116.23,39.93`；空 = 不联网） | `weather_location` | 空 |
| 数据与重置 | 重置位置 / 重置养成数据 / 打开数据目录 | — | — |

> 说明：whale 的「余额 / TTS / 无障碍 / 主题」分组**全部移除**；
> 「天气」在 P8 以**彩云天气**（仅用于预设对话的天气题类型判定）重新引入，
> 且沿用在参考项目中确立的口径：**key 或城市为空 = 完全不联网**（`DIALOGUE.md` §5）。

## 3. 存储

- 常规设置存 `settings` 表（单例行）；新增项写入 `json_ext`（JSON），保证向后兼容。
- 变更即时落库（见 `DATA-MODEL.md`）。

## 4. 交互约定

- 开关采用胶囊样式；「数据与重置」操作需二次确认。
- 「打开数据目录」用 `QDesktopServices` 打开 `data/`。
- 「重置位置」把桌宠移回当前主屏正中央（每次启动也会自动居中，见 `PRESENTATION.md` §3）。

## 5. 「找不到看板娘」防护

- 关闭桌宠显示后，**托盘/左下角保留唤回入口**（沿用 whale 🐋 思路），点击恢复显示。
- 位置越界时自动夹回可见区域，避免「跑到屏幕外找不到」。

## 6. 实现状态（P6）

| 交付项 | 代码位置 |
|---|---|
| 设置对话框（标签页式，非模态） | `src/view/SettingsDialog.{h,cpp}` |
| 陪伴表现：6 个开关 + 立绘尺寸 | `SettingsDialog::buildAppearanceTab` |
| 日常 / 成就墙 / 成长日记（内嵌复用 P4 面板） | `ContentPanel::embedInto`（独立窗口走 `showStandalone`） |
| 小游戏（插件化）：全局开关 + 按注册表列出各插件（名称 / 上次配置 / 开始） | `SettingsDialog::buildMiniGameTab`、`PetWindow::showMiniGame`、`src/minigame/**` |
| 数据与重置（重置位置 / 打开数据目录 / 重置养成，二次确认） | `SettingsDialog::buildDataTab` + `PetWindow` |
| 设置持久化（含 `json_ext` 扩展键） | `SettingsRepo::load` / `save` |
| 设置生效（尺寸 / 粒子 / 惯性 / 气泡 / 深夜静默 / 显隐） | `PetWindow::applySettings` |
| 唤回入口（托盘菜单 + 左下角浮动按钮） | `PetWindow::setupTray` / `setupRecallEntry` |
| 启动居中（分辨率 / 显示器变化后仍居中） | `PetWindow::defaultPosition` + `PetWindow::showPet` |
| 位置越界夹回 | `PetWindow::clampToVisibleArea`（启动 + 松手 + 运行期屏幕变化时，见 `PetWindow::watchScreenChanges`） |

**落库位置**（按 §3 约定）：

- `pose_size` / `bubble_enabled` / `particles_enabled` / `keyword_aware` / `minigame_enabled` → `settings` 表既有列；
- `pet_enabled` / `night_quiet` / `drag_inertia` / `recycle_bin_reminder_enabled` → `json_ext`（JSON），
  **不新建列**，保留未知键向后兼容；
- 小游戏难度 / 配置：扫雷（`minigame_preset` / `minigame_custom_width` / `minigame_custom_height` /
  `minigame_custom_mines`）、鲸鱼娘找小猫（`kitten_difficulty`）与国际象棋
  （`chess_engine_path` / `chess_difficulty` / `chess_human_is_white`）→ `json_ext`，同样不新建列；
  **接 Token 不落库任何配置**（难度每次在窗口内选择，插件的 `configSummary()` 只回报三档口径，
  见 `docs/MINIGAME-INTERFACE.md` §12.3）；
- P7 智能感知：`work_aware_enabled` / `context_api_enabled` / `context_api_port` /
  `context_api_token` → `json_ext`（缺省即默认值；未知键保留）；
- P7.5 ACP：`acp_enabled` / `acp_signal_path` → `json_ext`（同样缺省即默认值，**不新建列**）；
- P7.6 ACP 客户端：`acp_dsh_path` / `acp_profile` / `acp_workspace` → `json_ext`（同上）。

**验证**：`ctest -C Debug` / `-C Release` 均 **9/9 通过**（新增 `test_settings`）；部署与冒烟结论见 `ROADMAP-P6-Fin.md`。

---

## 7. 智能感知与 Context API（P7）

| 交付项 | 代码位置 |
|---|---|
| 设置项读写与缺省 | `src/model/SettingsData.h`、`src/model/SettingsRepo.cpp`（`json_ext`） |
| 运行期生效与门控（默认关：不采样、不监听） | `PetWindow::applyWorkStateSettings` / `setWorkAware` / `setContextApiEnabled` |
| 开关入口（Phase 1 走右键菜单勾选项） | `PetWindow::setupContextMenu`（`工作状态感知` / `本地 Context API`） |
| 采样与判定链路 | `src/platform/**`（P7.1 起含 Win32 真实采集）、`src/viewmodel/EnvironmentService.*`、`src/viewmodel/WorkStateService.*` |
| 通道与能力 | `src/contextapi/**`（详见 `CONTEXT-API.md`；三通道（HTTP 回环 / MCP stdio / 命名管道）与 MCP Client、ACP 均已实现，见下述 P7.2 / P7.4 / P7.5 / P7.6） |
| MCP Server 命名管道 + 桥接 exe（P7.2） | `src/contextapi/transport/LocalPipeTransport.*`、`src/contextapi/ContextApiService.*`、`src/app/mcp_bridge_main.cpp`（目标 `whalepet-mcp`）；随「本地 Context API」总开关一并启停 |
| MCP Client / 外部进程插件（P7.4） | `src/plugin/process/**`；组合根 `PetWindow::setupProcessPlugins`（配置来源 `<数据目录>/plugins.json`，不存在则零开销） |
| ACP 显式信号（P7.5） | `src/contextapi/acp/**` + `src/viewmodel/AcpSignalService.*`；组合根 `PetWindow::setupAcp` / `setAcpEnabled` |
| ACP 客户端（P7.6） | `src/contextapi/acp/AcpClient.{h,cpp}`；组合根 `PetWindow::startAcpClient` / `attachAcpSession`（以 `node <bin.js> --profile <name>` 拉起 dsh，stdio NDJSON） |

- **默认均为关**：`work_aware_enabled = false` 时不采样；`context_api_enabled = false` 时不监听任何端口、
  且不注册 `context.*` 能力（`capabilities.list` 里也不会出现）。
- 运行期关闭 Context API 时，已注册的上下文能力会被**标记为不可用**（能力无法从注册表移除，
  故用可用性如实地表达「当前不可用」，调用返回 `-32002`）。
- Phase 1 未在设置面板内新增控件（避免与 §4 的既有布局冲突），开关先落在右键菜单；
  后续阶段再并入设置面板。

**验证（P7.0 / P7.1）**：`test_settings` 继续覆盖 `json_ext` 语义往返与未知键保留；新增设置项的
缺省值、读写与门控由 `test_context_dispatch`（门控）与 `test_platform_skeleton`（无数据降级）覆盖。
`ctest -C Debug` / `-C Release` 各 **17/17 通过**。

**P7.4 / P7.5 补充（2026-10-02）**：`acp_enabled` 缺省为 **关**（关闭时不轮询信号文件）；
`acp_signal_path` 为空时使用数据目录下 `acp-signals.jsonl`。外部进程插件**无独立设置项**，
配置来源固定为 `<数据目录>/plugins.json`（不存在则不启动任何外部进程）。新增
`test_acp` / `test_process_plugin` 后，`ctest -C Debug` / `-C Release` 各 **20/20 通过**。

**P7.1 补充**：`work_aware_enabled = false`（默认）时**不采样、不安装任何系统钩子**——
低层输入钩子只在勾选后由 `EnvironmentService::start()` → `setObserving(true)` 安装，
取消勾选 / 退出时由 `stop()` / 观察者析构卸载（`docs/ROADMAP-P7-Fin.md` P7.1）；
前端行为（立绘 / 台词随工作状态变化）属人工目视项。

**P7.6 补充（2026-10-02）**：新增 `acp_dsh_path` / `acp_profile` / `acp_workspace` 三个 `json_ext` 键
（§2 表），缺省均为空 = **不启动 ACP 子进程**；启用入口仍是既有的「ACP / IDE 信号」勾选项
（`acp_enabled`），勾选后由 `PetWindow::startAcpClient` / `attachAcpSession` 拉起并接管会话。
新增 `test_acp_event_mapper` / `test_acp_client` 后，`ctest -C Debug` / `-C Release` 各 **22/22 通过**。

---

## 8. 预设问答与彩云天气（P8）

| 交付项 | 代码位置 |
|---|---|
| 设置项读写与缺省（`dialogue_enabled` / `weather_key` / `weather_location`） | `src/model/SettingsData.h`、`src/model/SettingsRepo.cpp`（`json_ext`） |
| 面板控件（「预设对话」分组：开关 + key + 城市，`editingFinished` 即时落库） | `src/view/SettingsDialog.cpp::buildAppearanceTab` / `persist` / `reload` |
| 运行期生效 | `PetWindow::applyDialogueSettings`（配置变化即刷新一次天气）/ `setDialogueEnabled` |
| 开关入口（右键菜单） | `PetWindow::setupContextMenu`（`我可以提问（主人的问题）` 勾选项 + `我想问鲸鱼娘…`） |
| 天气请求与判定 | `src/viewmodel/WeatherService.*`、`src/core/WeatherRules.*` |
| 敏感题解锁与每日配额（好感度 5000 / 每日 3 次） | `core::kSensitiveUnlockAffinity` / `kSensitiveDailyLimit`；`DialogueService` + `meta` 键 `dialogue.sensitive_*` |

- `dialogue_enabled` 缺省为**开**：仅在静息（非工作 / 非深夜 / 气泡空闲 / 桌宠可见）时低频提醒
  （15–30 分钟随机一档），关闭后立即停止定时器并撤下正在显示的面板；
- `weather_key` / `weather_location` 缺省为空 = **完全不联网**，且此时**天气问题槽位不可用**；
  两者都填写后才会请求彩云天气，成功结果缓存 30 分钟、失败静默退避 60 分钟；
- 天气只用于**问答面板的天气问题**（类型判定 → 回答 + 立绘），不做独立天气卡片 / 多城市 / 手动刷新。

**验证**：`test_preset_dialogue`（时段 / 天气类型 / 工作立绘池 / 语料解析 / 五选一槽位与可用性 /
回答选取），`ctest -C Debug` 全量 **35/35 通过**（2026-10-04 复跑；见 `TESTING.md` §2）。
