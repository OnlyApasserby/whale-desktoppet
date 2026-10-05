# 测试策略（TESTING）

> 不沿用参考项目的测试体系（GTest / node:test）；**自行编写测试用例**，基于 **Qt6::Test**。面向个人使用，聚焦核心逻辑正确性。

## 1. 原则

- **只测核心逻辑**（`core/` + `model/`），UI 交互以最小冒烟为主。
- 所有测试命令**必须带超时**（禁止裸跑）：`ctest ... --timeout 120`，并在 CMake 中固化 `TIMEOUT`。
- 无显示环境用 `QT_QPA_PLATFORM=offscreen`；仍不可用则 `SKIP_RETURN_CODE 77` 并写明原因。
- 禁止以删除断言 / 注释用例 / 放宽比较 / 吞异常的方式让结果「变绿」。

## 2. 测试对象与用例

| 模块 | 用例要点 |
|---|---|
| `PetStateMachine` | 各事件转移；`AFK`/`SUCCESS`/`CURIOUS` 窗口超时回落；**主动说话**节流（≥6s）与**用户交互不节流**；表现批次序号（同一缓存态重放不改号、新事件递增、回落归零）；深夜静默；不打断规则；**静息态节日换装**（`core::FestivalRules` 的 5 个节日日期与换装让位规则，见 `STATE-MACHINE.md` §5.1）。随机源用**固定序列 RNG**保证可复现 |
| `workStateIsBusy`（工作 / 未工作分类） | 与参考 `BUSY_STATES` 对齐的两分法：`Reading`/`Coding`/`VibeCoding`/`Debugging`/`Meeting` 为忙，`Unknown`/`Idle`/`Browsing`/`Game`/`Afk` 为未工作；`WorkState::Idle` 立绘对齐 `idle-cute` |
| `GrowthService` | 经验/升级曲线；心情/饱食边界（0/100 夹取）；饱食随时间衰减；连续签到跨天判定 |
| `AchievementService` | 39 项判定条件；重复解锁幂等；解锁写日记 |
| `QuestService` | 每日 3 槽刷新（跨 `day_key`）；领取幂等；进度累加 |
| `SigninService` | 周签到格；1/3/7 里程碑奖励 |
| `ChatService` | 场景选词；最近 N 条去重；关键词→`meme-*` 映射；开关关闭时不触发 |
| `Database` | 建表/迁移；单例读写；事务回滚；安装目录不可写时的降级路径 |
| `LineTable` | 台词文件解析；缺失文件降级（空表 + 日志） |
| `ChessGame`（国际象棋） | FEN 往返；合法着法（含王车易位 / 吃过路兵 / 兵升变）；将军 / 将死 / 逼和 / 和棋；UCI 着法串互转；非法着法拒绝；结算折算与难度表 |
| `CapabilityRegistry` / `PluginRegistry`（P7） | 插件注册（空 id / 重复 id）、能力冲突仲裁（Builtin > Dll > Process）、同步失败与异步受理的**契约区分**、生命周期容错、内置层装载器、小游戏兼容适配 |
| `WorkStateRules`（P7） | 应用类别归一化、各工作状态判据、Coding vs Vibe Coding、置信度阈值、最短驻留滞回、无数据立即降级；P7.1 追加：真实输入画像回归、会话暂停（锁屏 / 屏保）优先于「无数据」|
| 感知层（P7） | 空实现恒「无数据」、组合观察者的类别/切换/停留/滚动窗口、采集失败不伪造数据 |
| 真实 Win32 感知（P7.1） | 宽字符→UTF-8 / 路径取进程名；三个采样器（前台 / 输入 / 系统状态）在注入替身读数下的填值、失败不伪造、差分降级每次至多计 1；注入替身**绝不安装系统钩子**；锁屏 + 读不到前台窗口 → `afk` |
| `JsonRpcDispatcher` / 三通道（P7） | JSON-RPC 2.0 校验与错误码、能力别名路由、门控与回环绑定、MCP 方法映射、三通道（HTTP 回环 / stdio / 命名管道）结果一致 |
| 命名管道 + MCP 桥接（P7.2） | 命名管道承载**完整 MCP 会话**（`initialize` / `tools/list` / `tools/call`）、token 门控（`-32003`）、**总开关同时启停 HTTP 与管道**、**真实桥接进程 `whalepet-mcp.exe` 端到端**（stdio ↔ 管道 + `Content-Length` 分帧 + `--token` 注入） |
| DLL 插件装载（P7.3） | 真实 DLL 装载与能力注册（`origin = Dll`）、`apiVersion` 协商（不兼容被跳过且不影响其它插件）、失败降级、缺失目录 / 非插件文件 / IID 不匹配不报错 |
| `MiniGameService`（小游戏结算） | 档位奖励数值；每日 3 局上限；按「游戏 + 难度」分桶的个人最快与跨天清零；落库往返；旧版纪录键迁移 |
| ACP 显式信号（P7.5） | `AcpSignalSource` 增量读取（顺序 / 非法行忽略 / 未换行尾部 / 截断重置）；`AcpAgentBridge` 会话幂等与事件落盘；`AcpSignalRules` kind 映射与 `payload` 显式覆盖；`AcpSignalService` 轮询广播；显式信号覆盖推断且窗口过期回落 |
| 外部进程插件 / MCP Client（P7.4 / P9-B） | `ProcessServerSpec` 配置校验；`McpStdioClient` 分帧收发与请求应答配对；`McpPluginSession` 握手 / `tools/list` 发现 / `tools/call` 异步转发；调用超时与子进程崩溃隔离；**P9-B**：`ProcessPluginConfig` 纯逻辑解析（合法数组 / 非数组 / 非对象条目）与会话状态只读快照 `sessionStates()` |
| 宿主服务注册化（P9-A） | 5 个无 UI 服务（养成 / 胃袋 / 对话 / 彩蛋 / 回收站）以 builtin 插件注册进能力总线；各暴露 1 个只读状态能力（`service.*`，id 与 `builtinServiceCapabilityIds()` 一致）；服务未启动时返回 `-32002`（不伪造数据）；`startAll` 回填服务句柄；缺 `PetController` 时对话插件优雅降级 |
| ACP 事件映射与客户端（P7.6） | `AcpEventMapper` 以**真实 dsh 报文夹具**驱动（`session/update` → `CoreSignal`，工具按 `title` 细分，未知变体忽略）；`AcpClient` 端到端（握手 / 会话方法 / 权限自动应答 / 崩溃隔离） |

