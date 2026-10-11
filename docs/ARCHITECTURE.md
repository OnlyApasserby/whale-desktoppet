# 总体架构设计（ARCHITECTURE）

## 1. 目标与范围

构建一个 **Windows 独立桌宠应用**：单进程、原生 Qt Widgets、无宿主依赖。核心能力 = **桌宠外壳**（透明置顶窗口 / 拖拽 / 菜单 / 立绘显示）+ **养成系统** + **梗聊天**。

## 2. 技术选型

| 项 | 选择 | 说明 |
|---|---|---|
| 语言 | C++17 | 固定标准 |
| UI | Qt 6.8.4 **Widgets** | 原生窗口，非 QML、非 WebEngine |
| 图像 | Qt6::Gui（含 webp 图像插件） | 立绘为 webp，构建期确认 `imageformats/qwebp` |
| 数据 | **Qt6::Sql（QSQLITE）** | SQLite 为 Qt 内建模块，**不引入第三方库** |
| 本机通道（P7） | **Qt6::Network**（`QTcpServer` + `QLocalServer`） | Context API 的**本地**传输；Qt 官方模块。`QTcpServer`（HTTP 回环，P7.0）与 `QLocalServer`（命名管道，P7.2）均已落地 |
| 插件加载（P7） | `QPluginLoader` + 外部进程 stdio | 三层插件中的动态层与进程层（见 `PLUGIN-ARCHITECTURE.md`） |
| 桌面采集（P7.1） | `user32`（Win32 API） | 前台窗口 / 进程名 / 空闲时长 / 低层输入钩子 / 会话状态；**Windows 系统库，非第三方依赖** |
| 测试 | Qt6::Test | 自研用例 |
| 构建 | CMake 4.4.2 + VS 18 2026 | 详见 `BUILD.md` |

> 约束（P7 修订）：**零第三方依赖，允许 Qt 官方模块**。
> SQLite 走 `Qt6::Sql` 的 QSQLITE 驱动；JSON 用 `Qt6::Core` 的 `QJsonDocument`；
> 本机通道用 `Qt6::Network`。原「零新依赖」口径的修订记录见 `README.md` §5.1。

## 3. 分层结构

```
┌────────────────────────── View（Qt Widgets） ──────────────────────────┐
│ PetWindow      透明/无边框/置顶主窗口，承载立绘；桌面四边框贴边判定       │
│ PoseView       立绘渲染 + 程序化动效（缩放/位移/旋转/透明度）+ 贴边探头   │
│ SpeechBubble   台词气泡                                                  │
│ TrayMenu       右键菜单 / 系统托盘                                       │
│ SettingsDialog 设置面板                                                  │
│ Panels         状态/成长/成就/任务/日记等展示面板                        │
└─────────────────────────────────────────────────────────────────────────┘
                 ▲ 数据绑定(只读属性) / 命令          │ 用户事件
                 │                                    ▼
┌──────────────────── ViewModel / Controller ─────────────────────────────┐
│ PetController     总调度：连接事件 → 状态机 → 表现/数据                  │
│ PosePresenter     pose/动效/气泡指令的发布者                             │
│ GrowthService     养成数值计算（心情/好感/饱食/等级/签到/陪伴时长）       │
│ AchievementService 成就判定                                             │
│ QuestService      每日任务 / 周签到                                      │
│ ChatService       台词选择 + 关键词表情感知                              │
│ DialogueService   问答编排（五选一选项池 / 回答随机 / 敏感题每日配额）    │
│ WeatherService    彩云天气类型判定（空配置 = 完全不联网）                 │
└─────────────────────────────────────────────────────────────────────────┘
                 ▲ 纯逻辑调用                        │
                 ▼                                    ▼
┌──────────────── Core（无 UI 依赖，可单测） ────────┬──── Model（数据） ───┐
│ PetStateMachine  状态机（移植 whale core.js）      │ Database   SQLite 封装│
│ Rules            数值/成就/任务纯规则函数          │ Repositories 各表访问 │
│ LineTable        台词库解析/匹配                   │ Schema    建表/迁移   │
└───────────────────────────────────────────────────┴───────────────────────┘
```

**设计原则**
- **Core 层零 Qt UI 依赖**：状态机与规则为纯 C++，可脱离界面单测（延续 whale `core.js` 的优点）。
- **单向数据流**：View 只发事件、只读属性；所有写入经 Controller → Service → Model。
- **表现与逻辑分离**：状态机输出「语义结果」（pose 名 / 台词 key / 特效类型），由 Presenter 翻译成 Qt 动画。

**P7 净增的三层**（详见 `PLUGIN-ARCHITECTURE.md`）：

| 层 | 目标 | 职责 |
|---|---|---|
| `platform` | `whalepet_platform` | 桌面环境感知的**接口**（前台窗口 / 输入活跃度 / 系统状态）；P7.0 只提供空实现，**P7.1 起为真实 Win32 采集**（默认关闭，开启后才安装低层输入钩子并采样） |
| `plugin` | `whalepet_plugin` | **通用能力总线**：`IPlugin` / `CapabilityDescriptor` / `CapabilityRegistry` + 三层装载器（内置 / DLL / 外部进程）。**三层均已接入组合根**（内置 / DLL（P7.3）/ 外部进程（P7.4）） |
| `contextapi` | `whalepet_contextapi` | **本地 Context API**：`JsonRpcDispatcher` + 三通道（MCP stdio / 回环 HTTP / 命名管道（P7.2））+ ACP 集成（P7.5 显式信号、P7.6 ACP 客户端） |

依赖方向：`view → {model, platform, plugin, contextapi} → core`，**禁止反向**。
`contextapi` 通过 `IContextProvider` 取数据（实现落在 view 侧），故不反向依赖 view。

> **P7 交付（2026-10-02）**：P7.2 的命名管道通道（`LocalPipeTransport`）与 `whalepet-mcp.exe`
> 桥接 exe、P7.3 的 `plugins/` 目录扫描接线与 DLL 插件产物**均已交付**，P7.0–P7.6 全部完成。
> 二者**不影响默认运行行为**（默认不监听任何端口 / 管道、不扫描 `plugins/`）。
> 逐项交付核查见 `docs/P7-REMAINING-INTERFACES-AUDIT.md`。

## 4. 模块清单

| 模块 | 职责 | 关键来源 |
|---|---|---|
| `PetWindow` | 透明无边框置顶窗口、拖拽、多显示器、位置持久化、**桌面四边框贴边判定与吸附** | DesktopPet 思路 |
| `PoseView` | 加载 webp、按 pose 切换、程序化动效、**贴边探头立绘贴齐边框** | 新增（替代 QMovie/GIF） |
| `PetStateMachine` | 状态集合与转移 | whale `core.js` |
| `GrowthService` | 心情/好感/饱食/等级/连续签到/陪伴时长 | whale 养成 |
| `AchievementService` | 成就判定与解锁 | whale 成就（39） |
| `QuestService` | 每日任务、周签到 | whale |
| `ChatService` | 台词选取、关键词表情 | whale 台词库（530+） |
| `DialogueService` | 问答：五选一（天气 / 敏感固定槽 + 随机三题）+ 每题三回答随机取一 | 【P8 新增】主人提问 → 鲸鱼娘回答 |
| `WeatherService` | 彩云天气类型判定（供对话天气题） | 【P8 新增】参考项目天气的**受限重做** |
| `Database` | SQLite 连接、建表、迁移、降级 | 新增 |
| `SettingsDialog` | 设置项读写 | whale（去宿主化重做） |

