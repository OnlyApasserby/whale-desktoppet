# WhalePet 桌宠 · 设计文档索引

> 工作名 **WhalePet**：基于 Qt 原生底座、复用「鲸鱼娘」立绘与养成/梗聊天逻辑的 **Windows 独立桌面宠物**。
> 本目录（`docs/`）存放全部设计文档与分阶段 ROADMAP。

---

## 一、文档清单

| 文档 | 内容 | 状态 |
|---|---|---|
| `README.md` | 索引、命名约定、已锁定决策 | ✅ 已完成 |
| `ARCHITECTURE.md` | 总体架构、分层、模块划分、目录结构、参考项目映射 | ✅ 已完成 |
| `BUILD.md` | 构建基线（Qt 6.8.4 + MSVC + CMake 4.4.2）与环境/命令模板 | ✅ 已完成 |
| `packages.md` | 打包与分发（免安装版 + NSIS 安装包）：安装/卸载「三处对应表」、运行期写权限（`stomach/`）、功能更新时的同步维护清单 | ✅ 已完成 |
| `release.md` | **发布记录**（唯一台账）：版本号、免安装版 / NSIS 安装版产物名与路径、与 `references/` 协议检测结论的关联、待确认项 | 随每次发布维护 |
| `STATE-MACHINE.md` | 状态机设计（移植 whale `core.js`） | ✅ 已完成 |
| `PRESENTATION.md` | 立绘资产、静态立绘 + 程序化动效、窗口与交互表现 | ✅ 已完成 |
| `POSE-ASSETS.md` | **poses 资源利用率提升方案**：资源定义/分类/现状（93 张，59.1% 有引用）、根因分析（R1–R6）、优化策略（唯一索引 `poses.json` + 生成器 + 门禁测试 + 预载分档 + 命名规范 + 审查机制）、量化指标（M1–M11）、四阶段实施（A–D）与责任分工、与上游 `dsh-whale-musume` 对标评估 | 🟡 **阶段 B（路径 A）已落地**：按需加载 + LRU 容量上限 + 负缓存 + **严格图片限制（尺寸必须 256×256、仅 Qt 原生支持格式）**；阶段 A 部分（门禁测试已落地，`poses.json` 索引未做）、C/D 未开始 |
| `DATA-MODEL.md` | SQLite 表结构、存储路径、版本迁移与降级 | ✅ 已完成 |
| `GAMEPLAY.md` | 养成系统（心情/好感/饱食/等级/成就/任务/签到/羁绊/日记） | ✅ 已完成 |
| `CHAT.md` | 梗聊天、台词库组织、关键词表情感知 | ✅ 已完成（P8 起与预设对话并列） |
| `DIALOGUE.md` | **预设问答（P8）**：主人提问 → 鲸鱼娘回答；五选一选项池（固定天气题 · 无 API 不可用；固定敏感题 · 好感度 5000 解锁且每日 3 次；其余 3 题每次随机刷新）、每题三回答随机取一、独立立绘池、触发门槛、彩云天气接入与降级 | ✅ 已完成 |
| `MINIGAME-INTERFACE.md` | 小游戏**插件化接入机制**与各插件规格（扫雷：接口 / 注册表 / 通用结算契约 / 难度预设 / 立绘台词 / 成就；鲸鱼娘找小猫：地图探索 / 物体交互 / 场景切换 / 外部可配置资源；国际象棋：外部 UCI 引擎（QProcess）/ 规则校验 / 引擎目录与打包） | ✅ 已完成 |
| `PLUGIN-ARCHITECTURE.md` | **通用分层插件总线**：模块划分、依赖方向、三层插件（内置 / DLL / 外部进程）、统一 capability 协议、数据流与状态流转、小游戏兼容策略 | ✅ 已完成（P7.0 落地；三层均已接入组合根） |
| `CONTEXT-API.md` | **本地 Context API**：上下文数据模型、JSON-RPC 方法表与错误码、**三通道**（MCP stdio + 本地回环 + 命名管道）、访问控制与隐私边界、MCP Client（外部进程插件）、ACP / IDE Agent 集成（含 P7.6 直连 DeepSeek Harness） | ✅ 已完成（P7.0 落地；P7.2 / P7.4 / P7.5 / P7.6 补实现） |
| `ACP-EVAL.md` | **ACP 实现评估**：Vibe Coding 实时状态接入方案对比（ACP Client / DOM·CDP / MCP）、ACP v1 协议要点与 `session/update` 事件映射、许可与验证边界（官方无 C++ SDK） | ✅ 已完成（映射已实现并用真实 dsh 报文验证） |
| `mapinit.md` | 小游戏**地图 / 棋盘控件的初始化与尺寸强制规范**（尺寸必须由自身参数显式计算，禁止用布局返回值定尺寸；新增地图类插件必读） | ✅ 已完成 |
| `SETTINGS.md` | 设置项清单与设置面板设计 | ✅ 已完成 |
| `TESTING.md` | 自研测试策略（Qt6::Test） | ✅ 已完成 |
| `P7-REMAINING-INTERFACES-AUDIT.md` | **P7.2 / P7.3 交付核查报告**（原「剩余接口」已清零）：两组接口的逐项实现 / 接线 / 打包 / 测试核查与改动清单（文件名保留以不破坏交叉引用） | ✅ 已完成（2026-10-02） |
| `ROADMAP-P0.md` ~ `ROADMAP-P8.md` | 分阶段实施路线图（已验收阶段带 `-Fin` 后缀；P7.0–P7.6 **全部完成**；P8（时段常驻立绘 / 工作立绘池 / 预设对话）**已完成**） | 见下 |
| ~~`NONACTION-COMPANION.md`~~（EX3 已归档） | **非动作类游戏陪玩 · 研究目标（待立项）**：EX1 的 `hp/gold` 中心模型在无 HP/金币游戏上的语义缺口、要回答的 Q1–Q6、候选指标族（进度/事件/计数/时间/会话/弱信号）与设计方向、验收设想、风险与明确不做 | 🗄 已归档（EX3 随外部游戏陪玩移入 `dump/docs/`，不入库） |
| `ROADMAP-P9-Fin.md` | **P9 渐进式插件化**：P9-A（宿主服务经 builtin 层注册化，剥离 5 个 `setup*`）+ P9-B（外部进程型深化）+ P9-C（`IPluginUiHost` UI 宿主契约与贡献点协议）；范围、交付物、A1~A6 与 C1~C6 验收、零回归约束；裁决见 `ARCHITECTURE.md` §A.6 / §A.7 | ✅ **已完成**（P9-A / P9-B / P9-C 全部验收通过，2026-10-06） |
| `pitfalls/` | **踩坑记录唯一存放位置**：按实施阶段分子文件夹 `p1/` … `p9/`、`ext0/`、`ex1/`、`ex2/`，每个真实问题一份文件 `P-<三位序号>-<短横线短语>.md`（共 91 条） | 随问题追加 |
| `pitfalls/index.md` | **踩坑记录唯一索引入口**（含非条目材料归档：新增条目模板、阶段实测结论、待人工验收项）；由本文件 §七 指向 | 随追加维护 |