> **已落地的测试目标**（截至 2026-10-05，共 **36** 个，均在 CTest 注册、带 `TIMEOUT`；`test_win32_observer` 仅在 `WIN32` 下注册）：
>
> | 目标 | 文件 | 对应上面哪一行 |
> |---|---|---|
> | `test_smoke` | `tests/test_smoke.cpp` | 冒烟 + 表现层去重 + `PetWindow`/`PoseView` + 小游戏界面回归（扫雷棋盘尺寸 / 找小猫按键与场景 / 国际象棋棋盘点击与拖动交互） |
> | `test_state_machine` | `tests/test_state_machine.cpp` | `PetStateMachine`（含静息态节日换装：5 个节日日期 / 静息两档换装 / 工作·深夜·挂机·互动让位 / 非节日零回归） |
> | `test_line_table` | `tests/test_line_table.cpp` | `LineTable`（多文件加载 + 状态机场景覆盖） |
> | `test_database` | `tests/test_database.cpp` | `Database`（建表 / 迁移幂等 / 单例往返 / 事务回滚 / 目录三级降级） |
> | `test_growth` | `tests/test_growth.cpp` | `GrowthService` + `core/GrowthRules`（升级曲线 / 增量表 / 夹取 / 饱食衰减 / 跨天签到 / 升级信号 / 持久化） |
> | `test_content` | `tests/test_content.cpp` | `AchievementService`/`QuestService`/`SigninService` + `DiaryRepo`（39 项判定 / 3 槽抽签 / 周签到里程碑 / 日记上限） |
> | `test_chat` | `tests/test_chat.cpp` | `ChatService` + `core/ChatRules`（分时问候 / 深夜静默 / 心情分层 / 羁绊跨档 / 21 项关键词映射与开关 / 节流与序号 / 真实语料覆盖与立绘存在性） |
> | `test_hotword` | `tests/test_hotword.cpp` | 自定义热词优先匹配 / 归一化去重 / 热词表 CRUD / 显式录入不受开关门控 |
> | `test_settings` | `tests/test_settings.cpp` | 设置项「生效」：`night_quiet`（`PetStateMachine`）/ `pose_size` 与 `particles_enabled`、`drag_inertia`（`PoseView`）/ `bubble_enabled`（`SpeechBubble`） |
> | `test_minesweeper` | `tests/test_minesweeper.cpp` | 扫雷纯逻辑 `core::Minesweeper`（预设与自定义校验 / 首点安全布雷 / 连通区展开 / 翻格与插旗 / 胜负与全对插旗 / 峰值连翻 / 档位判定 / 随机源确定性） |
> | `test_minigame` | `tests/test_minigame.cpp` | 小游戏通用结算 `MiniGameService`（档位奖励数值 / 每日 3 局上限 / 按「游戏 + 难度」分桶的个人最快与跨天清零 / 落库往返 / 旧版纪录键迁移） |
> | `test_kitten` | `tests/test_kitten.cpp` | 找小猫纯逻辑 `core::RfkWorld`（物体表解析与非法行跳过 / 地图解析与错误 / 移动与撞墙 / 物体一次性消费 / 场景切换 / 通关与结算快照 / 主动结束 / 通用结算折算 / 难度表）+ 随包地图可达性与物件台词覆盖校验 |
> | `test_chess` | `tests/test_chess.cpp` | 国际象棋纯逻辑 `core::ChessGame`（FEN 往返 / 初始合法着法 / UCI 着法串 / 双步与吃过路兵 / 王车易位与路径被攻击的拒绝 / 兵升变四选一 / 将军·将死·逼和·和棋 / 非法着法拒绝 / 结算折算与难度表） |
> | `test_plugin_registry`（P7） | `tests/test_plugin_registry.cpp` | 插件注册与能力收集 / id 冲突与优先级仲裁 / 调用路由与错误码 / 异步能力取走回调的契约 / 装载器容错 / `minigame.*` 兼容适配 |
> | `test_platform_skeleton`（P7） | `tests/test_platform_skeleton.cpp` | 空实现恒「无数据」/ 组合观察者的类别·切换·停留·滚动窗口 / 失败不伪造数据 |
> | `test_work_state`（P7） | `tests/test_work_state.cpp` | 各工作状态判据 / Coding vs Vibe Coding / 置信度与滞回 / 状态机工作态通道（专注态静默与 `work.*` 豁免、不打断一次性表现、`Unknown` 零回归）/ 工作·未工作分类（`workStateIsBusy`）与 `Idle → idle-cute` 对齐 / P8：编程族常驻 `running`、其余 busy 态走 `work-*` 立绘池 |
> | `test_preset_dialogue`（P8） | `tests/test_preset_dialogue.cpp` | 时段划分与每段空闲立绘（07:00/17:59/18:00/22:59/23:00/06:59 与非法小时）/ 彩云中文天气 → 类型（雷·雹·雪·雨·雾霾尘·阴·云·晴 的优先级与「阴转多云」等边界）/ 天气立绘可达与 id 往返 / 工作立绘池（一轮 13 张不重复、`note()` 避重、编程族与池态互斥）/ 问答语料解析（同 slot 多条候选、脏行与孤儿回答丢弃、sceneKey 稳定）/ **五选一**（恒 5 项且槽位顺序固定、随机三题避开上一轮、语料不足时占位禁用）/ 槽位可用性（天气未配置 API → 禁用；敏感好感度 < 5000 → 禁用；当日三次用尽 → 禁用）/ 回答选取（普通题随机 slot、天气题按类型并逐级回落）/ 独立立绘池成员可达与避重 |
> | `test_context_dispatch`（P7） | `tests/test_context_dispatch.cpp` | JSON-RPC 2.0 校验与错误码 / 能力别名路由 / 门控（默认不监听、关闭后能力不可用）/ token 鉴权 / MCP `initialize`·`tools/list`·`tools/call` 映射 / 本地 HTTP 回环与 stdio 内存设备结果一致 |
> | `test_context_pipe`（P7.2） | `tests/test_context_pipe.cpp` | 命名管道承载完整 MCP 会话 / token 门控（`-32003`）/ **总开关同时启停 HTTP 与命名管道** / **真实桥接进程 `whalepet-mcp.exe` 端到端**（`QProcess` stdio ↔ 管道，含 `Content-Length` 分帧与 `--token` 注入）。依赖宏 `WHALEPET_MCP_EXE` 指向构建产物 |
> | `test_dll_plugin`（P7.3） | `tests/test_dll_plugin.cpp`（示例插件 `src/plugin/examples/hello`（`ext_hello`）与 `badabi`（`ext_badabi`）） | 真实 DLL 装载 / `apiVersion` 协商（不兼容被跳过且不影响其它插件）/ 失败降级 / 缺失目录、非插件文件、IID 不匹配均不报错 / 能力可见且可调用 |
> | `test_win32_observer`（P7.1） | `tests/test_win32_observer.cpp` | UTF-16→UTF-8 与路径取进程名 / 前台采样器填值且失败不伪造 / 输入采样器的空闲与差分降级（每次至多计 1，含时钟回绕保护）/ 注入替身不安装系统钩子且默认装配倾向钩子 / 系统状态 → `systemPaused` / 组合切换与停留 / 生命周期清空聚合记忆 / 锁屏无可读前台窗口 → `afk` |
> | `test_acp`（P7.5） | `tests/test_acp.cpp` | JSONL 信号源增量读取与顺序 / 非法行与缺 `kind` 忽略（不产假信号）/ 未换行尾部不消费 / 文件截断重置 / `setFilePath` / 会话生命周期幂等与事件落盘 / 信号→工作态映射（kind 表 + `payload` 显式覆盖）/ 轮询广播 / **显式信号覆盖推断且窗口过期回落** |
> | `test_process_plugin`（P7.4） | `tests/test_process_plugin.cpp`（子进程 `tests/mcp_test_server.cpp`） | 配置校验语义 / 拉起 + `initialize` 握手 + `tools/list` 发现（`ext.<pluginId>.<tool>`，`origin = Process`）/ `tools/call` 异步转发与结果回投 / 工具错误码透传 / 调用超时回投 / **子进程崩溃隔离**（pending 回投 `-32002` 且该来源能力标记不可用） |
> | `test_acp_event_mapper`（P7.6） | `tests/test_acp_event_mapper.cpp`（真实夹具 `tests/fixtures/acp-real-events.json`） | ACP `session/update` → `CoreSignal`（thought / message / tool_call / tool_call_update / plan / usage）/ 工具按 `title` 细分 / 未知变体与畸形输入忽略 / 事件 → 工作态映射。**夹具为真实 dsh（`dsh --profile acp`）原始报文**，故同时是「协议形状漂移」的回归守卫 |
> | `test_acp_client`（P7.6） | `tests/test_acp_client.cpp`（子进程 `tests/acp_test_agent.cpp`） | ACP 客户端端到端：启动 + `initialize` 握手 / `session/new` / `session/list` + `session/resume` / `session/prompt` 异步事件映射 / 权限自动应答 / **Agent 崩溃隔离**。含可选用例 `realDshSmokeOrSkip`——设置 `WHALEPET_ACP_REAL_DSH=<dsh>/lib/bin.js` 时用**真实 DeepSeek Harness** 跑一遍，否则跳过（CI 友好） |
> | `test_context_http_security`（安全加固） | `tests/test_context_http_security.cpp` | **SECURITY-REVIEW.md 极端边界 1/2**：跨站调用与认证（外部 `Origin` + `text/plain` / 外部 `Origin` + 合法 `Content-Type` / 缺失 / 错误 / 只差一字符的 token / 不可信 `Origin` 矩阵，逐例断言**有副作用假工具的调用计数保持 0**）/ `Content-Type` 允许与拒绝矩阵 / 响应从不带 CORS 头 / 缓冲上限（超长头 431、多连接并发、逐字节延迟）/ `Content-Length` 越界 413、非法与冲突 400、缺失 411、`chunked` 501 / 未收完正文仍等待且补齐后放行 / 慢速客户端超时被关闭且通道仍可用 / 反复启停回收套接字与缓冲 |
> | `test_gamestate_boundaries`（安全加固） | `tests/test_gamestate_boundaries.cpp` | **SECURITY-REVIEW.md 极端边界 3–6**：桥接文件输入（空 / 超限 / 仅空行 / 截断 / 被替换 / 超长 jsonl 末行）与 socket 输入（空响应 / 空白 / 畸形 JSON / 端点格式 / 无换行超大流按字节上限快速失败 / 每 100ms 1 字节的长期流被**总时长**上限约束并断开 / 合法首行+尾随垃圾）；CDP 发现白名单矩阵 + 假 `/json` 服务的不可信目标过滤 / 畸形 JSON / 非数组根 / 超大响应；WebSocket 生命周期（错误 id 后再发正确 id、永久超时、超大消息中止并可重连、引擎 error 与 exceptionDetails、握手后被断开 + 反复 3 轮无残留）；profile 数值与文件边界（`maxJumps` / 偏移 / `maxBytesPerRound` 越界与**上限边界正例**、失败不留部分生效 profile）；内存读取（字节预算恰好用满/超一字节、地址溢出**零次读取**、NaN/Inf/超范围浮点、跳数边界、空指针、部分读取、进程退出、Win32 读取器 8 轮 attach/detach 句柄不增长、目标进程被杀后读取失败） |
> | `test_pose_assets`（立绘资源门禁） | `tests/test_pose_assets.cpp` | 立绘「磁盘 / 索引 / `kPoses` / qrc」三方一致性、尺寸（256×256）与格式门禁、预载分档白名单（`core` / `warm` / `none`）、缓存与 LRU 行为、`PoseLibrary` 诊断埋点（`residentBytes` 等），共 18 例 |
> | `test_recyclebin`（2026-10-04） | `tests/test_recyclebin.cpp` | `RecycleBinService` 确定性行为：查询不崩且字段恒为非负、启停切换定时器、`start()` 立即检查一次（非 Windows / 不可用时 skip，不断言回收站必须非空） |
> | `test_code_easter_egg`（EX 彩蛋） | `tests/test_code_easter_egg.cpp` | `core::injectCodeEgg` 注释段识别（`//` 行注释段 / `/* */` 块注释 / Python `#`）与幂等注入；`viewmodel::EasterEggService` 5% 触发、工作区边界、幂等；关键不变量：**删掉注入行后文件逐字节等于原文** |
> | `test_game_memory`（EX1.1） | `tests/test_game_memory.cpp` | `gamestate` 只读读取底座（离线）：profile 解析 / 指针链各类型解引用 / `unsafe` 门控 / 魔数校验 / 连续失败后失效 |
> | `test_game_memory_e2e`（EX1.1） | `tests/test_game_memory_e2e.cpp`（合成靶进程 `tests/game_target_sim.cpp`） | 端到端：`attach` / 真实读取 / 目标进程退出处理 / 未启用时零开销 |
> | `test_unity_adapters`（EX1.2） | `tests/test_unity_adapters.cpp` | `dump.cs` 解析 / 字段名→指针链转换 / `UnityRuntime` 后端判定（Mono 名单 → `mono`，否则 `GameAssembly.dll` → `il2cpp`）/ 适配器端到端读取 / 模块缺失降级 / 连续失败失效 / 工厂路由 |
> | `test_rpgmaker_adapters`（EX1.3） | `tests/test_rpgmaker_adapters.cpp` | 特殊场景判据与滞回 / 桥接文件与 JSONL 快照 / CDP 对本地 `QWebSocketServer` 回放 / 连续失败失效与重连 / 工厂路由（MV·MZ 有 CDP 端点 → CDP，否则回退桥接；RGSS → 桥接） |
> | `test_game_companion`（EX1.4） | `tests/test_game_companion.cpp` | 判定规则（血量→持续态 / 置信度与滞回 / `Unknown` 立即降级 / 里程碑边沿 / 立绘与 `game.*` 场景映射）、状态机游戏态通道（最低让位 / 不打断一次性 / 里程碑播报 / 静默陪伴 / `Unknown` 零回归）、`GameCompanionService`（启停 / 上报 / 危险与里程碑透传 / 适配器失效自动停用） |
> | `test_service_plugins`（P9-A） | `tests/test_service_plugins.cpp` | 5 个宿主服务插件注册（参数非法拒绝）/ 能力 id 与 `builtinServiceCapabilityIds()` 一致且均为 Builtin + 只读 / 未启动时能力返回 `-32002` 且不伪造数据 / `startAll` 回填服务句柄且幂等 / 缺 `PetController` 时对话插件优雅降级 |
>
>
> `test_line_table` / `test_chat` 通过编译宏 `WHALEPET_LINES_DIR` 直读 `assets/lines/` 全部语料，
> 用于校验「代码引用的场景 key 在语料里真有候选」；`test_kitten` 同法并加读
> `WHALEPET_MAPS_DIR`（`assets/maps/`），用四方向 BFS 校验「每个难度下起点都能走到出口 / 小猫」，
> 同时确认物体表声明的每个台词场景 key 都在 `assets/lines/kitten.txt` 中有候选。
> `test_database` / `test_growth` 都用 `QTEST_GUILESS_MAIN`（只需 `QCoreApplication`），
> 不创建任何 Widget，故 offscreen 与无显示环境都能跑。
> P7 的目标里 `test_work_state` / `test_platform_skeleton` / `test_context_dispatch` /
> `test_plugin_registry` 只用 `QCoreApplication`（`test_plugin_registry` 虽链接 `whalepet_view`，
> 但只构造非 Widget 类型），因此无显示环境可跑；P9 的 `test_service_plugins` 同理
> （`QTEST_GUILESS_MAIN`，只构造 `QObject` 宿主与内存数据库）；
> 其余 P7 目标（`test_acp` / `test_acp_client` / `test_acp_event_mapper` / `test_process_plugin` /
> `test_win32_observer` / `test_context_pipe` / `test_dll_plugin`）在 `main()` 里把
> `QT_QPA_PLATFORM` 缺省设为 `offscreen`，同样无需真实桌面。
> 其中 `test_context_pipe` 会拉起真实子进程 `whalepet-mcp.exe` 并建立本机命名管道，
> `test_dll_plugin` 需从构建目录加载 `ext_hello.dll` / `ext_badabi.dll`，二者都**依赖构建产物存在**
> （CMake 已加 `add_dependencies` 与产物路径宏，见 `cmake/Tests.cmake`）。