> **P3 落地情况**：`model/` 已按上表落地为静态库 `whalepet_model`
> （`Qt6::Core` + `Qt6::Sql`，**不链接 Widgets**，依赖 `whalepet_core`）；
> `GrowthService` 落在 `src/viewmodel/`，其数值规则拆到零 Qt 的 `src/core/GrowthRules.h`
> 以便脱 UI 单测。类清单见 `DATA-MODEL.md` §4 的实施落位表。

## 5. 建议目录结构

```
desktoppet/
├─ src/
│  ├─ app/          # main.cpp、应用装配、生命周期
│  ├─ view/         # PetWindow / PoseView / SpeechBubble / TrayMenu / SettingsDialog / Panels
│  ├─ viewmodel/    # PetController / *Service / Presenter / EnvironmentService / WorkStateService
│  ├─ core/         # PetStateMachine / Rules / LineTable / WorkState / WorkStateRules（无 UI 依赖）
│  ├─ model/        # Database / Repositories / Schema
│  ├─ platform/     # 【P7 净增】桌面感知接口 / 空实现 / Win32 真实采集（P7.1）
│  ├─ plugin/       # 【P7 净增】能力总线：IPlugin / Capability / Registry / 三层装载器
│  ├─ contextapi/   # 【P7 净增】JsonRpc 分发 / 三通道（HTTP + stdio + 命名管道）/ ACP 信号与客户端
│  ├─ minigame/     # 小游戏插件接口、注册表与兼容适配器
│  └─ common/       # 常量、工具、事件定义、类型
├─ assets/
│  ├─ poses/        # 93 张 webp 立绘（含 4 张桌面贴边探头立绘）
│  ├─ lines/        # 台词库、关键词表
│  └─ *.qrc
├─ tests/           # 自研单测
├─ docs/            # 本设计文档目录
├─ cmake/           # 【构建模块化】CMake 子模块（CompileOptions / QtDependencies / Libraries /
│                  #   OutputLayout / Executables / Tests / PluginExamples），由顶层 include 引入
├─ CMakeLists.txt   # 构建编排入口（project / 版本 / 语言），具体配置见 cmake/
└─ CMakePresets.json
```

## 6. 事件与线程模型

- 主线程（Qt GUI 线程）负责全部 UI 与状态机 tick；SQLite 读写默认同步（个人使用量级足够）。
- 定时驱动：**待机 tick**（状态机概率事件）、**AFK 计时**（180s）、**气泡间隔**（≥6s）、**养成结算 tick**（如饱食随时间下降）。
- 若后期 DB 写放大，再引入 `QThread`/`QtConcurrent` 异步落盘（本期不做）。

## 7. 参考项目模块映射

| 新模块 | 移植来源 | 方式 |
|---|---|---|
| `PetWindow` | `DesktopPet/src/view/PetMainWindow.*` | 参考实现，在 MSVC 基线重写（去除 MinGW/QMovie 依赖） |
| `PetStateMachine` | `whale/assets/whale-moe-core.js` | **逻辑移植**为纯 C++ |
| `PoseView` / 动效 | `whale/assets/dsh-whale-moe.js`（presenter） | **重写**为 Qt 动画 |
| 养成/成就/任务 | whale 对应逻辑 | 逻辑移植 + 落 SQLite |
| `ChatService` | whale 台词库 + 关键词表 | 迁移内容 + 移植匹配逻辑 |
| `Database` | `DesktopPet` 的 JSON 持久化 | **替换**为 SQLite |
| 设置面板 | whale 注入式设置 | **重做**为 Qt 对话框 |

## 8. 关键风险与对策

| 风险 | 对策 |
|---|---|
| webp 图像插件缺失 | 构建期探测 `qwebp`，缺失则报错并给出 windeployqt 说明（见 `BUILD.md`） |
| 安装目录写权限（Program Files / UAC） | 默认写「安装目录同级 `data/`」；写失败降级到用户目录并日志告警（见 `DATA-MODEL.md`） |
| 台词语料量大（530+） | 外部资源文件承载，不硬编码进 C++（见 `CHAT.md`） |
| 静态立绘缺动效导致「呆板」 | 用程序化动效补偿（呼吸缩放、惯性位移、过渡遮断，见 `PRESENTATION.md`） |

---

## 附录 A · 「完全插件化重构」可行性调研（2026-10-05 调研；✅ P9-A / P9-B / P9-C 全部验收通过）

> 调研方式：只读扫描 + 并发子代理核查（组合根/UI 耦合、viewmodel·core·model 可拆性、插件总线与构建目标承载力、参考项目做法）。
> 结论状态：**P9-A / P9-B / P9-C 全部验收通过（2026-10-06 完成）**——§A.5 曾裁决「暂不推进」，2026-10-05 经用户**重新裁决推翻**，先启动 **P9-A + P9-B**（见 §A.6），二者逐条通过 A1~A6 验收后经用户指示启动 **P9-C**（引入 G1 `IPluginUiHost` 与 G2 贡献点协议，见 §A.7），C1~C6 亦逐条通过。
> 「未确认前不修改任何模块边界」原则已在 §A.7 由用户解除：**P9-C 经确认后正式引入 G1（UI 宿主上下文）与 G2（贡献点协议）**。

### A.1 现状事实（已验证，均带源码/CMake 证据）

| # | 事实 | 证据 |
|---|---|---|
| 1 | 全部业务模块是**编译期静态库**，没有任何动态边界 | `cmake/Libraries.cmake:19/78/113/141/186/214/255`（7 个 `qt_add_library(... STATIC)`） |
| 2 | **`view` + `viewmodel` + `minigame` 合编为一个静态库** `whalepet_view`，并以 `PUBLIC` 链接 core/model/platform/plugin/contextapi（**EX3 起 `gamestate` 已移除**） | `cmake/Libraries.cmake:255-361` |
| 3 | 工程内**无 DLL 导出层**：无 `generate_export_header`、无 `WHALEPET_EXPORT`、无 `__declspec(dllexport)` | 全 `src/` 检索仅命中 `MiniGamePlugin.h:94`、`plugin/dll/IPluginFactory.h:6/9/38` 及示例插件 `examples/*/HelloPlugin.h`、`BadAbiPlugin.h` |
| 4 | 能力总线在设计上**主动不依赖 UI**：`whalepet_plugin` 只依赖 Qt6::Core + whalepet_core，`PluginContext` 仅持前向声明指针 | `cmake/Libraries.cmake:181-207` |
| 5 | 宿主 `PetWindow` 是**全部功能的组合根**：22 个硬编码装配函数覆盖 15 个功能域 | `src/view/PetWindow.cpp:313/333/338/366/465/482/539/559/572/624/734/779/857/886/922/982/1037/1234/1271/1382/1875/1981` |
| 6 | 唯一实现「宿主不认识具体实现」的是**小游戏**，但它是 `whalepet_view` 内部的 `MiniGameRegistry` 注册表，不是跨 DLL 契约 | `cmake/Libraries.cmake:276-281`、`src/minigame/MiniGamePlugin.h` |
| 7 | 三层装载器（builtin / DLL / 外部进程）已存在，外部进程层（MCP stdio）为净增能力 | `src/plugin/builtin/`、`src/plugin/dll/`、`src/plugin/process/` |
| 8 | `references/` 两参考项目均无 Copyleft 信号；本项目维持 MIT | `references/README.md` §三 |

### A.2 判定：**「完全插件化」（宿主零业务分支、全部功能可独立编译与替换）当前不成立，不建议推倒重来**

要达成「完全插件化」，必须先**净增 4 类当前完全不存在的契约**：

