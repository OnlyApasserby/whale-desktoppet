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
| `STATE-MACHINE.md` | 状态机设计（移植 whale `core.js`） | ✅ 已完成 |
| `PRESENTATION.md` | 立绘资产、静态立绘 + 程序化动效、窗口与交互表现 | ✅ 已完成 |
| `DATA-MODEL.md` | SQLite 表结构、存储路径、版本迁移与降级 | ✅ 已完成 |
| `GAMEPLAY.md` | 养成系统（心情/好感/饱食/等级/成就/任务/签到/羁绊/日记） | ✅ 已完成 |
| `CHAT.md` | 梗聊天、台词库组织、关键词表情感知 | ✅ 已完成 |
| `MINIGAME-INTERFACE.md` | 小游戏**插件化接入机制**与各插件规格（扫雷：接口 / 注册表 / 通用结算契约 / 难度预设 / 立绘台词 / 成就；鲸鱼娘找小猫：地图探索 / 物体交互 / 场景切换 / 外部可配置资源） | ✅ 已完成 |
| `PLUGIN-ARCHITECTURE.md` | **通用分层插件总线**：模块划分、依赖方向、三层插件（内置 / DLL / 外部进程）、统一 capability 协议、数据流与状态流转、小游戏兼容策略 | ✅ 已完成（P7.0 落地） |
| `CONTEXT-API.md` | **本地 Context API**：上下文数据模型、JSON-RPC 方法表与错误码、双通道（MCP stdio + 本地回环）、访问控制与隐私边界、ACP / IDE Agent 预留接口 | ✅ 已完成（P7.0 落地） |
| `mapinit.md` | 小游戏**地图 / 棋盘控件的初始化与尺寸强制规范**（尺寸必须由自身参数显式计算，禁止用布局返回值定尺寸；新增地图类插件必读） | ✅ 已完成 |
| `SETTINGS.md` | 设置项清单与设置面板设计 | ✅ 已完成 |
| `TESTING.md` | 自研测试策略（Qt6::Test） | ✅ 已完成 |
| `ROADMAP-P0.md` ~ `ROADMAP-P6.md` | 分阶段实施路线图（已验收阶段带 `-Fin` 后缀） | 见下 |
| `traps-Pn.md` | 各实施阶段的**真实踩坑记录**（`ROADMAP-Pn` ↔ `traps-Pn`，如 `ROADMAP-P1` ↔ `traps-P1.md`） | 随阶段进行 |
| `traps-extend0.md` | **扩展功能踩坑记录**：P0–P6 交付范围之外的真实问题（如打包分发后「安装版拖拽投喂不可用」的完整性级别/UIPI 问题） | 随问题追加 |

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
     - **P7（插件化智能桌宠 + 本地 Context API）**：设计文档与第一阶段（P7.0）重构骨架
       → `PLUGIN-ARCHITECTURE.md`、`CONTEXT-API.md`、`ROADMAP-P7.md`、`traps-P7.md`。
       结论：**不推倒重来**，采用「渐进式泛化 + 净增层」——保留既有分层，
       **`MiniGameRegistry` 一行未改**（兼容适配在外层完成），净增 `platform`（感知）/
       `plugin`（能力总线：内置 / DLL / 外部进程三层共存，统一 capability 协议）/
       `contextapi`（JSON-RPC 分发 + MCP stdio / 回环 HTTP 双通道 + ACP 预留接口）三块；
       `core::WorkState*` 提供 Coding / Vibe Coding 等状态判定，`PetStateMachine` 新增工作态通道
       （默认 `Unknown`，行为与 P6 一致）。**Debug / Release CTest 各 16/16**。
       分阶段优先级 P7.0 骨架（已完成）→ P7.1 真实感知 → P7.2 通道与 MCP 桥接 → P7.3 DLL 插件
       → P7.4 外部进程插件 → P7.5 ACP / IDE 集成。
       - **P7.1（真实桌面感知）已完成（2026-10-02）**：`platform` 层接入真实 Win32 采集
       （`Win32DesktopObserver`：前台窗口标题 + 进程名、`GetLastInputInfo` 空闲、
       **低层钩子**键鼠计数（安装失败降级为差分）、会话锁定 / 屏保 → `systemPaused`）；
       新增观察者生命周期 `setObserving()`，钩子**只在采样期间存在**（默认关闭 = 零系统资源）；
       `core::WorkStateRules` 按真实数据回归调参（会话暂停优先于「无数据」；
       新增「采样窗口内高强度单应用输入 → Coding」判据），并补 `test_win32_observer`
       （CTest 16 → 17，**Debug / Release 各 17/17**）。
       **MCP 与 ACP 仍是预留接口**（只定义不接入）。
       - **踩坑**：P7.0 5 条；P7.1 2 条（判定顺序缺陷 TRAP-P7-006、默认装配误关低层钩子
       TRAP-P7-007，均由新单测/真实接线暴露，见 `traps-P7.md`）。
       - **P6+ 追加（桌面四边框贴边）**：拖到桌面（屏幕可用区域）四条边框 **20px** 以内即判定贴合、
         吸附对齐，并**立即**切换为对应方向的探头立绘（上 `home-bottom` / 下 `home-peek` /
         左 `settings-peek` / 右 `workbench-peek`）；贴边期间不切拖动立绘；判定为**纯逻辑**
         `core/DesktopEdge.h`（`test_smoke` 覆盖），贴边不进状态机、离开边框即恢复
         → `PRESENTATION.md` §3.1、`STATE-MACHINE.md` §1。
         立绘清单随之由 92 张增至 **93 张**（`home-bottom` 入 `assets.qrc`）。
         Debug / Release CTest 各 **17/17**。