---

## 二、命名与完成标记约定

1. **ROADMAP 分阶段命名**：`ROADMAP-Pn.md`（n = 0,1,2…），每个文件对应一个实施阶段。
2. **完成标记 `Fin`**：当某个阶段（或文档）**全部交付物完成并验收通过**后，在其文件名后追加 `Fin`：
   - `ROADMAP-P0.md` → 完成后重命名为 `ROADMAP-P0-Fin.md`
   - 以此类推：`ROADMAP-P1-Fin.md`、`ROADMAP-P2-Fin.md` …
3. **当前状态**：
   - **P0（规划与设计文档）已完成** → `ROADMAP-P0-Fin.md`。
   - **P1（外壳与立绘底座）**：实现与自动化验证完成，人工目视项待复验 → 暂不含 `Fin`。
   - **P2（状态机驱动表现）**：✅ 2026-09-30 人工复验通过（9 项中 8 项通过；
     `TRAP-P2-007` 为未复现的长期观察项，不阻塞）→ `ROADMAP-P2-Fin.md`。
   - **P3（养成与数据层）**：✅ 2026-09-30 人工复验通过（5/5），Debug / Release CTest 各 5/5
     → `ROADMAP-P3-Fin.md`。
   - **P4（内容层：日常 / 成就 / 日记）**：实现与自动化验证完成（`test_content`）；文件名为
     `ROADMAP-P4.md`（暂不含 `Fin`）。
   - **P5（梗聊天）**：✅ 自动化验证完成，Debug / Release CTest 各 7/7 → `ROADMAP-P5-Fin.md`
     （人工目视项见该文件「验收标准」）。
   - **P6（设置 / 打磨 / 测试 / 打包）**：✅ 2026-10-01 完成，Debug / Release CTest 各 9/9，
     部署干净 PATH 冒烟通过 → `ROADMAP-P6-Fin.md`（**项目全部阶段完成**）。
   - **P6+ 追加**：小游戏由「戳泡泡（预留）」**替换为扫雷**（3 档预设 + 自定义尺寸/雷数，
     保留进行游戏时鲸鱼娘的立绘变化与台词播报），小游戏类 7 项成就由预留改为可解锁；
     一局结算按「通关/及格/失败」发放养成奖励（每日 3 局上限，照搬参考项目）；
     应用图标改用 `assets/icon/whalepet.ico`（窗口图标 + exe 图标）；版本升至 **0.2.0**。
     Debug / Release CTest 各 **11/11**（新增 `test_minesweeper`、`test_minigame`）→ `MINIGAME-INTERFACE.md`。
   - **P6+ 追加（第二个小游戏）**：按插件规范接入**「鲸鱼娘找小猫」**（Robot Finds Kitten 风格的地图
     探索：方向键 / WASD / 点击相邻格移动，绕过礁石、捡起沿途物件、顺着海流切换场景，在最深处找到
     小猫；不同类别物体触发差异化立绘与专属台词）。**物体列表、地图与台词全部是外部资源**
     （`assets/maps/*.txt`、`assets/lines/kitten.txt`），可自行增删替换；三个难度分别需要穿越
     1 / 2 / 3 个场景。接入过程**未改动宿主（`PetWindow`）与结算服务的任何一行**，
     验证插件机制按设计生效；Debug / Release CTest 各 **12/12**（新增 `test_kitten`）
     → `MINIGAME-INTERFACE.md` §10。
     - **P6+ 追加（第三个小游戏：国际象棋）**：接入**国际象棋**（`chess`）——对手是**外部 UCI 引擎**
     （如 Stockfish，程序不自带棋力），经 `QProcess` 启动并按 **UCI 协议**通信；本程序侧实现并校验
     合法着法 / 王车易位 / 吃过路兵 / 兵升变 / 将军将死逼和，引擎着法同样复核后落盘；
     棋盘支持**点击与拖动**两种走子方式（点击棋子高亮全部合法落点，落在非法格不移动、棋子回到原格）；
     引擎路径可指定（默认回退安装目录 `engine/`）、三档棋力、执白 / 执黑，均落库；
     引擎缺失 / 启动失败 / 返回非法着法均**优雅降级并提示**。**用户需自行准备引擎**
     （见 `README.md`「国际象棋引擎」）。打包脚本新增 `engine/` 目录并纳入卸载删除逻辑
     （`docs/packages.md` §3.1）。再次**未改动宿主与结算服务**；Debug / Release CTest 各 **18/18**
     （新增 `test_chess`）→ `MINIGAME-INTERFACE.md` §11。
     - **P7（插件化智能桌宠 + 本地 Context API）**：设计文档与第一阶段（P7.0）重构骨架
       → `PLUGIN-ARCHITECTURE.md`、`CONTEXT-API.md`、`ROADMAP-P7-Fin.md`、`docs/pitfalls/`。
       结论：**不推倒重来**，采用「渐进式泛化 + 净增层」——保留既有分层，
       **`MiniGameRegistry` 一行未改**（兼容适配在外层完成），净增 `platform`（感知）/
       `plugin`（能力总线：内置 / DLL / 外部进程三层共存，统一 capability 协议）/
       `contextapi`（JSON-RPC 分发 + MCP stdio / 回环 HTTP 双通道）+ ACP 显式信号与 ACP 客户端
       （P7.5 / P7.6 落地）三块；
       `core::WorkState*` 提供 Coding / Vibe Coding 等状态判定，`PetStateMachine` 新增工作态通道
       （默认 `Unknown`，行为与 P6 一致）。**Debug / Release CTest 各 16/16**。
       分阶段优先级 P7.0 骨架（已完成）→ P7.1 真实感知（已完成）→ P7.2 通道与 MCP 桥接（**已完成**）
       → P7.3 DLL 插件（**已完成**）→ P7.4 外部进程插件（已完成）→ P7.5 ACP / IDE 集成（已完成）
       → P7.6 ACP 实时状态接入（已完成）。**P7.0–P7.6 全部交付**。
       - **P7.1（真实桌面感知）已完成（2026-10-02）**：`platform` 层接入真实 Win32 采集
       （`Win32DesktopObserver`：前台窗口标题 + 进程名、`GetLastInputInfo` 空闲、
       **低层钩子**键鼠计数（安装失败降级为差分）、会话锁定 / 屏保 → `systemPaused`）；
       新增观察者生命周期 `setObserving()`，钩子**只在采样期间存在**（默认关闭 = 零系统资源）；
       `core::WorkStateRules` 按真实数据回归调参（会话暂停优先于「无数据」；
       新增「采样窗口内高强度单应用输入 → Coding」判据），并补 `test_win32_observer`
       （CTest 16 → 17，**Debug / Release 各 17/17**）。
       **MCP / ACP 的预留接口已于 P7.4 / P7.5 补全为具体实现**（`src/plugin/process/**`、
       `src/contextapi/acp/**`）。
       - **P7.4（外部进程插件 / MCP Client）已完成（2026-10-02）**：`ProcessPluginLoader`
       由「配置与校验骨架」升级为完整实现（`McpStdioClient` 分帧 JSON-RPC 客户端 +
       `McpPluginSession` 握手 / `tools/list` 发现 / `tools/call` 异步转发 / 崩溃隔离），
       能力 id = `ext.<pluginId>.<tool>`；超时与崩溃隔离均有单测
       （`test_process_plugin` 以真实子进程 `mcp_test_server` 端到端验证，CTest 18 → 19）。
       - **P7.5（ACP / IDE Agent 集成）已完成（2026-10-02）**：`ISignalSource` / `IAgentBridge`
       接口不变、实现落地（`AcpSignalSource` / `AcpAgentBridge` / `AcpSignalRules`），显式信号作为
       **覆盖性输入**优先于推断（窗口过期回落到推断）；新增「ACP / IDE 信号」菜单项（默认关）
       与设置项 `acp_enabled` / `acp_signal_path`；新增 `test_acp`（CTest 19 → 20），
       **Debug / Release 各 20/20**。
       - **P7.6（ACP 实时状态接入）已完成（2026-10-02）**：新增 `AcpEventMapper`
       （ACP `session/update` → `CoreSignal`，工具细分依据 `title`）与 `AcpClient`
       （NDJSON over stdio + `QProcess` 子进程 + `initialize` / `session/*` + 权限自动应答 +
       崩溃隔离）；组合根新增 dsh 路径 / profile / workspace 设置项并串起与文件信源**共用**的下游
       覆盖链路；新增 `test_acp_event_mapper`（真实 dsh 报文夹具）与 `test_acp_client`
       （假 Agent 子进程端到端），**Debug / Release 各 22/22**。
      - **P7.2（通道启用与 MCP 桥接）已完成（2026-10-02）**：新增命名管道通道
      `LocalPipeTransport`（`QLocalServer`，**每条连接复用 `StdioTransport`**，故分帧 / MCP 方法映射 /
      token 门控与 stdio 通道同源；管道名唯一约定源 `kDefaultContextPipeName = "whalepet-context-v1"`）；
      `ContextApiService::start()` **一并启停 HTTP 与命名管道**且失败回滚；新增**控制台**桥接进程
      `whalepet-mcp.exe`（`src/app/mcp_bridge_main.cpp`，**故意不加 `WIN32`**：stdio ↔ 命名管道
      `Content-Length` 分帧字节转发，`--pipe` / `--token` / `--help`）；打包脚本与 NSIS 安装 / 卸载
      清单纳入该 exe；新增 `test_context_pipe`（8 用例，含**真实桥接进程端到端**），CTest **22 → 23**。
      - **P7.3（动态插件 DLL）已完成（2026-10-02）**：`DllPluginLoader` **接入组合根**
      （`PetWindow::setupDllPlugins()`，扫描 `<applicationDirPath>/plugins`，在构建菜单之前装载）；
      新增示例插件 `ext_hello`（合法）与 `ext_badabi`（`apiVersion=99` 负例）及 `test_dll_plugin`
      （装载 / ABI 协商 / 失败降级 / 缺失目录与非插件文件不报错），CTest **23 → 24**。
      `plugins/` **不随包分发**（`File /x` 排除 + 打包前清空 + 卸载非递归兜底，保护用户自装插件）。
      **Debug / Release 各 24/24**。
      - **踩坑**：P7.0 5 条；P7.1 2 条（TRAP-P7-006 / TRAP-P7-007）；P7.4 1 条
      （TRAP-P7-008：测试桩 server 的 stdio I/O）；P7.6 1 条（TRAP-P7-009：`signals` 是 Qt
      关键字宏）；P7.2 3 条（TRAP-P7-010：桥接两侧必须都分帧；TRAP-P7-011：`std::fread` 读管道会
      阻塞到读满 4096 字节而永久死锁，须用 `_read`；TRAP-P7-012：单测 `connectToServer()` 后同步等
      5s 致空等）；安全加固 8 条（TRAP-P7-013：HTTP 通道可被浏览器跨站调用；
      TRAP-P7-014：`windows.h` 的 `max` 宏；TRAP-P7-015：JSON 数字是 `double` 致小数静默截断 /
      越界 UB；TRAP-P7-016：桥接 socket 慢速流永久阻塞；TRAP-P7-017：`QWebSocket` 需真实握手；
      TRAP-P7-018：假服务与阻塞被测同线程互相饿死；TRAP-P7-019：指针链地址加法回绕；
      TRAP-P7-020：NaN/Inf 转整数是 UB），合计 **20 条**，见 `docs/pitfalls/`。
       - **P6+ 追加（桌面四边框贴边）**：拖到桌面（屏幕可用区域）四条边框 **20px** 以内即判定贴合、
         吸附对齐，并**立即**切换为对应方向的探头立绘（上 `home-bottom` / 下 `home-peek` /
         左 `settings-peek` / 右 `workbench-peek`）；贴边期间不切拖动立绘；判定为**纯逻辑**
         `core/DesktopEdge.h`（`test_smoke` 覆盖），贴边不进状态机、离开边框即恢复
         → `PRESENTATION.md` §3.1、`STATE-MACHINE.md` §1。
         立绘清单随之由 92 张增至 **93 张**（`home-bottom` 入 `assets.qrc`）。
         Debug / Release CTest 各 **17/17**。
      - **P7+ 追加（节日换装 + 工作 / 未工作立绘同步）**：新增纯逻辑 `core/FestivalRules.h`
        （`festivalPoseOf()`：公历固定日 10-31 / 12-25 / 02-14 + 农历小表 2026 / 2027 的春节·中秋，
        共 **5** 个节日，资源**全部复用既有 93 张立绘**、零新增美术）；
        `PetStateMachine::contextPose()` 在**静息态**按当日日期换装（跨零点自动换 / 脱），
        工作态（busy）与深夜 `sleep`、挂机 `afk` / `thinking`、浏览 / 游戏 / 离开一律让位
        —— 与参考项目「忙时情绪（含节日）让位」一致。
        同时把工作 / 未工作分类具名为 `core::workStateIsBusy()`（对齐参考 `BUSY_STATES`），
        并将 `WorkState::Idle` 的立绘由 `waiting` 对齐为 `idle-cute`（参考 `idle → idle-cute`）。
        详见 `STATE-MACHINE.md` §1.1 / §5.1、`PRESENTATION.md` §1.1。
   - **安全加固 + 极端边界测试（2026-10-03）**：按 `SECURITY-REVIEW.md` 修复本地
     Context API 的 HTTP 通道漏洞（「token 为空即不校验」+ 不校验 `Origin` ⇒ 浏览器可对
     本机端口发起跨站 JSON-RPC 调用）。改为**三层纵深防御**：`LocalHttpTransport::start()`
     在 token 为空时 **fail closed**（不监听）、`Content-Type` 必须 `application/json`
     （阻断 CORS 简单请求）、`Origin` 必须同源同端口（403）；令牌改定长比较；
     响应从不带 CORS 头。无令牌时只启用命名管道（浏览器不可达），组合根
     `PetWindow::setContextApiEnabled` 自动生成并落盘 256 bit 令牌，避免功能形同虚设。
     另修复 `SECURITY-REVIEW.md` §极端边界测试建议暴露的真实缺陷：HTTP 无缓冲/连接上限、
     桥接文件与 socket 读取无大小与**总时长**上限、CDP 发现不校验 `webSocketDebuggerUrl`
     主机且响应无大小上限、profile 数值越界（负数/小数/`1e30`）触发整数转换 UB、
     指针链与静态根**地址溢出**、float/double 的 NaN/Inf 转整数 UB。
     新增 `test_context_http_security`（11 例）与 `test_gamestate_boundaries`（23 例），
     并扩充 `test_game_companion`（+4 例启停边界）、`test_context_dispatch`（+1 例
     fail-closed 守卫）；`test_rpgmaker_adapters` 既有 8 例仍全绿。
   - **当前总量（EX3 后）**：`CMakeLists.txt` 现注册 **32 个测试目标**（Windows 下；
     `test_win32_observer` 为 `WIN32` 条件目标）。EX3 移除了 5 个外部游戏陪玩测试目标
     （`test_game_memory` / `test_game_memory_e2e` / `test_unity_adapters` /
     `test_rpgmaker_adapters` / `test_gamestate_boundaries`）与辅助进程 `game_target_sim`；
     保留 `test_game_companion`（判定 / 状态机通道 / 服务编排，改为假数据源驱动）。
     P9 新增 `test_service_plugins`（P9-A）、`test_ui_plugin_host`（P9-C）；自 P8 后陆续新增
     `test_pose_assets` / `test_recyclebin` / `test_code_easter_egg` / `test_game_companion`。
   - **立绘加载路径 A（2026-10-04，即 `POSE-ASSETS.md` 阶段 B）**：按需加载 + 容量受限 LRU 替代「启动全量预载 93 张」。
     新增 `view/PoseImageLoader`（**严格图片限制**：尺寸必须 256×256、仅 Qt 原生支持的格式、
     尺寸闸门前置到解码之前 → 同时是超大图的 OOM 闸门）、`view/AssetsResource`
     （`Q_INIT_RESOURCE` 收敛为单一定义，修正 `PoseLibrary` 依赖 `main()` 调用顺序的隐患）；
     `PoseLibrary` 改为 core(12) / warm(22) / 按需 三档 + LRU(36) + 负缓存；
     新增 `test_pose_assets`（18 例，CTest 31 → 32）。M3 常驻内存 23.3 → **9.0 MiB**（≤10 MiB 目标达成），
     预载冗余率 40.9% → **0%**。Debug / Release 各 **32/32**
     → `POSE-ASSETS.md` §实施状态、`docs/pitfalls/`（新增 TRAP-EXT0-005 / 006）。
   - **仍未打 `Fin` 的阶段**：`ROADMAP-P1.md`（人工目视项待复验）与 `ROADMAP-P4.md`
     （自动化验证完成、人工复验未登记）——两者均为历史遗留状态，不是新的待办；
     是否补做人工验收并由其改签为 `-Fin` 由项目 owner 决定。
   - **P9（渐进式插件化）**：✅ **2026-10-06 完成**（P9-A / P9-B 于 2026-10-05 验收通过；
     P9-C 于 2026-10-06 验收通过；Debug / Release 构建退出码 0，CTest **37 个目标**，
     `deploy-release/` offscreen 冒烟通过）→ `ROADMAP-P9-Fin.md`。
     依据 `ARCHITECTURE.md` 附录 A 的 4 缺口（G1/G2/G3/G4）与 ROI 排序，**§A.5「暂不推进」已由 §A.6 推翻**：
     - **P9-A**（宿主服务注册化）：5 个无 UI 依赖的服务经 builtin 层注册进能力总线，各暴露 1 个只读状态能力
       （`service.growth` / `service.stomach` / `service.dialogue` / `service.easterEgg` / `service.recycleBin`），
       宿主从 `setupGrowth` / `setupStomach` / `setupDialogue` / `setupEasterEgg` / `setupRecycleBin` 剥离装配；
       新增 `src/viewmodel/builtin/`（10 个文件）与 `test_service_plugins`。**A1~A6 逐条通过**（2026-10-05）。
     - **P9-B**（外部进程型深化）：`plugins.json` 解析下沉为 `plugin::ProcessPluginConfig`，新增
       `ProcessPluginLoader::sessionStates()` 与设置页「外部插件」只读列表；A1~A6 同批通过。
     - **P9-C**（UI 宿主契约与贡献点协议）：按既有调研定义引入 **`IPluginUiHost`（G1）与贡献点协议（G2）**，
       使 UI 面板型插件可在**不修改宿主**的前提下注册 UI；新增 `src/plugin/ui/`（契约，仅前向声明 `QWidget`）
       + `src/view/ui/`（宿主分发 `UiContributionHost` + 试点 `StatusPanelUiPlugin`）；宿主「状态」入口改为
       贡献点驱动；新增 `test_ui_plugin_host`（CTest **36 → 37**）。**C1~C6 逐条通过**（2026-10-06）。
     - **踩坑**：`P-081` / `P-082` / `P-083`（P9-A/B）；`P-084`（P9-C：测试替身固定字段与断言期望不一致）。
     - **裁决与证据**：`ARCHITECTURE.md` §A.6（启动 P9-A/B）/ §A.7（启动并完成 P9-C）；
       验证与达成证据见 `ROADMAP-P9-Fin.md` §6。
   - **P7 交付核查**：P7.2（命名管道 + `whalepet-mcp.exe` 桥接）与 P7.3（`plugins/` DLL 装载）
     均已交付，逐项实现 / 接线 / 打包 / 测试核查见 **`docs/P7-REMAINING-INTERFACES-AUDIT.md`**。
   - **P8（时段常驻立绘 / 工作立绘池 / 预设问答）**：✅ 2026-10-04 完成（**当时** Debug CTest **33/33**；
     当前全套 **37 个目标**）
     → `ROADMAP-P8.md` / `DIALOGUE.md` / `docs/pitfalls/`。要点：
     ① 时段常驻立绘（日间 `idle-cute` / 傍晚 `night` / 深夜 `daily-pajama`；**2026-10-04 二次修订**：深夜无唤醒态，点击不换立绘、满 10 次转虚弱，跨时段当帧立即刷新）；
     ② 编程态常驻 `running` + 13 张 `work-*` 立绘池（60s 轮转，联动热词命中与 ACP 工作态）；
     ③ 预设问答（**主人提问 → 鲸鱼娘回答**；面板标题「主人的问题」，**五选一**：
     固定 1 个天气问题（未配置彩云 key/城市时禁用）、固定 1 个敏感私密问题
     （好感度 ≥ 5000 解锁、每日 3 次），其余 3 题每次随机刷新；每题三个预设回答随机取一、
     以文字输出；敏感 / 选择 / 天气三类**独立立绘池**）。
     立绘档位随功能扩张调整为 core **14** + warm **25**（容量 36 → 40，M3 仍 ≤ 10 MiB，见 `POSE-ASSETS.md`）。