| 缺口 | 缺失内容 | 受影响的插件形态 | 证据 |
|---|---|---|---|
| G1 UI 宿主上下文 | 宿主窗口句柄 / 父 `QWidget` / 生命周期回调 | 小游戏窗口、设置页、状态面板、内容面板、对话面板（5 类） | `Libraries.cmake:181-207`（总线不依赖 Widgets） |
| G2 贡献点协议 | 右键菜单项、托盘项、设置页注册 | 一切需要"出现在界面上"的功能 | `PetWindow.cpp:1875`（`setupContextMenu`）、`:1981`（`setupTray`）、`:734`（`setupSettings`）写死 |
| G3 生命周期与后台能力 | 独立构造/析构、定时器、失败重启、事件循环 | 服务型（感知采样、天气轮询、回收站轮询…） | `PetWindow.cpp` 22 处装配全由宿主 new/connect/析构 |
| G4 ABI 与符号导出层 | `generate_export_header` + 稳定 ABI 子集（或全改为进程外插件） | 一切 DLL 形态插件 | `Libraries.cmake:255` 为 STATIC；`src/plugin/dll/` 仅自包含工厂 |

**收益面评估**：

- **高收益 / 低成本**：`whalepet_core` 中已有的**零 Qt 纯逻辑**（`GrowthRules` / `Achievements` / `Quests` / `SigninRules` / `WorkStateRules` / `WeatherRules` / `DaySlotRules` / `WorkPosePool` / `PresetDialogue` / `DialogueOptions` / `FestivalRules` / `Minesweeper` / `RobotKitten` / `Chess` / `CodeEasterEgg`）与**外部进程型**能力（Context API / ACP / MCP / 游戏陪玩）——前者无 UI 依赖、后者天然跨进程，这两类**可以低成本插件化**。
- **低收益 / 高成本**：UI 面板型（设置页、状态面板、内容面板、小游戏窗口、对话面板）。其插件化成本 90% 花在 G1/G2，收益仅"UI 可替换"，对单进程个人桌宠几乎无价值；且回归面巨大（宿主 22 个装配点 + 全量测试）。

### A.3 建议路线（按 ROI 排序，不推倒重来）

保持 `docs/PLUGIN-ARCHITECTURE.md` 已定的「通用能力总线 + 三层装载器 + 渐进式泛化」，把"完全插件化"拆为三步试水：

1. **P9-A（低风险、高收益）**：把 `whalepet_core` 纯逻辑与无 UI 的 Service 经 **builtin 层**彻底注册化，宿主从 22 个 `setup*` 中剥离对应装配（先动 `setupGrowth` / `setupStomach` / `setupDialogue` / `setupEasterEgg` / `setupRecycleBin`）。
2. **P9-B**：外部进程型（`plugin/process/` 已具备 MCP stdio 装载器）继续走深，宿主只保留注册与状态展示。
3. **P9-C（仅在确有需求时）**：再引入 `IPluginUiHost`（G1）+ 贡献点协议（G2），才具备讨论"UI 也插件化"的前提。

**判定 G4 的取舍**：若坚持 DLL 形态，必须先引入导出宏与稳定 ABI 子集（成本高、约束强）；若接受**进程外插件**，可完全绕过 G4，与既有 `process` 装载器同构——**推荐后者**。

### A.4 待用户确认

- [x] 是否采纳 A.3 的推荐路线（保留渐进式泛化，不做完全插件化）？
- [x] 若决定推进，先从 P9-A 还是 P9-B 起步？
- [x] 是否引入 P9-C 的 UI 宿主上下文契约（决定 UI 型插件是否在范围内）？

### A.5 用户裁决（2026-10-05）

> **结论：暂不推进，仅留存调研结论。**（附录 A 保持 ⏳ 待确认状态，入口见 §一「技术文档」与本附录）

- 不启动 P9-A / P9-B / P9-C 中任何一步；**不改动任何模块边界**，`whalepet_view` 等静态库分层与
  现有「通用能力总线 + 三层装载器」形态保持不变。
- 本附录作为**决策留痕与后续复用依据**：若将来重启该议题，直接从 A.2 的 4 个缺口（G1 UI 宿主上下文 /
  G2 贡献点 / G3 生命周期 / G4 ABI 导出层）与 A.3 的 ROI 排序开始，无需重新调研。
- A.2 已登记的事实（7 个 STATIC 库、22 个 `setup*` 装配点、无导出层）在架构发生实质变化时**需要重新核对**。

### A.6 用户裁决（2026-10-05，修订 · 推翻 A.5）

> **结论：正式启动 P9-A + P9-B；P9-C 暂缓。**（本节修订 A.5；A.5 保留为决策留痕，不再作为现行结论。）
> **⚠️ 部分已修订（2026-10-05）**：P9-A / P9-B 验收通过后 **P9-C 已启动**，本节「P9-C 暂缓」不再生效，见 **§A.7**。

- **推翻说明**：用户在 2026-10-05 的会话中明确要求「启动 P9 阶段的项目插件化重构，完整实现 P9-A 与 P9-B」，
  该指示与 A.5「不启动 P9-A / P9-B / P9-C 中任何一步」直接冲突，经用户显式确认后以本节为准。
- **范围**：
  - **P9-A**：把 `whalepet_core` 纯逻辑与**无 UI 依赖**的 Service 经 **builtin 层**注册化，
    宿主从 `setupGrowth` / `setupStomach` / `setupDialogue` / `setupEasterEgg` / `setupRecycleBin` 中剥离对应装配；
  - **P9-B**：`plugin/process/` 外部进程型继续深化，宿主只保留注册与状态展示。
- **不做（P9-C 暂缓）**：不引入 `IPluginUiHost`（G1）与贡献点协议（G2）；
  **UI 面板型插件仍不在范围内**，A.2 的 G1 / G2 缺口保持未填补。
- **仍然保持**：A.2「不推倒重来、保留 `whalepet_view` 等静态库分层」的口径不变；
  `PLUGIN-ARCHITECTURE.md` §7 的零回归红线（`MiniGameRegistry` 一族一行不改）继续有效。
- **阶段登记**：P9 作为**主阶段**（可交付里程碑）登记入 `docs/pitfalls/index.md` 阶段表，
  踩坑条目落 `docs/pitfalls/p9/`（序号接续 `P-081` 起）。
- **路线图与验收标准**：见 **`docs/ROADMAP-P9-Fin.md`**（P9-A / P9-B 交付物、A1~A6 验收与零回归约束）。

### A.7 用户裁决（2026-10-05，P9-A / P9-B 验收通过 → 启动 P9-C）

> **结论：P9-A / P9-B 逐条通过 A1~A6 验收；据此启动 P9-C，正式引入 G1（`IPluginUiHost` UI 宿主上下文）与 G2（贡献点协议）。**（本节修订 §A.6 中「P9-C 暂缓」的部分；A.5 / A.6 保留为决策留痕。）

- **验收确认**：P9-A（宿主服务经 builtin 层注册化）与 P9-B（`plugin/process/` 外部进程型深化）的
  A1（构建）/ A2（测试）/ A2'（零回归）/ A3（部署冒烟）/ A4（文档与踩坑）/ A5（阶段目标）/ A6（用户确认）
  **逐条通过**，证据见 `docs/ROADMAP-P9-Fin.md` §3.1 / §6 / §6.1。