### 2.1 编写约定（P7 起）

- **断言宏内不放复杂表达式**：`QVERIFY` / `QCOMPARE` 参数里不要写花括号初始化列表或多层模板
  （如 `std::vector<std::pair<QString, X>>{...}`）——moc 会报 `missing ')' in macro usage`
  （见 `docs/pitfalls/` TRAP-P7-004）。复杂表达式先落到局部变量再断言。
- **异步能力必须取走回调**：`ICapability::invoke` 返回 `false` 表示「异步已受理」，
  必须 `ctx.takeResponder()`；同步失败必须返回 `true` 并填 `error`
  （见 `docs/pitfalls/` TRAP-P7-005）。测试应显式覆盖这两条路径。
- **真实网络仅限回环**：HTTP 通道测试绑定 `127.0.0.1` + 端口 `0`（系统分配），
  不得依赖外部网络或固定端口。

## 3. 测试组织

- 每个模块一个测试可执行目标（`qt_add_executable`），注册到 CTest。
- 逻辑测试（`core/`）**不链接 Widgets**，可 headless 运行。
- 冒烟测试（可选）：`offscreen` 下创建 `PetWindow`、切一次 pose、退出。
- 表现层去重（`test_smoke`，需要 Qt）：同一 `PoseResult` 重复 present → 特效只迸发一次、台词只播一次；
  特效 500ms 强制间隔内丢弃且**不补播**；新台词**打断**上一条流式输出并从第一个字重来。

## 4. CMake 约定

```cmake
enable_testing()
qt_add_executable(test_statemachine tests/test_statemachine.cpp)
target_link_libraries(test_statemachine PRIVATE Qt6::Test <core_lib>)
add_test(NAME test_statemachine COMMAND test_statemachine)
set_tests_properties(test_statemachine PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
```

## 5. 运行

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir build -C Debug --output-on-failure --timeout 120
```

## 6. 覆盖目标（个人使用的务实标准）

- `core/` 逻辑：关键分支全覆盖（状态转移、边界夹取、幂等）。
- `model/`：读写与迁移路径可用。
- 不追求行覆盖数字，追求**关键行为**有断言。

## 7. 降级观测

- 任何跳过（如 offscreen 不可用、台词语料缺失）必须在 CTest/日志中写明原因，不得静默。