4. 除 ROADMAP 外的一般设计文档（如本页表格中的设计类文档）**不使用** `Fin` 后缀，其完成状态统一在本索引表「状态」列维护。
5. **踩坑记录命名 `docs/pitfalls/<阶段>/P-<三位序号>-<短横线短语>.md`**：按实施阶段分文件夹（`p1/` … `p8/`、`ext0/`、`ex1/`），每个**真实问题**独立成一份文件；序号**全局单调递增、不复用、不重排**。
   - **唯一入口**：`docs/pitfalls/index.md`（`docs/README.md` §七 指向它）。新增条目必须同时在 `index.md` 的「按阶段索引」补一行；**不得另建第二套踩坑目录或入口**。
   - **原始编号保留**：条目内保留历史编号 `TRAP-<阶段>-<序号>`（如 `TRAP-P7-011`），用于与既有文档、ROADMAP 与提交记录交叉引用。
   - **触发时机**：实施过程中**真实遇到** Bug、构建/配置失败、环境异常、行为与验收标准不符等问题时，**逐条新建文件**；问题解决前不得美化、删除或提前标记完成。
   - **禁止编造**：仅记录已实际复现并排查过的问题，不得凭想象填写未发生条目。
   - **每条记录字段**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档（以 `debug` 技能第 4 节为准）。
   - **非条目材料**（新增条目模板、阶段实测结论、待人工验收项）统一放 `docs/pitfalls/index.md` §二，**不得混入条目文件**。
   - 踩坑记录**不随阶段完成而改序号或删除**，作为历史留存长期保留。
   - **迁移说明**：原按阶段聚合的 `docs/traps-P1.md` … `docs/traps-P8.md`、`docs/traps-extend0.md`、`docs/traps-ex1.md`（共 10 份）已于本次目录改造中按规范拆分为 **80 份**独立条目文件（`P-001` … `P-080`），**正文未改动**，原文件已删除。