- **范围变更**：§A.6 的「P9-C 暂缓、不引入 UI 宿主契约与贡献点协议」**改为**：启动 **P9-C**，
  净增 G1 `IPluginUiHost`（宿主窗口句柄 / 父 `QWidget` / 生命周期回调）与 G2 贡献点协议
  （右键菜单项 / 托盘项 / 设置页注册），使 **UI 面板型插件**（设置页 / 状态面板 / 内容面板 /
  小游戏窗口 / 对话面板）可在**不修改宿主**的前提下注册 UI。
  详见 `docs/ROADMAP-P9-Fin.md` §2.3 / §3.2（交付物与验收；**2026-10-06 经用户确认，作为门禁固化**）。
- **仍然保持**：A.2「不推倒重来、保留 `whalepet_view` 等静态库分层」的口径不变；
  `PLUGIN-ARCHITECTURE.md` §7 的零回归红线（`MiniGameRegistry` 一族一行不改）继续有效；
  G4（导出宏 / 稳定 ABI 子集）仍**不在范围内**，UI 型插件继续走**进程内 builtin 层**而非 DLL。
- **阶段登记**：P9-C 踩坑继续落 `docs/pitfalls/p9/`（序号接续 `P-085` 起）。
- **实现状态（2026-10-06）**：P9-C 交付物已按本节落地——契约 `src/plugin/ui/`（`IPluginUiHost` + 贡献点协议，
  仅前向声明 `QWidget`，保持 `whalepet_plugin` 零 Widgets）、宿主侧 `src/view/ui/`（`UiContributionHost`
  分发 + 试点 `StatusPanelUiPlugin`），`PetWindow` 删除硬编码「状态」入口改由贡献点驱动；
  **C1~C6 逐条通过（2026-10-06 完成）**，详见 `docs/ROADMAP-P9-Fin.md` §6.3 / §6.4。

---

## 附录 B · EX3/EX4 裁决：移除外部游戏陪玩 → 转向小游戏陪玩（2026-10-09，经用户确认）

> 性质：**架构范围变更裁决**。经用户对四项提问的确认，正式决定**移除 EX1 外部游戏陪玩**的运行期能力，
> 并把「陪玩」收敛为**自研小游戏的状态驱动型陪伴**。本节为最终结论；执行须逐阶段过 A1~A6 验收（`ROADMAP` 约定）。
> **编码尚未开始**——本节先固化范围与边界（G1 门禁产物）。

### B.1 决策依据（用户确认）

| # | 决策项 | 结论 |
|---|---|---|
| 1 | 小游戏陪玩形态 | **状态驱动型**：实时读小游戏状态 → `mood` 滞回 / 里程碑边沿 / 静默陪伴 |
| 2 | 复用策略 | **全部复用**：保留 `core::GameState`、`PetStateMachine` 游戏通道、`GameCompanionService` 编排、`context.gameState` 能力，仅替换数据源与里程碑判据 |
| 3 | 文档与踩坑 | **归档保留**：代码 / 测试 / EX1 专题文档 / 桥接样例移入 `dump/`（`.gitignore` 忽略、不入库）；**踩坑记录原位保留**（规范硬约束） |
| 4 | 阶段划分 | **两阶段**：`EX3` = 拆除外部陪玩；`EX4` = 小游戏陪玩 |

### B.2 EX3 范围：拆除外部游戏陪玩（EX1 运行期）

**删除（运行期 + 测试 + 构建）**

- `src/gamestate/` 全目录（31 文件 / 约 3460 行）：只读内存读取器、指针链解析、profile 加载、
  Unity（Mono/IL2CPP）与 RPG Maker（MV/MZ/RGSS）适配器、CDP 客户端、只读桥接、特殊场景检测。
- 测试目标 `test_game_memory` / `test_game_memory_e2e` / `test_unity_adapters` /
  `test_rpgmaker_adapters` / `test_gamestate_boundaries` 及辅助进程 `game_target_sim`。
- `docs/samples/rpgmaker-mv/`（只读桥接插件样例）。

**改造（为 EX4 铺路的唯一净增抽象）**

- 把 `gamestate::IGameStateAdapter` 泛化为**中立数据源接口**（暂名 `IGameCompanionSource`，落 `core/` 或
  `viewmodel/`），`GameCompanionService::AdapterFactory` 改绑该接口；`GameProfile` 随 `gamestate` 一并移除
  （进程内小游戏源不需要 profile）。
- `tests/test_game_companion.cpp` **保留并改造**：保留判定规则 / 状态机通道 / 服务启停等用例，
  剥离依赖 `gamestate`（profile / 适配器失效）的用例。

**保留不动（管线复用）**

- `src/core/GameState.{h,cpp}`（`GameSample`/`GameMood`/`GameMilestoneSet`/`GameCompanionRules`）、
  `PetStateMachine` 游戏通道、`PetController::handleGameState`、`PetContextProvider`/`ContextSnapshot` 的
  game 段、`context.gameState` 能力注册、`game-*` 立绘池与 `assets/lines/game.txt`、
  `docs/pitfalls/ex1/` 与 `p7/` 相关条目。
- **同时移除**：菜单项「游戏陪玩」与设置项 `game_companion_enabled` / `game_profile_path`
  （旧键由 `SettingsRepo::purgeLegacyGameCompanionKeys()` 在启动时清理）。
- `context.gameState` 能力**暂留**（无数据源时返回 `available=false`）；EX4 改名 `context.miniGame`
  并接入小游戏数据源、新增菜单「小游戏陪玩」。

**CMake / 依赖**

- 删 `whalepet_gamestate` 静态库目标与 `whalepet_view` 对它的链接；
  **删 `Qt6::WebSockets` 依赖**（全仓仅 `gamestate/CdpWebSocketClient` 使用）。

**文档（按决策 3，移入 `dump/`）**

- `ROADMAP-ex1.md`、`RPGMAKER-SOP.md`、`UNITY-SOP.md`、`NONACTION-COMPANION.md` 与
  `samples/rpgmaker-mv/` 已移入 `dump/docs/`（`.gitignore` 忽略，不入库）；
  `README.md` / `TESTING.md` / `CONTEXT-API.md` / `pitfalls/index.md` 同步改引用。

**预期量化**：源码净减约 3500 行；CTest **37 → 32**；Qt 模块依赖净减 1 个。

### B.3 EX4 范围：小游戏陪玩（**插件侧自描述 + 陪玩侧通用聚合**）

> **2026-10-09 修订（经用户确认）**：原表述「陪玩侧读三款小游戏状态对象并折算」会把
> **每个具体游戏的状态折算硬编码在陪玩侧**，导致「新增一个小游戏就要改陪玩代码」，违背插件化目标。
> 现改为**状态折算下沉到插件侧（自描述）+ 陪玩侧通用聚合**。

- **净增（插件侧，唯一接触点）**：`IMiniGameCompanionSource`（**可选**接口）——由小游戏**视图**
  把自身私有状态折算为**中立的** `core::GameSnapshot`；未实现该接口的游戏只是不参与陪玩，
  菜单 / 设置页 / 结算 / 成就照常。**不修改** `IMiniGamePlugin` / `MiniGameView` / `MiniGameRegistry`
  / `MiniGameContext` / `MiniGameInfo` 的任何签名（守住 §7 零回归红线）。
- **净增（中立契约与判定，零 Qt）**：`core::GameSnapshot`（游戏无关字段：进行态 / 进度 / 计数 / 危险）
  与 `core::MiniGameCompanion`（中立 `mood` 与里程碑边沿判定；与 `GameCompanionRules` 共用同一套
  置信度阈值与最短驻留滞回常量）。
- **净增（陪玩侧通用聚合）**：`viewmodel::MiniGameCompanionSource` —— 向宿主索取「当前可见的小游戏
  自描述源」候选并取首个可用者，**不含任何具体玩法的分支**；新增小游戏无需改动本文件。
