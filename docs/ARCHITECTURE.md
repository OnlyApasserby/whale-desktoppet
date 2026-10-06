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

## 附录 A · 「完全插件化重构」可行性调研（2026-10-05，✅ P9-A / P9-B 验收通过；P9-C 已启动）

> 调研方式：只读扫描 + 并发子代理核查（组合根/UI 耦合、viewmodel·core·model 可拆性、插件总线与构建目标承载力、参考项目做法）。
> 结论状态：**P9-A / P9-B 验收通过；P9-C 已启动**——§A.5 曾裁决「暂不推进」，2026-10-05 经用户**重新裁决推翻**，启动 **P9-A + P9-B**（见 §A.6）；二者逐条通过 A1~A6 验收后，经用户再次指示**启动 P9-C**（引入 G1 `IPluginUiHost` 与 G2 贡献点协议，见 §A.7）。
> 「未确认前不修改任何模块边界」原则已在 §A.7 由用户解除：**P9-C 经确认后正式引入 G1（UI 宿主上下文）与 G2（贡献点协议）**。

### A.1 现状事实（已验证，均带源码/CMake 证据）

| # | 事实 | 证据 |
|---|---|---|
| 1 | 全部业务模块是**编译期静态库**，没有任何动态边界 | `cmake/Libraries.cmake:19/78/113/141/186/214/255`（7 个 `qt_add_library(... STATIC)`） |
| 2 | **`view` + `viewmodel` + `minigame` 合编为一个静态库** `whalepet_view`，并以 `PUBLIC` 链接 core/model/platform/plugin/contextapi/gamestate | `cmake/Libraries.cmake:255-361` |
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
- **路线图与验收标准**：见 **`docs/ROADMAP-P9.md`**（P9-A / P9-B 交付物、A1~A6 验收与零回归约束）。

### A.7 用户裁决（2026-10-05，P9-A / P9-B 验收通过 → 启动 P9-C）

> **结论：P9-A / P9-B 逐条通过 A1~A6 验收；据此启动 P9-C，正式引入 G1（`IPluginUiHost` UI 宿主上下文）与 G2（贡献点协议）。**（本节修订 §A.6 中「P9-C 暂缓」的部分；A.5 / A.6 保留为决策留痕。）

- **验收确认**：P9-A（宿主服务经 builtin 层注册化）与 P9-B（`plugin/process/` 外部进程型深化）的
  A1（构建）/ A2（测试）/ A2'（零回归）/ A3（部署冒烟）/ A4（文档与踩坑）/ A5（阶段目标）/ A6（用户确认）
  **逐条通过**，证据见 `docs/ROADMAP-P9.md` §3.1 / §6 / §6.1。
- **范围变更**：§A.6 的「P9-C 暂缓、不引入 UI 宿主契约与贡献点协议」**改为**：启动 **P9-C**，
  净增 G1 `IPluginUiHost`（宿主窗口句柄 / 父 `QWidget` / 生命周期回调）与 G2 贡献点协议
  （右键菜单项 / 托盘项 / 设置页注册），使 **UI 面板型插件**（设置页 / 状态面板 / 内容面板 /
  小游戏窗口 / 对话面板）可在**不修改宿主**的前提下注册 UI。
  详见 `docs/ROADMAP-P9.md` §2.3 / §3.2（交付物与验收，初版草案待用户确认）。
- **仍然保持**：A.2「不推倒重来、保留 `whalepet_view` 等静态库分层」的口径不变；
  `PLUGIN-ARCHITECTURE.md` §7 的零回归红线（`MiniGameRegistry` 一族一行不改）继续有效；
  G4（导出宏 / 稳定 ABI 子集）仍**不在范围内**，UI 型插件继续走**进程内 builtin 层**而非 DLL。
- **阶段登记**：P9-C 踩坑继续落 `docs/pitfalls/p9/`（序号接续 `P-084` 起）。