---

## 三、已锁定的产品决策

| # | 决策项 | 结论 |
|---|---|---|
| 1 | 产品形态 | **独立桌面应用**（双击即用，无宿主依赖） |
| 2 | 目标平台 | **仅 Windows** |
| 3 | 技术路线 | **原生 Qt 底座 + 移植 whale 的 JS 逻辑与资产** |
| 4 | 运行形态 | **原生 Qt Widgets**（非 WebEngine、非 Web 壳） |
| 5 | 桌宠形象 | **鲸鱼娘**，复用参考项目的 92 张 webp 立绘，并新增 1 张贴边立绘（合计 **93 张**） |
| 6 | 动画方案 | **不做逐帧动画**，采用「静态立绘 + 程序化动效」 |
| 7 | 玩法主线 | **以 whale 的养成系统为主** |
| 8 | 保留的延伸功能 | **仅保留「梗聊天」**；数据源全部落在安装目录 |
| 9 | 数据存储 | **SQLite**；**不迁移**参考项目历史数据 |
| 10 | 测试与受众 | 不沿用参考项目测试体系；**自研测试用例**；面向个人使用 |

---

## 四、参考项目与复用边界

| 参考项目 | 路径 | 复用什么 | 不复用什么 |
|---|---|---|---|
| DesktopPet | `references/DesktopPet/` | 透明置顶窗口 / 拖拽 / 右键菜单的实现思路；分层解耦思路 | 其构建系统（Qt 6.9.1 + MinGW）、GIF 播放路线、RPG 玩法 |
| dsh-whale-musume | `references/dsh-whale-musume/` | 92 张立绘；状态机纯逻辑；养成/成就/任务/签到/日记规则；台词库与关键词感知 | DSH DOM 契约层、天气、余额、TTS、无障碍、主题适配、注入式设置 |