- **改造（最小侵入）**：`IGameCompanionSource` 增加**可选**的中立快照出口 `readSnapshot()`
  （默认返回 `false` → 回落既有 `read()`）；`GameCompanionService::tick()` 优先走快照通道，
  并按 `core::MiniGameCompanion` 判定。既有 RPG 判定（`GameCompanionRules` / `GameSample`）与其
  全部既有用例**保持不变**（零回归）。
- **复用**：`GameCompanionService` 采样与编排、`PetStateMachine` 游戏通道（最低让位 + 静默陪伴）、
  `GameMood` / `GameMilestoneSet` → 立绘与 `game.*` 台词映射（零新增美术）。
- **接线**：宿主装配通用聚合数据源，并**随「是否有可见小游戏窗口」启停采样**（无小游戏时零开销，
  保持既有「未启用即零开销」口径）。
- **修订 B.5.2**：**不新增**「小游戏陪玩」菜单开关 —— 陪玩在小游戏窗口打开时**自动生效**、
  窗口关闭即停；`context.gameState` 能力 id **暂留**（其数据自动来自小游戏陪玩），本期不引入
  `context.miniGame`。
- **需明确（保留）**：状态驱动陪玩与既有 `presentGame`（一次性表现）的让位/互斥语义，避免双源抢
  立绘与台词；当前沿用「一次性表现优先、陪玩态最低让位」既有语义（见 `STATE-MACHINE.md`）。
- **新增测试**：`test_minigame_companion`（中立规则 / 通用聚合 / 编排集成 / 「新游戏零改动接入」）。

### B.4 红线与约束（不变）

- **不推倒重来**，保留 `whalepet_view` 等静态库分层与既有依赖方向。
- `MiniGameRegistry` 一族**零回归**（`PLUGIN-ARCHITECTURE.md` §7）。
- **踩坑记录不得删除**，EX1/P7 相关条目原位保留为历史。
- 崩溃一律按 `docs/README.md` §六交回用户，AI 不自行调试。

### B.5 细节决策（2026-10-09 已确认）

1. **外部陪玩代码与文档**：移入工作区 `dump/` 并在 `.gitignore` 忽略（不入库，备查）。
2. **命名**：~~菜单「游戏陪玩」→「小游戏陪玩」，能力 id `context.gameState` → `context.miniGame`~~
   —— **2026-10-09 修订（见 B.3）**：陪玩改为**随小游戏窗口自动启停**，不新增菜单开关；
   `context.gameState` 能力 id 本期**暂留不变**（其数据自动来自小游戏陪玩）。
3. **版本与设置**：版本升至 **0.3.0**（移除记入 `docs/release.md`）；启动时清理旧设置键
   （`SettingsRepo::purgeLegacyGameCompanionKeys()`）。
4. **开工顺序**：先完成 **EX3** 并验收，再另起一轮做 **EX4**。

### B.6 EX4 实现记录（2026-10-09，已完成）

按 B.3 修订后的方向落地，**「新增小游戏 → 陪玩零改动接入」目标达成**：

| 层 | 新增 / 改动 | 位置 |
|---|---|---|
| 中立契约 | **新增** `core::GameSnapshot`（零 Qt） | `src/core/GameSnapshot.h` |
| 中立判定 | **新增** `core::MiniGameCompanion`：`miniGameCandidate` / `miniGameEvaluate` / `miniGameMilestones` / `gameSampleFromSnapshot`（零 Qt） | `src/core/MiniGameCompanion.{h,cpp}` |
| 插件侧自描述 | **新增** `IMiniGameCompanionSource`（可选接口）；扫雷 / 找小猫 / 象棋**三个视图**实现 `companionSnapshot()` | `src/minigame/MiniGameCompanionSource.h`、`src/minigame/{minesweeper,kitten,chess}/*View.{h,cpp}` |
| 陪玩侧通用聚合 | **新增** `viewmodel::MiniGameCompanionSource`（`readSnapshot()` 走中立通道，`read()` 折算兼容管线） | `src/viewmodel/MiniGameCompanionSource.{h,cpp}` |
| 数据源契约 | **改动（向后兼容）** `IGameCompanionSource` 增加默认 `readSnapshot()`（默认 `false` → 回落 `read()`） | `src/viewmodel/IGameCompanionSource.h` |
| 编排 | **改动** `GameCompanionService` 增加 `onSnapshot()`；`tick()` 优先中立快照通道，否则回落既有 RPG 路径 | `src/viewmodel/GameCompanionService.{h,cpp}` |
| 宿主接线 | **改动** `PetWindow::setupGameCompanion()` 装配通用聚合数据源；新增 `syncGameCompanion()` 随窗口显隐启停；`showMiniGame()` 打开即启、`QDialog::finished` 关闭即停 | `src/view/PetWindow.{h,cpp}` |
| 构建 | **改动** `cmake/Libraries.cmake`（新增 6 个源文件）、`cmake/Tests.cmake`（新增 `test_minigame_companion`） | `cmake/*.cmake` |
| 测试 | **新增** `tests/test_minigame_companion.cpp`（14 用例 + init/cleanup；覆盖中立规则 / 通用聚合 / 编排 / 「全新游戏零改动接入」） | `tests/` |

**验证**：Debug / Release 构建退出码 0；CTest Debug / Release 各 **33/33**（既有 32 项零回归，
新增 `test_minigame_companion` 16 项全 PASS）。**未删除任何既有断言、未放宽任何条件**。

**踩坑**：`P-094`（新增 `.cpp` 漏 include 自身头）、`P-095`（most vexing parse → C2228）、
`P-096`（PowerShell 管道吞退出码，**重复命中 P-004**）。

**明确边界（未做 / 不做）**：不引入 `context.miniGame` 能力 id 改名；不新增菜单 / 设置开关；
`GameSample` 与 `GameCompanionRules`（EX1 RPG 语义）**保留不动**，仅由 EX4 中立通道并行使用。

---

## 附录 C · EX6 裁决：随机闪现（2026-10-10，经用户确认）

### C.1 决策依据（用户确认）

**约束（用户给定）**：**复用现有立绘、零新依赖**。同一轮并发子代理调研（只读、带源码证据）的对比结论：

| 候选 | 结论 |
|---|---|
| 随机移动（连续位移） | **需新增美术**：93 张立绘无 walk / move 语义（`running` 已被编程态占用），「平移」还会被读成漂移；且需新增持续窗口位移通道 |
| **随机闪现（瞬时换位）** | **可行且明显更优**：不需要新图（闪现**不换姿态**）、不新增持续位移通道、单次 260ms 表现、不进状态机 → 挂机 / 睡眠 / 工作 / 贴边口径零回归 |

**用户裁决**：2026-10-10「按照建议进行实现」——采纳评估中的 Q1–Q6 全部建议：
主方案（不换立绘、纯几何 + 透明度）、**默认关**、2–8 分钟随机单次、加淡入淡出、
只在出现端一次粒子、阶段归**扩展阶段 EX6**。

### C.2 范围与红线

- **不动**：既有 93 张立绘及其登记（`core/PoseNames.h` 与 `assets/assets.qrc` **零改动**）、
  `PoseLibrary` 预载分档与容量（core 14 + warm 25 = 39 < 40，见 `P-063`）、
  状态机 `PetStateMachine`（**一行未改**）、小游戏插件一族与结算。
