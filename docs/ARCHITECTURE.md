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