---

## 五、明确不做（Out of Scope）

- 任何宿主（DeepSeek Harness）相关的 DOM 契约、设置页 slot 注入、localStorage 数据。
- 余额代理、MiMo TTS 播报、无障碍模式、宿主主题跟随。
- ~~天气（Open-Meteo）~~ → **P8 修订**：天气以**受限形态**回归 —— 仅作为预设对话的「天气题」，
  数据源为**彩云天气**（非 Open-Meteo），且 **key / 城市为空时完全不联网**；不做天气卡片、
  不做待机闲聊插天气、不做多城市（见 `DIALOGUE.md` §5、`SETTINGS.md` §8）。
- 逐帧动画素材制作。
- 跨平台（macOS / Linux）适配。

### 5.1 依赖口径（P7 修订）

- **原口径**（P0–P6）：`ARCHITECTURE.md` §2 曾写「**零新依赖**」。
- **现行口径**（P7 起）：**零第三方依赖，允许 Qt 官方模块**。
  - 新增 `Qt6::Network`（`QTcpServer` / `QLocalServer`）用于**本机回环**通道；
  - JSON 序列化用 `Qt6::Core` 的 `QJsonDocument` / `QJsonObject`，**不引**第三方 JSON 库；
  - SQLite 仍走 `Qt6::Sql` 的 QSQLITE 驱动；插件动态加载用 Qt 官方 `QPluginLoader`。