- **新增**：`src/core/RandomFlickerRules.h`（零 Qt 纯逻辑）、`src/viewmodel/FlickerService.{h,cpp}`、
  `src/viewmodel/builtin/FlickerServicePlugin.{h,cpp}`、`tests/test_flicker.cpp`。
- **改动**：`PoseView`（`startFlash()` / `frameAlpha()` / `flashMidpointReached()`）、
  `PetWindow`（装配 / 门槛 / 落点 / 位移 / 设置生效）、
  `SettingsData` + `SettingsRepo` + `SettingsDialog`（`flicker_enabled`）、
  `DialoguePanel`（`reposition()` 由 `private` 改为 `public`：面板**不随窗口移动自动重排**）、
  `BuiltinServicePlugins`（宿主服务 5 → 6）、`cmake/Libraries.cmake` + `cmake/Tests.cmake`。
- **零新依赖**：仅 Qt 官方模块（Core / Gui / Widgets）+ 既有自研纯逻辑头；粒子仍全部程序化自绘。

### C.3 实现记录（2026-10-10）

| 关注点 | 落点 |
|---|---|
| 落点算法（纯逻辑） | `core::flickerSafeRect()` / `pickFlickerTarget()` / `nextFlickerDelayMs()`：四边内缩 **41px**（= 贴边带 20 + 1 + 立绘透明留白 20）、最小位移 **120px**、抽样 12 次、退化**对角镜像**、空随机源与过小屏幕**一律放弃**（不猜坐标） |
| 调度与门槛 | `viewmodel::FlickerService`（随机单次 `QTimer`，与回收站提醒同构）；门槛经 `BuiltinServiceHooks::flickerAllowed` 注入，**不满足即静默跳过本轮**，等下一轮 2–8 分钟 |
| 表现 | `PoseView::startFlash()` + `frameAlpha()`：**260ms**，缩放 `1.0→0.35→1.0`、不透明度 `1.0→0→1.0`；位移发生在 `flashMidpointReached()`（**帧进度驱动**，遵 `P-011`） |
| 收尾顺序 | `move()` → `clampToVisibleArea()` → `syncDesktopEdge()` → `DialoguePanel::reposition()` → `playFx(Fx::Star)`（**只在出现端一次**，受 500ms 全局冷却约束）；**不调用** `snapToDesktopEdge()` |
| 设置 | `flicker_enabled` **默认关**；右键菜单 + 设置面板双入口；`setFlickerEnabled()` 幂等落库（`json_ext`，**不新建列**） |

### C.4 验收状态

- **A1 构建 / A2 测试**：✅ **用户确认通过**（Debug 构建 + CTest；新增 `test_flicker`，CTest 目标 34 → 35）。
- **A3 产物可运行**：⏳ **待人工目视** —— 开启「随机闪现」后等 2–8 分钟观察一次闪现；
  默认关，故关闭态行为与既有完全一致。
- **A4 / A5 文档与踩坑**：✅ 本轮已刷新（`SETTINGS.md` / `PRESENTATION.md` §2 + §3.2 /
  `STATE-MACHINE.md` / `TESTING.md` / `DATA-MODEL.md` / 本附录 / `docs/pitfalls/ex6/`）。
- **A6 用户确认**：⏳ 待用户目视验收。

**踩坑**：`P-104`（Qt Creator 多配置预设未声明 `CMAKE_BUILD_TYPE` → Build type 为空 →
`No "" CMake configuration found`，见 `docs/pitfalls/ex6/`）。

---

## 附录 D · P10 裁决：工作能力插件（2026-10-11，经用户确认）

> 本附录是本阶段（**主阶段 P10**）的架构方案与**用户确认结论**，实施以此为准；
> 阶段归档见 `docs/pitfalls/p10/`，验收条件（A1~A6）随 `ROADMAP-P10.md` 维护。

### D.1 决策依据

**原始需求（用户，2026-10-11）**：「为桌宠添加**工作能力插件**——在 Python 环境下，编写一些预制代码块，
在用户选择工作时，提供列表，呈现可用功能，在经过一段时间后输出 python 脚本在用户指定目录。
同时将戳一戳的彩蛋功能迁移到桌宠工作这一个插件中，在工作期间戳一戳会在 python 脚本中插入注释的
俏皮话（每次工作至多触发一次）」。

**前置研判（并发只读子代理调研 + 主代理汇总裁决）：结论为「可行」**，关键证据：

| 关注点 | 结论 | 证据 |
|---|---|---|
| 插件落位 | **内置 builtin 层**最自然：`PluginContext.controller`（`PetController*`）可驱动立绘/台词，`contributions()` 可零侵入挂菜单；DLL 层拿不到宿主内部对象，外部进程层只适合可隔离的第三方工具 | `src/plugin/PluginInterface.h:44-48`、`src/view/PetWindow.cpp:366-379` |
| 「Python 环境」口径 | **不需要解释器**：产物是 `.py` 文本，不联网、不执行生成的代码；符合 `BUILD.md` §5「零第三方依赖」（不可协商） | `docs/BUILD.md:122-129` |
| 内容资源分发 | 走 **qrc**（现有语料/地图全部编译进 `assets.qrc`）→ `package-release.ps1` / `installer.nsi` / `packages.md` 三处清单**零改动** | `src/viewmodel/PosePresenter.cpp:22-32`、`assets/assets.qrc:97-109`、`docs/packages.md:226-230` |
| 会话生命周期 | 照 `DialogueService` 范式：服务自持 `QTimer` + `start/stop/cancel` 三件套 + 幂等清理 + 「进行中」状态位防重复打开 | `src/viewmodel/DialogueService.cpp:140-174,226-230` |
| 文件产出 | `QSaveFile` 原子写，**只写用户显式指定目录**（绝不写安装/数据目录） | 迁移前由 `viewmodel::EasterEggService` 提供（**已退役**）；现由 `viewmodel::WorkService::produce()` 承接 |
| 戳一戳彩蛋 | 迁移前**已是「真写文件」**实现（注释注入 / `whalepet-egg` 幂等标记 / 边界完备 / 14 条俏皮话），故整体迁移可行 | 迁移前 `core/CodeEasterEgg` + `viewmodel::EasterEggService`（**均已退役 / 瘦身**；现为 `core::eggCommentLine()` + `WorkService::onPoke()`） |

**用户裁决（2026-10-11）**：

1. **立项 `P10`**，范围＝**纯产出**（内置 builtin 插件 + qrc 预制块库 + 工作台面板 + 到时/手动产出 `.py`
   到用户指定目录）；**不引入 Python 解释器依赖**。
2. 工作会话期间的常驻工作立绘：**新增「桌宠互动（菜单选项）」状态机通道**，由该通道**向工作插件
   暴露接口**，并**更改核心状态机优先级**——该通道**仅低于深夜**。
3. 旧设置键 `code_egg_enabled` / `code_egg_workspace` **退役**，迁移为新 `work.*` 键。
4. 本轮「构建与部署目录收敛」**并入 P10 阶段归档**（不再单列扩展阶段）。

### D.2 范围与红线

- **新增**：
  - `src/core/PythonBlocks.{h,cpp}`（零 Qt 纯逻辑：块模型、唯一索引解析、`assembleScript()`、`insertEggComment()`）；
  - `src/viewmodel/WorkService.{h,cpp}`（工作会话：时长、进度、到时/手动产出、取消、蛋标记）；
  - `src/viewmodel/builtin/WorkServicePlugin.{h,cpp}`（builtin 插件；能力 id `service.work`；菜单/托盘贡献点）；
  - `src/view/ui/WorkBenchPanel.{h,cpp}`（工作台面板：功能块列表 + 多选 + 时长 + 输出目录 + 开始/取消/立即产出）；
  - `assets/python/blocks/index.txt`（**唯一索引**：id / 标题 / 类别 / 一句话说明 / 依赖）+ `assets/python/blocks/*.txt`（块正文）；
  - `tests/test_python_blocks.cpp`（组装与注释插入的纯逻辑用例）。