4. 除 ROADMAP 外的一般设计文档（如本页表格中的设计类文档）**不使用** `Fin` 后缀，其完成状态统一在本索引表「状态」列维护。
5. **踩坑记录命名 `traps-Pn.md`**：每个实施阶段对应一份踩坑记录（`ROADMAP-Pn` ↔ `traps-Pn`，如 `ROADMAP-P1.md` ↔ `traps-P1.md`）。
   - **触发时机**：该阶段实施过程中**真实遇到** Bug、构建/配置失败、环境异常、行为与验收标准不符等问题时，**逐条追加**记录；问题解决前不得美化、删除或提前标记完成。
   - **禁止编造**：仅记录已实际复现并排查过的问题，不得凭想象填写未发生条目。
   - **每条记录建议字段**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档。
   - 踩坑记录**不随阶段完成而改名**（不加 `Fin`），作为该阶段的历史留存长期保留。
   - **扩展功能**（不属于 P0–P6 任一阶段的后续问题，如打包分发后的可用性问题）另记入
     `traps-extend0.md`；记录格式、字段与 `traps-Pn.md` 完全一致。

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
| DesktopPet | `referances/DesktopPet/` | 透明置顶窗口 / 拖拽 / 右键菜单的实现思路；分层解耦思路 | 其构建系统（Qt 6.9.1 + MinGW）、GIF 播放路线、RPG 玩法 |
| dsh-whale-musume | `referances/dsh-whale-musume/` | 92 张立绘；状态机纯逻辑；养成/成就/任务/签到/日记规则；台词库与关键词感知 | DSH DOM 契约层、天气、余额、TTS、无障碍、主题适配、注入式设置 |

---

## 五、明确不做（Out of Scope）

- 任何宿主（DeepSeek Harness）相关的 DOM 契约、设置页 slot 注入、localStorage 数据。
- 天气（Open-Meteo）、余额代理、MiMo TTS 播报、无障碍模式、宿主主题跟随。
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
4. **修复顺序**：由用户调试确认根因后，AI 再实施修复，并把结论按 `traps-Pn.md` 规范记入对应阶段踩坑记录。