- 该修订已同步至 `ARCHITECTURE.md` §2/§3，并记录于 `PLUGIN-ARCHITECTURE.md` §3.2。

---

## 六、开发协作与调试约定

1. **崩溃一律交回用户调试，AI 不得自行排查。**
   一旦出现崩溃（进程异常退出、访问违例，如退出码 `0xC0000409` / `-1073740791`、`0xC0000005` 等），**立即停止**继续编译、构建、测试与复现尝试；**不得**自行加 ASan/调试器插桩、不得新建 sanitizer 构建目录、不得用「改代码试探」的方式定位。应改为**如实记录现象并交回用户**，由用户使用 **Qt Creator**（Debug 构建 + PDB）或 **WinDbg** 等调试器定位。
2. **交回用户时必须给出**：可复现步骤、退出码/报错原文、涉及的构建配置与二进制路径、已排除项，以及**明确标注**哪些是「已验证」、哪些只是「推测」。
3. **崩溃排查的临时产物**（sanitizer 构建目录、转储、日志）不进入仓库；如需新建独立构建目录，先与用户确认。
4. **修复顺序**：由用户调试确认根因后，AI 再实施修复，并把结论按 `docs/pitfalls/` 规范记入对应阶段踩坑记录。

---

## 七、踩坑记录索引