- **改动**：`core/PetTypes.h`（新通道枚举 + `EventType` + `Event` 工厂）、`core/PetStateMachine.{h,cpp}`、
  `viewmodel/PetController.{h,cpp}`、`view/PetWindow.cpp`（装配 / 设置生效 / 菜单贡献点 / 会话门槛）、
  `model/SettingsData.h` + `SettingsRepo.cpp` + `view/SettingsDialog.cpp`（`work.*` 键，旧键退役）、
  `viewmodel/builtin/BuiltinServicePlugins.{h,cpp}`（宿主服务 6 → 7）、`assets/assets.qrc`、
  `cmake/Libraries.cmake` + `cmake/Tests.cmake`、
  `tests/test_state_machine.cpp` / `test_service_plugins.cpp` / `test_settings.cpp`（同步既有断言计数）。
- **零新依赖**：仅 Qt 官方模块（Core / Gui / Widgets / Sql）+ 自研纯逻辑头。
- **明确不做**：不引入 Python 解释器、不执行生成的脚本、不联网、**不再改写用户源码工作区**；
  不改 `context.workState` 语义（**感知用户工作** ≠ **桌宠自己工作**，两者只共用能力总线与立绘资源）；
  不改小游戏插件族与结算；不新增 qrc 之外的随包清单。

### D.3 状态机：新增「桌宠互动（菜单选项）」通道（用户口径）

**通道语义**：由**菜单选项触发的桌宠互动**（如「工作台…」开启的工作会话）产生的**常驻表现**
（立绘 + 台词），与「感知用户工作」的 `WorkState` 通道**完全解耦**。

**优先级（本阶段修订后，从高到低）**：

| 顺位 | 档位 | 说明 |
|---|---|---|
| 1 | 一次性事件 | 点击/连击/喂食/摸头/升级等**短时反馈**，仍在 `Tick` 侧覆盖（`PetStateMachine.cpp:371-399`），不参与常驻链比较 |
| 2 | **深夜** | `DaySlot::LateNight`（23:00–06:59，含虚弱态）——**仍在菜单互动之上**（用户口径：「仅低于深夜」） |
| 3 | **菜单互动（新增）** | `core::MenuInteraction`，常驻表现；**高于**工作态 / 睡眠循环 / 时段态 / 满值 / 挂机 / 陪玩 / 静息 |
| 4 | 工作态（感知） | `WorkState`（Reading / Coding / VibeCoding / Debugging / Meeting 等） |
| 5 | 睡眠循环 / 时段态 / 满值 / 挂机 / 游戏陪玩 / 静息 | 既有档位保持不变 |

- **插入点**：`PetStateMachine::contextPose()` 中「深夜档 `return`」之后、「工作态档」之前
  （现状即 `src/core/PetStateMachine.cpp:118` 与 `:125` 之间），保证「仅低于深夜、高于其余全部常驻档」。
- **深夜不可打断**：通道进入/变化时，深夜时段一律不播报（与工作态同口径，见 `:568-571`）。
- **接口暴露**：`PetController::handleMenuInteraction(core::MenuInteraction)` + 查询 `menuInteraction()`；
  插件经 `PluginContext.controller` 调用（**不要求插件直接操作状态机**）。

### D.4 工作会话与彩蛋迁移口径

| 关注点 | 口径 |
|---|---|
| 会话入口 | 右键菜单 / 托盘「工作台…」→ 面板（`createView` 懒建、插件自持，范式同 `StatusPanelUiPlugin` 一族） |
| 会话参数 | 功能块**多选**（清单取自 qrc 唯一索引）+ 时长预设 **5 / 15 / 25 / 45 分钟** + 「立即产出」+「取消」（取消不产出） |
| 产出 | 到时或手动：`assembleScript()` 生成 `.py`（头注释含块清单与生成时间 + 逐块**原样落盘** + 分隔）→ `QSaveFile` 原子写 → **只写用户显式指定的输出目录**；文件名 `whalepet_work_<yyyyMMdd-HHmmss>.py`，已存在则顺延序号 |
| 语法正确性 | **不做执行验证**：组装只加注释头/分隔，块级由作者预校验；单测断言「输出 = 头 + 块序拼接」；脚本头注明「由 WhalePet 生成，未经运行验证」 |
| 彩蛋迁移 | 会话进行中「戳一下」（`interactionOccurred(Poke)`）→ 开关开启且**本会话未触发过** → 标记 `eggArmed`；**产出时**把俏皮话作为注释写入脚本（含 `whalepet-egg` 幂等标记）。**每次会话至多一次**；会话外戳一戳不再有插话效果（普通戳一戳反应保留）。原「5% 概率改写用户源码工作区」路径**整体退役** |
| 设置键 | 新增 `work_output_dir`（默认空 = 面板提示先选目录）、`work_duration_min`（默认 25）、`work_enabled`（「工作期间戳一戳插俏皮话」开关，**默认关**）；旧 `code_egg_enabled` / `code_egg_workspace` **退役**（保留在库、不再读写，`SETTINGS.md` 标注已废弃） |

### D.5 验收状态（进行中）

| 交付项 | 状态 |
|---|---|
| ① 构建与部署目录收敛（构建目录固定 `build/`，删除 `build-package/`、`deploy-release/`） | ✅ **已完成**（2026-10-11）：Debug / Release CTest 各 **35/35**；发布脚本回归 §8.2 第 1～9 项通过（第 10 项待人工验收）；踩坑 `P-105` / `P-106` |
| ② 状态机「菜单互动」通道 + 插件接口 | ✅ **已完成**（2026-10-11）：`core::MenuInteraction`（`None` / `Work`）+ `EventType::MenuInteractionChanged` + `PetController::handleMenuInteraction()`；`contextPose()` 第 1 档（**仅低于深夜**），工作态 / 陪玩态在会话期间「只记录不表现」；新增 `test_state_machine::menuInteractionOutranksWorkAndSlot` / `menuInteractionYieldsToLateNight`（33/33 通过，既有 31 条零回归） |
| ③ 工作能力插件（块库 / 面板 / 会话 / 产出） | ✅ **已完成**（2026-10-11）：`core::PythonBlocks`（唯一索引解析 / 组装 / 末尾分发器）、`assets/python/blocks/`（唯一索引 + 9 个块正文）、`viewmodel::WorkService`（会话 / 计时 / QSaveFile 原子写产出、**绝不覆盖**）、`builtin::WorkServicePlugin`（能力 `service.work` + 右键 / 托盘「工作台…」）、`ui::WorkBenchPanel`；新增 `tests/test_python_blocks`（含「块正文必须定义 `run_<entry>()` 且已登记 `assets.qrc`」的资源守卫） |
| ④ 戳一戳彩蛋迁移 + `work.*` 设置键 | ✅ **已完成**（2026-10-11）：`WorkService::onPoke()`（**会话内至多一次**）+ 产出时插入 `# … (whalepet-egg)` 注释；`viewmodel::EasterEggService` 与其 builtin 插件**整体删除**、`core/CodeEasterEgg` 瘦身为「话池 + `eggCommentLine()`」；新增 `work_enabled` / `work_output_dir` / `work_duration_min`，旧 `code_egg_*` **退役**（`SETTINGS.md` §2 已标注） |
| A1 / A2 构建与测试 | ✅ Debug / Release 构建退出码 0；CTest 各 **36/36**（2026-10-11，既有 35 项零回归） |
| A3 产物可运行 / 可部署 | ✅ 发布脚本回归：复用 `build` 重出 0.3.0 三产物，`TESTING.md` §8.2 第 1～9 项通过（第 10 项安装包静默往返需 UAC，**待人工验收**） |
| A4 / A5 文档与踩坑 | ✅ `ARCHITECTURE.md` 附录 D、`STATE-MACHINE.md` §1.2、`SETTINGS.md` §2、`TESTING.md` §2、`PLUGIN-ARCHITECTURE.md` 服务清单、`CONTEXT-API.md` 能力表、`README.md`（根 + `docs/`）、`release.md` 已刷新；踩坑 `P-105` … `P-109`（`docs/pitfalls/p10/`） |
| A6 用户确认 | ⏳ 待阶段收尾（建议目视验收：工作台面板交互、产出脚本内容、工作期立绘、戳一戳插俏皮话） |

