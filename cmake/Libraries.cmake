# ---------------------------------------------------------------------------
# 模块：Libraries —— 全部静态库目标定义
# 职责：定义工程内所有静态库 target 及其源文件、include 路径、链接依赖：
#   whalepet_core      零 Qt UI 依赖的核心逻辑
#   whalepet_model     数据层（Qt6::Core + Qt6::Sql）
#   whalepet_platform  桌面环境感知（Win32）
#   whalepet_gamestate 游戏状态只读读取底座（EX1）
#   whalepet_plugin    通用能力总线（P7）
#   whalepet_contextapi 本地 Context API（P7）
#   whalepet_view      视图与 viewmodel 层（供 WhalePet 与测试复用）
# 依赖：CompileOptions（全局编译选项）、QtDependencies（Qt6 target 可用）。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域。
#       模块内目标按“底层 → 上层”顺序定义，后定义者可链接前序目标。
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Core 静态库：零 Qt UI 依赖（仅 C++17 标准库），可脱离界面单测
# ---------------------------------------------------------------------------
qt_add_library(whalepet_core STATIC
    src/core/PoseNames.h
    src/core/PoseCatalog.h
    # 桌面四边框贴边判定（纯逻辑，零 Qt 依赖，见 docs/PRESENTATION.md §3.1）
    src/core/DesktopEdge.h
    src/core/PetTypes.h
    src/core/IRandom.h
    src/core/LineTable.h
    src/core/LineTable.cpp
    src/core/PetStateMachine.h
    src/core/PetStateMachine.cpp
    src/core/GrowthRules.h
    src/core/ChatRules.h
    src/core/Calendar.h
    # 节日换装（公历固定日 + 农历小表；零 Qt 纯函数，见 docs/STATE-MACHINE.md §5.1）
    src/core/FestivalRules.h
    src/core/Achievements.h
    src/core/Quests.h
    src/core/SigninRules.h
    src/core/MiniGameTypes.h
    src/core/Minesweeper.h
    src/core/Minesweeper.cpp
    src/core/RobotKitten.h
    src/core/RobotKitten.cpp
    # 小游戏：国际象棋（纯逻辑：FEN / 合法着法 / 王车易位 / 过路兵 / 升变 / 将死逼和 / 结算折算）
    src/core/Chess.h
    src/core/Chess.cpp
    # P7：工作状态（含 Coding / Vibe Coding 判据）—— 零 Qt 纯逻辑，可脱 UI 单测
    src/core/WorkState.h
    src/core/WorkState.cpp
    src/core/WorkStateRules.h
    src/core/WorkStateRules.cpp
    # EX1：游戏陪玩状态（GameSample / GameCompanionRules / 特殊场景）—— 零 Qt 纯逻辑，可脱 UI 单测
    src/core/GameState.h
    src/core/GameState.cpp
    # P8：时段常驻立绘（日间 / 傍晚 / 深夜 + 深夜唤醒窗口）—— 零 Qt 纯逻辑
    src/core/DaySlotRules.h
    # P8：工作立绘池（work-* 轮转 + 编程族 running + 热词 / ACP 联动）
    src/core/WorkPosePool.h
    src/core/WorkPosePool.cpp
    # P8：预设对话（用户提问 → 鲸鱼娘回答；语料解析 + 五选一选项池）
    src/core/PresetDialogue.h
    src/core/PresetDialogue.cpp
    src/core/DialogueOptions.h
    src/core/DialogueOptions.cpp
    src/core/DialoguePoseRules.h
    # P8：天气类型判定（彩云天气中文描述 → 类型 → 立绘）
    src/core/WeatherRules.h
    src/core/WeatherRules.cpp
    # EX 彩蛋（experiment/easter-egg1）：源码注释段落识别与俏皮话注入（零 Qt 纯逻辑）
    src/core/CodeEasterEgg.h
    src/core/CodeEasterEgg.cpp
)
target_include_directories(whalepet_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ---------------------------------------------------------------------------
# Model 静态库：Qt6::Core + Qt6::Sql（**不链接 Widgets**），可 headless 单测
#   —— 连接/建表/迁移/事务/降级 + 仓储。见 docs/DATA-MODEL.md §4
# ---------------------------------------------------------------------------
qt_add_library(whalepet_model STATIC
    src/model/DataPaths.h
    src/model/DataPaths.cpp
    src/model/PetStateData.h
    src/model/SettingsData.h
    src/model/Schema.h
    src/model/Schema.cpp
    src/model/Database.h
    src/model/Database.cpp
    src/model/PetStateRepo.h
    src/model/PetStateRepo.cpp
    src/model/SettingsRepo.h
    src/model/SettingsRepo.cpp
    src/model/AchievementRepo.h
    src/model/AchievementRepo.cpp
    src/model/QuestRepo.h
    src/model/QuestRepo.cpp
    src/model/SigninRepo.h
    src/model/SigninRepo.cpp
    src/model/DiaryRepo.h
    src/model/DiaryRepo.cpp
    src/model/HotwordRepo.h
    src/model/HotwordRepo.cpp
)
target_include_directories(whalepet_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(whalepet_model
    PUBLIC Qt6::Core Qt6::Sql whalepet_core
)

# ---------------------------------------------------------------------------
# Platform 静态库（P7 净增）：桌面环境感知的**接口**与实现
#   —— 只依赖 Core/Qt6::Core，不依赖 Widgets，可在 headless 下单测
#   —— P7.1：Win32 真实采集（前台窗口 / 进程名 / 空闲 / 键鼠计数 / 会话状态）
#      + Win32 文本工具（宽字符 → UTF-8 / 路径取文件名，纯函数、可脱系统单测）
# ---------------------------------------------------------------------------
qt_add_library(whalepet_platform STATIC
    src/platform/DesktopObserver.h
    src/platform/DesktopObserver.cpp
    src/platform/EmptyDesktopObserver.h
    src/platform/EmptyDesktopObserver.cpp
    src/platform/Win32TextUtil.h
    src/platform/Win32TextUtil.cpp
)
target_include_directories(whalepet_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(whalepet_platform PUBLIC Qt6::Core whalepet_core)

# Win32 采集实现只用 Windows 平台编译（windows.h）；
# 需要 user32：GetForegroundWindow / GetLastInputInfo / SystemParametersInfoW /
#              OpenInputDesktop / SetWindowsHookExW 均在其中。
if(WIN32)
    target_sources(whalepet_platform PRIVATE
        src/platform/Win32DesktopObserver.h
        src/platform/Win32DesktopObserver.cpp
    )
    target_link_libraries(whalepet_platform PUBLIC user32)
endif()

# ---------------------------------------------------------------------------
# GameState 静态库（EX1 净增）：游戏状态的**只读**读取底座
#   —— profile 模型/加载器 + 只读进程内存读取器 + 有界指针链解析 + 引擎适配器
#   —— 只依赖 Qt6::Core（JSON/字符串）+ whalepet_core，**不依赖 Widgets**
#   —— 【红线】仅申请读权限（PROCESS_VM_READ）；不提供任何写入能力（见 docs/ROADMAP-ex1.md §4.1）
# ---------------------------------------------------------------------------
qt_add_library(whalepet_gamestate STATIC
    src/gamestate/GameProfile.h
    src/gamestate/GameProfile.cpp
    src/gamestate/IGameMemoryReader.h
    src/gamestate/Win32GameMemoryReader.h
    src/gamestate/Win32GameMemoryReader.cpp
    src/gamestate/PointerChainResolver.h
    src/gamestate/PointerChainResolver.cpp
    src/gamestate/ChainSampler.h
    src/gamestate/ChainSampler.cpp
    src/gamestate/IGameStateAdapter.h
    src/gamestate/GenericChainAdapter.h
    src/gamestate/GenericChainAdapter.cpp
    src/gamestate/GameStateAdapterFactory.cpp
    # EX1.2：Unity 后端判定 + Mono/IL2CPP 适配器 + dump.cs 离线转换
    src/gamestate/UnityRuntime.h
    src/gamestate/UnityRuntime.cpp
    src/gamestate/UnityAdapterBase.h
    src/gamestate/UnityAdapterBase.cpp
    src/gamestate/UnityMonoAdapter.h
    src/gamestate/UnityMonoAdapter.cpp
    src/gamestate/UnityIl2CppAdapter.h
    src/gamestate/UnityIl2CppAdapter.cpp
    src/gamestate/UnityDumpConverter.h
    src/gamestate/UnityDumpConverter.cpp
    # EX1.3：RPG Maker —— 桥接（RGSS 主路径）+ MV/MZ CDP 只读求值 + 特殊场景检测
    src/gamestate/RpgMakerSpecialScene.h
    src/gamestate/RpgMakerSpecialScene.cpp
    src/gamestate/CdpWebSocketClient.h
    src/gamestate/CdpWebSocketClient.cpp
    src/gamestate/RpgMakerBridgeAdapter.h
    src/gamestate/RpgMakerBridgeAdapter.cpp
    src/gamestate/RpgMakerCdpAdapter.h
    src/gamestate/RpgMakerCdpAdapter.cpp
)
target_include_directories(whalepet_gamestate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# EX1.3 起需要 Qt6::Network（CDP 的 http /json 发现 + 回环桥接）与 Qt6::WebSockets（CDP）。
target_link_libraries(whalepet_gamestate PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets whalepet_core)

# ---------------------------------------------------------------------------
# Plugin 静态库（P7 净增）：通用能力总线（三层插件共用）
#   —— 能力协议 / 插件注册表 / 内置层装载器 / DLL 与外部进程装载器
#   —— 只依赖 Qt6::Core（JSON 序列化）+ whalepet_core，**不依赖 Widgets**
#   —— 不依赖 view / model：PluginContext 只持有前向声明的指针（见 PluginInterface.h）
# ---------------------------------------------------------------------------
qt_add_library(whalepet_plugin STATIC
    src/plugin/Capability.h
    src/plugin/Capability.cpp
    src/plugin/PluginInterface.h
    src/plugin/PluginRegistry.h
    src/plugin/PluginRegistry.cpp
    src/plugin/builtin/BuiltinPluginLoader.h
    src/plugin/builtin/BuiltinPluginLoader.cpp
    src/plugin/dll/IPluginFactory.h
    src/plugin/dll/DllPluginLoader.h
    src/plugin/dll/DllPluginLoader.cpp
    # P7.4：外部进程插件（MCP Client）——配置规格 / stdio 客户端 / 单进程会话 / 编排
    src/plugin/process/ProcessServerSpec.h
    src/plugin/process/McpStdioClient.h
    src/plugin/process/McpStdioClient.cpp
    src/plugin/process/McpPluginSession.h
    src/plugin/process/McpPluginSession.cpp
    src/plugin/process/ProcessPluginLoader.h
    src/plugin/process/ProcessPluginLoader.cpp
)
target_include_directories(whalepet_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(whalepet_plugin PUBLIC Qt6::Core whalepet_core)

# ---------------------------------------------------------------------------
# ContextApi 静态库（P7 净增）：本地 Context API（对外接口）
#   —— JSON-RPC 分发核心 + 双通道（MCP stdio / 本机回环 HTTP）+ ACP 预留接口
#   —— 通过 contextapi::IContextProvider 取数据，**不反向依赖 view**（避免循环依赖）
# ---------------------------------------------------------------------------
qt_add_library(whalepet_contextapi STATIC
    src/contextapi/IContextProvider.h
    src/contextapi/ContextSnapshot.h
    src/contextapi/ContextSnapshot.cpp
    src/contextapi/JsonRpcDispatcher.h
    src/contextapi/JsonRpcDispatcher.cpp
    src/contextapi/ContextApiService.h
    src/contextapi/ContextApiService.cpp
    src/contextapi/builtin/ContextCapabilities.h
    src/contextapi/builtin/ContextCapabilities.cpp
    src/contextapi/transport/StdioTransport.h
    src/contextapi/transport/StdioTransport.cpp
    src/contextapi/transport/LocalHttpTransport.h
    src/contextapi/transport/LocalHttpTransport.cpp
    # P7.2：本地命名管道通道（QLocalServer；每连接复用 StdioTransport，供 whalepet-mcp.exe 连接）
    src/contextapi/transport/LocalPipeTransport.h
    src/contextapi/transport/LocalPipeTransport.cpp
    src/contextapi/ISignalSource.h
    src/contextapi/IAgentBridge.h
    # P7.5：ACP 预留接口的具体实现（显式信号源 / Agent 会话桥接 / 信号→工作态映射）
    src/contextapi/acp/AcpSignalSource.h
    src/contextapi/acp/AcpSignalSource.cpp
    src/contextapi/acp/AcpAgentBridge.h
    src/contextapi/acp/AcpAgentBridge.cpp
    src/contextapi/acp/AcpSignalRules.h
    src/contextapi/acp/AcpSignalRules.cpp
    # P7.6：ACP（Agent Client Protocol）session/update → CoreSignal 纯映射
    src/contextapi/acp/AcpEventMapper.h
    src/contextapi/acp/AcpEventMapper.cpp
    # P7.6：ACP 客户端（NDJSON over stdio + 子进程生命周期 + 会话方法）
    src/contextapi/acp/AcpClient.h
    src/contextapi/acp/AcpClient.cpp
)
target_include_directories(whalepet_contextapi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(whalepet_contextapi
    PUBLIC Qt6::Core Qt6::Network whalepet_core whalepet_plugin
)

# ---------------------------------------------------------------------------
# 视图静态库（供 WhalePet 与测试复用）
# ---------------------------------------------------------------------------
qt_add_library(whalepet_view STATIC
    src/common/PetVisuals.h
    src/common/UiPalette.h
    src/view/AssetsResource.h
    src/view/AssetsResource.cpp
    src/view/PoseImageLoader.h
    src/view/PoseImageLoader.cpp
    src/view/PoseLibrary.h
    src/view/PoseLibrary.cpp
    src/view/PoseView.h
    src/view/PoseView.cpp
    src/view/SpeechBubble.h
    src/view/SpeechBubble.cpp
    src/view/PetWindow.h
    src/view/PetWindow.cpp
    src/view/StatusPanel.h
    src/view/StatusPanel.cpp
    src/view/ContentPanel.h
    src/view/ContentPanel.cpp
    src/view/SettingsDialog.h
    src/view/SettingsDialog.cpp
    src/minigame/MiniGamePlugin.h
    src/minigame/MiniGameRegistry.h
    src/minigame/MiniGameRegistry.cpp
    # P7：小游戏插件 → 通用能力总线的兼容适配（MiniGameRegistry 本身零改动，见 §7）
    src/minigame/MiniGameCompatAdapter.h
    src/minigame/MiniGameCompatAdapter.cpp
    src/minigame/minesweeper/MinesweeperPlugin.h
    src/minigame/minesweeper/MinesweeperPlugin.cpp
    src/minigame/minesweeper/MinesweeperView.h
    src/minigame/minesweeper/MinesweeperView.cpp
    src/minigame/kitten/KittenPlugin.h
    src/minigame/kitten/KittenPlugin.cpp
    src/minigame/kitten/KittenView.h
    src/minigame/kitten/KittenView.cpp
    # 小游戏：国际象棋（外部 UCI 引擎经 QProcess 驱动，UI 不阻塞）
    src/minigame/chess/ChessPlugin.h
    src/minigame/chess/ChessPlugin.cpp
    src/minigame/chess/ChessView.h
    src/minigame/chess/ChessView.cpp
    src/minigame/chess/UciEngine.h
    src/minigame/chess/UciEngine.cpp
    src/view/GlobalHotkey.h
    src/view/GlobalHotkey.cpp
    src/view/HotwordDialog.h
    src/view/HotwordDialog.cpp
    # P8：预设对话提问面板（无边框工具窗口，问题 + 1~3 个回答按钮）
    src/view/DialoguePanel.h
    src/view/DialoguePanel.cpp
    src/viewmodel/PosePresenter.h
    src/viewmodel/PosePresenter.cpp
    src/viewmodel/PetController.h
    src/viewmodel/PetController.cpp
    src/viewmodel/ChatService.h
    src/viewmodel/ChatService.cpp
    src/viewmodel/GrowthService.h
    src/viewmodel/GrowthService.cpp
    src/viewmodel/AchievementService.h
    src/viewmodel/AchievementService.cpp
    src/viewmodel/MiniGameService.h
    src/viewmodel/MiniGameService.cpp
    src/viewmodel/QuestService.h
    src/viewmodel/QuestService.cpp
    src/viewmodel/SigninService.h
    src/viewmodel/SigninService.cpp
    src/viewmodel/StomachService.h
    src/viewmodel/StomachService.cpp
    # P7：感知采样调度 / 工作状态判定编排 / Context 数据提供者
    src/viewmodel/EnvironmentService.h
    src/viewmodel/EnvironmentService.cpp
    src/viewmodel/WorkStateService.h
    src/viewmodel/WorkStateService.cpp
    src/viewmodel/PetContextProvider.h
    src/viewmodel/PetContextProvider.cpp
    # P7.5：ACP 显式信号编排（轮询信号源 → 映射 → 覆盖性工作态）
    src/viewmodel/AcpSignalService.h
    src/viewmodel/AcpSignalService.cpp
    # EX1.4：游戏陪玩采样调度 / 判定编排 / 上报
    src/viewmodel/GameCompanionService.h
    src/viewmodel/GameCompanionService.cpp
    # P8：预设对话编排（问题池刷新 / 三选一回答 / 独立立绘池）与彩云天气接入
    src/viewmodel/DialogueService.h
    src/viewmodel/DialogueService.cpp
    src/viewmodel/WeatherService.h
    src/viewmodel/WeatherService.cpp
    # EX 彩蛋（experiment/easter-egg1）：工作区扫描 + 5% 触发 + 幂等原子写
    src/viewmodel/EasterEggService.h
    src/viewmodel/EasterEggService.cpp
    assets/assets.qrc
    resources/qt-ui/qt-ui.qrc
)
target_include_directories(whalepet_view PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# whalepet_core 用 PUBLIC：viewmodel 头文件（如 PetController.h）公开包含 core 头，
# 消费方（WhalePet / test_smoke）需要同样的 include 路径。
# whalepet_model 用 PUBLIC：PetWindow 持有 Database/GrowthService，消费方需要其 include 与 Qt6::Sql。
# P7：platform（感知）/ plugin（能力总线）/ contextapi（对外接口）同样 PUBLIC ——
#     测试目标需要直接构造这些类型的对象。
target_link_libraries(whalepet_view
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets whalepet_core whalepet_model
           whalepet_platform whalepet_plugin whalepet_contextapi
           # EX1.4：viewmodel::GameCompanionService 直接持有 gamestate 适配器（PUBLIC：
           # 测试目标需同期可见 gamestate 头，且 Qt6::Network/WebSockets 随库传递）。
           whalepet_gamestate
)
# GlobalHotkey 用 RegisterHotKey / UnregisterHotKey（P6），需显式链接 user32
if(WIN32)
    target_link_libraries(whalepet_view PRIVATE user32)
endif()