> **唯一入口**：本区只指向 **[`pitfalls/index.md`](pitfalls/index.md)**（索引表在该文件内维护），不在此重复列表。
> **存放规则**：按实施阶段分文件夹 —— `docs/pitfalls/p1/` … `p8/`、`p9/`、`ext0/`、`ex1/`、`ex2/`；每个真实问题一份文件，命名 `P-<三位序号>-<短横线短语>.md`，序号**全局单调递增、不复用、不重排**。
> **原始编号**：条目内保留历史编号 `TRAP-<阶段>-<序号>`（如 `TRAP-P7-011`），可与既有文档 / ROADMAP / 提交记录交叉引用。
> **非条目材料**（新增条目模板、阶段实测结论、待人工验收项）：见 `pitfalls/index.md` §二。
> **迁移说明**：原按阶段聚合的 `docs/traps-P1.md` … `traps-P8.md`、`traps-extend0.md`、`traps-ex1.md` 已拆分为 **80 份**独立条目文件，正文未改动；源码与文档注释中凡引用旧文件名者，一律指向 `docs/pitfalls/`。

阶段与文件夹对照：

| 阶段 | 文件夹 | 条目数 | 序号区间 |
|---|---|---|---|
| P1 | [`p1/`](pitfalls/p1/) | 5 | `P-001` … `P-005` |
| P2 | [`p2/`](pitfalls/p2/) | 12 | `P-006` … `P-017` |
| P3 | [`p3/`](pitfalls/p3/) | 5 | `P-018` … `P-022` |
| P4 | [`p4/`](pitfalls/p4/) | 6 | `P-023` … `P-028` |
| P5 | [`p5/`](pitfalls/p5/) | 3 | `P-029` … `P-031` |
| P6 | [`p6/`](pitfalls/p6/) | 7 | `P-032` … `P-038` |
| P7 | [`p7/`](pitfalls/p7/) | 20 | `P-039` … `P-058` |
| P8 | [`p8/`](pitfalls/p8/) | 10 | `P-059` … `P-068` |
| EXT0 | [`ext0/`](pitfalls/ext0/) | 5 | `P-069` … `P-073` |
| EX1 | [`ex1/`](pitfalls/ex1/) | 12 | `P-074` … `P-080`、`P-087` … `P-091` |
| P9 | [`p9/`](pitfalls/p9/) | 4 | `P-081` … `P-084` |
| EX2 | [`ex2/`](pitfalls/ex2/) | 2 | `P-085` … `P-086` |
| EX3 | [`ex3/`](pitfalls/ex3/) | 2 | `P-092` … `P-093` |
| EX4 | [`ex4/`](pitfalls/ex4/) | 0 | 待新增（小游戏陪玩，见 `ARCHITECTURE.md` 附录 B） |