---

## 附录 E · P11 裁决：绘画模块与「工作台多页化」（2026-10-11，经用户确认）

### E.1 需求与约束

用户要求新增**绘画模块**并集成到「工作台…」菜单路径下，含三项功能：抽象画生成器（随机配色 +
QPainter 实时画布 + 连续刷新）、线条画生成器（基础平面几何线框组合 + 图形拼接 + 风格一致性）、
交互式简易画板（纯白画布 + 外围控件严格遵循 qt-ui 样式 + 屏幕取色，参考 `references/src` 的 WinEase）。

### E.2 开工前勘察到的硬事实

1. 「工作台…」是 `builtin.work` 的**一个** UI 贡献点，点击懒建 `ui::WorkBenchPanel`（顶层工具窗、
   **单页哑面板**）；原贡献点协议只有 `ContextMenu` / `TrayMenu` / `SettingsTab` 三种 kind，
   **没有任何「在既有面板内挂子入口」的机制**；
2. 全局样式表里 `QWidget` 背景为黑，且 qt-ui 规范**禁止子控件局部 `setStyleSheet`**
   ⇒ 纯白画布需要「内容完全覆盖」的做法（画布自绘白底位图），不能靠局部样式；
3. `references/src`（WinEase）**没有取色器本体**，可复用的是底层件：`ScreenCapture::colorAt`
   （1×1 BitBlt，实测 `GetPixel` 在 DWM / 分层窗口下会返回过期色）、系统 Magnification API 放大镜、
   全屏 `OverlayWindow`、全局钩子 / 热键；**该拷贝已按用户指示于同日整目录移除**（属用户本人
   个人项目，处置见 E.6）；
4. 构建目录 `build/` 已存在（VS 多配置）⇒ **复用不新建**。

### E.3 裁决（用户确认的四项口径）

| # | 决策 | 结论与理由 |
|---|---|---|
| 1 | **集成方式** | **工作台多页化 + 新增 `ContributionKind::WorkbenchPage`**。理由：语义上真正「集成到工作台…菜单路径下」，且保持插件化红线 —— 宿主按 kind 通用分发，**不为绘画加任何业务分支**；新增绘画页 / 后续新页都只需贡献点。替代方案（独立「绘画…」面板并列于工作台 / 宿主自建子菜单）分别牺牲了菜单路径语义与「宿主不写业务分支」的约束，故未采用 |
| 2 | **取色路线** | **完全照参考**：Win32 **1×1 BitBlt** 取色 + 系统 **Magnification API** 放大镜。新增链接 `gdi32` / `magnification`（Windows SDK 系统库，**非第三方依赖**，已明确提示并获同意）；`MagInitialize` 不可用时降级为自绘放大镜（可观测、不静默） |
| 3 | **抽象画配色** | **完全随机 RGB**（三通道独立 [0,255]，不做色相簇限制与对比度过滤）；仅保留 alpha ∈ [64,220] 保证「每一笔看得见」 |
| 4 | **阶段归属** | **主阶段 P11**（新可交付里程碑）：`docs/DRAWING.md` + `docs/ROADMAP-P11.md` + 踩坑 `docs/pitfalls/p11/`，按 A1~A6 验收 |

### E.4 架构落位（净增 + 一处结构升级）

| 层 | 落位 | 说明 |
|---|---|---|
| 契约（`whalepet_plugin`） | `plugin/ui/PluginContribution.h` 新增 `WorkbenchPage` | 仍只依赖 `Qt6::Core`，`QWidget` 保持前向声明 |
| 宿主分发（`whalepet_view`） | `view/ui/UiContributionHost::appendToTabs(kind)`（默认 `SettingsTab`） | 与既有 `SettingsTab` 共用一条分发实现；旧调用零改动 |
| 容器（`whalepet_view`） | `view/ui/WorkBenchPanel` 改为页签容器 + `FillPagesFn` | 第 1 页仍是 P10「干活」页；本类不认识任何具体页面 |
| 接线（宿主） | `BuiltinServiceHooks::workbenchFillPages` → `WorkServicePlugin` → 面板；`PetWindow::setupUiPlugins()` 注册 `ui::DrawingUiPlugin` | 宿主只提供「按 kind 分发」一个通用回调 |
| 逻辑（`whalepet_core`，零 Qt） | `DrawingPalette` / `AbstractArt` / `LineArt` / `CanvasBoard` | 全可脱界面单测 |
| 平台（`whalepet_platform`） | `Win32ScreenColorPicker`（BitBlt 取色 + 系统放大镜，含降级） | 新增链接库仅 `gdi32` / `magnification` |
| 视图（`whalepet_view`） | `ColorSwatch` / `DrawingColors` / `DrawingShapes` / `AbstractArtView` / `LineArtView` / `DrawingBoardView` / `ScreenColorPickerOverlay` / `DrawingUiPlugin` | 页面所有权归页签容器，插件不删（避免双重释放） |

### E.5 验收状态

| 项 | 结果 |
|---|---|
| A1 / A2 构建与测试 | ✅ Debug / Release 构建退出码 0；CTest 各 **37/37**（2026-10-11，既有 36 项零回归） |
| A3 产物可运行 | ✅ 构建产物有界存活冒烟通过（复用 `build/`，未新建构建目录） |
| A4 / A5 文档与踩坑 | ✅ `DRAWING.md` / `ROADMAP-P11.md` / 本附录 / `docs/README.md`（文档清单 + 阶段对照 + 当前状态）/ `docs/pitfalls/index.md`（阶段表 + P11 索引）已刷新；踩坑 `P-110` … `P-112` |
| A6 用户确认 | ⏳ 待目视验收（清单见 `ROADMAP-P11.md` §5） |

### E.6 参考来源处置（2026-10-11，同日）

P11 开工时用于屏幕取色调研的参考拷贝 `references/src/`（WinEase 的**部分源码拷贝**，148 个文件）
**已按用户指示整目录移除**；用户说明该参考为其**个人项目**。处置口径：

- P11 的取色 / 放大镜是**按官方 API 语义的独立实现**（`src/platform/Win32ScreenColorPicker.*`、
  `src/view/ui/ScreenColorPickerOverlay.*`），**未复制其源码** ⇒ **移除不影响任何已交付代码**，
  也无需重跑验证（本轮只是删除只读参考 + 文档/注释口径更新）；
- 本项目 `LICENSE` 维持 **MIT**（不涉及第三方协议待确认项）；
- `references/` 现只剩 `DesktopPet/` 与 `dsh-whale-musume/`（登记见 `references/README.md` §二）。
