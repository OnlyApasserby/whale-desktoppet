cmake_minimum_required(VERSION 3.21)

project(WhalePet
    VERSION 0.2.0
    DESCRIPTION "WhalePet desktop pet (Qt Widgets)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /MP)
endif()

# P7：新增 Qt6::Network（QTcpServer / QLocalServer）用于 Context API 的**本机回环**通道。
# 依赖口径见 docs/README.md §5.1：零第三方依赖，允许 Qt 官方模块。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Test)

qt_standard_project_setup()

# qt_standard_project_setup() 只开启 AUTOMOC/AUTOUIC，**不开启 AUTORCC**。
# 若不显式开启，加入目标的 .qrc 会被当作未知源文件忽略，资源不会编译进产物，
# 运行期资源路径全部失效（链接期表现为 qInitResources_* 未解析）。见 docs/traps-P1.md。
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# 构建期确认 WebP 图像插件（见 docs/BUILD.md §6）
# 缺失时必须显式报错，禁止静默失败。
# ---------------------------------------------------------------------------
set(_qt_prefix "${Qt6_DIR}/../../..")
get_filename_component(_qt_prefix "${_qt_prefix}" ABSOLUTE)
set(_imgfmt_dir "${_qt_prefix}/plugins/imageformats")
if(EXISTS "${_imgfmt_dir}/qwebp.dll" OR EXISTS "${_imgfmt_dir}/qwebpd.dll")
    message(STATUS "WebP image plugin found: ${_imgfmt_dir}")
else()
    message(FATAL_ERROR
        "未找到 WebP 图像插件（qwebp.dll / qwebpd.dll）。\n"
        "预期目录: ${_imgfmt_dir}\n"
        "webp 立绘将无法加载。请确认 Qt 安装包含 imageformats/qwebp，"
        "并在部署时使用 windeployqt 拷贝 plugins/imageformats。")
endif()

# ---------------------------------------------------------------------------
# 构建期确认 SQLite 驱动插件（见 docs/DATA-MODEL.md §1、docs/ROADMAP-P3.md）
# P3 起 Database 依赖 QSQLITE；插件缺失会让程序静默降级到内存库（数据不落盘），
# 属于「能跑但错」，必须在构建期就暴露。
# ---------------------------------------------------------------------------
set(_sqldrivers_dir "${_qt_prefix}/plugins/sqldrivers")
if(EXISTS "${_sqldrivers_dir}/qsqlite.dll" OR EXISTS "${_sqldrivers_dir}/qsqlited.dll")
    message(STATUS "SQLite driver plugin found: ${_sqldrivers_dir}")
else()
    message(FATAL_ERROR
        "未找到 SQLite 驱动插件（qsqlite.dll / qsqlited.dll）。\n"
        "预期目录: ${_sqldrivers_dir}\n"
        "养成数据将无法落盘（只会降级到内存库）。请确认 Qt 安装包含 sqldrivers/qsqlite，"
        "并在部署时使用 windeployqt 拷贝 plugins/sqldrivers。")
endif()

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
)
# GlobalHotkey 用 RegisterHotKey / UnregisterHotKey（P6），需显式链接 user32
if(WIN32)
    target_link_libraries(whalepet_view PRIVATE user32)
endif()

# ---------------------------------------------------------------------------
# 主程序
# ---------------------------------------------------------------------------
qt_add_executable(WhalePet WIN32
    src/app/main.cpp
)
target_link_libraries(WhalePet PRIVATE whalepet_view)

# Windows 可执行文件图标（资源管理器 / 任务栏 / 开始菜单快捷方式）。
# 用 CMake 生成 .rc 并写入图标的**绝对路径**：rc.exe 对 .rc 内相对路径的解析
# 取决于调用时的工作目录，绝对路径可避免「静默用了默认图标」这类问题。
if(WIN32)
    set(_whalepet_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/whalepet.ico")
    if(NOT EXISTS "${_whalepet_icon}")
        message(FATAL_ERROR "未找到应用图标: ${_whalepet_icon}")
    endif()
    file(TO_CMAKE_PATH "${_whalepet_icon}" _whalepet_icon_posix)
    set(_whalepet_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/whalepet_app_icon.rc")
    # 保持纯 ASCII：rc.exe 不按 UTF-8 解析（与 packaging/*.ps1 的 ASCII 约定同源）
    file(WRITE "${_whalepet_icon_rc}"
        "// Generated by CMakeLists.txt - do not edit: Windows executable icon\n"
        "IDI_ICON1 ICON \"${_whalepet_icon_posix}\"\n")
    target_sources(WhalePet PRIVATE "${_whalepet_icon_rc}")
endif()

# ---------------------------------------------------------------------------
# Release 产物目录 = 部署目录（deploy-release/），见 docs/BUILD.md §3
#   Release 版 WhalePet.exe 直接生成在 deploy-release/，与 windeployqt 拷贝的
#   Qt 运行库同目录，省掉「先 Copy-Item 再部署」这一步（历史见 traps-P1.md），
#   也避免部署目录里的 exe 落后于构建产物。
#   Debug 仍留在 build/Debug/；RUNTIME_OUTPUT_DIRECTORY_RELEASE 是
#   「配置专属」属性，VS 多配置生成器不会再多套一层 Release/ 子目录。
# ---------------------------------------------------------------------------
set(WHALEPET_DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deploy-release"
    CACHE PATH "开发部署目录（含 PDB，供崩溃分析）")

# 正式发布目录（免安装版内容）：exe + windeployqt 运行库，**不含调试符号与用户数据**。
set(WHALEPET_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/WhalePet"
    CACHE PATH "正式发布目录（免安装版内容）")

# 发布模式（`-DWHALEPET_PACKAGE=ON`，见 docs/BUILD.md §10）：
#   Release 产物直接落在 dist/WhalePet，且**不生成调试符号**（不加 /Zi /DEBUG，
#   CMake 对 Release 默认不产出 PDB）——面向分发的干净产物。
# 默认 OFF：deploy-release/ 保持带 PDB 的开发约定（docs/README.md §六 崩溃分析）。
option(WHALEPET_PACKAGE "构建正式发布版（无调试符号，产物在 dist/WhalePet）" OFF)

if(WHALEPET_PACKAGE)
    set_target_properties(WhalePet PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WHALEPET_DIST_DIR}"
    )
else()
    # Release 版同时产出调试符号（PDB 落在部署目录）：
    #   用户侧的崩溃分析统一用 Qt Creator 打开 Release 产物（见 docs/README.md §六），
    #   没有同目录 PDB 就只能看到地址。`/Zi` 只增加调试信息，**不改变 /O2 优化**，
    #   也不涉及 debug/release 运行库混用（与 `TRAP-P2-008` 的混用问题无关）。
    set_target_properties(WhalePet PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WHALEPET_DEPLOY_DIR}"
        PDB_OUTPUT_DIRECTORY_RELEASE "${WHALEPET_DEPLOY_DIR}"
    )
    target_compile_options(WhalePet PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(WhalePet PRIVATE $<$<CONFIG:Release>:/DEBUG>)
endif()

# ---------------------------------------------------------------------------
# P7.2：MCP stdio 桥接进程（控制台）
#   —— 主程序是 WIN32 GUI 子系统，没有可用的 stdin/stdout，无法直接当 MCP stdio
#      Server；本进程把 MCP 客户端的 stdin/stdout 字节转发到主程序的命名管道
#     （LocalPipeTransport），自身不含业务逻辑。见 docs/CONTEXT-API.md §4、
#     docs/packages.md §2.1。
#   —— 控制台子系统（**故意不加 WIN32**）：必须保留可用的 stdin/stdout。
#   —— 与 WhalePet.exe 同目录产出，随包分发（安装/卸载对应关系见 docs/packages.md §5）。
# ---------------------------------------------------------------------------
qt_add_executable(whalepet-mcp
    src/app/mcp_bridge_main.cpp
)
# 复用 kDefaultContextPipeName（管道名唯一约定源）与 contextapi 的 include 路径
target_link_libraries(whalepet-mcp PRIVATE whalepet_contextapi)

if(WHALEPET_PACKAGE)
    set_target_properties(whalepet-mcp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WHALEPET_DIST_DIR}"
    )
else()
    set_target_properties(whalepet-mcp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WHALEPET_DEPLOY_DIR}"
        PDB_OUTPUT_DIRECTORY_RELEASE "${WHALEPET_DEPLOY_DIR}"
    )
    target_compile_options(whalepet-mcp PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(whalepet-mcp PRIVATE $<$<CONFIG:Release>:/DEBUG>)
endif()

# ---------------------------------------------------------------------------
# 测试（Linux 无显示 / CI 无桌面时返回 77 跳过，见 docs/TESTING.md）
# ---------------------------------------------------------------------------
enable_testing()

qt_add_executable(test_smoke tests/test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE whalepet_view Qt6::Test)
# 随包地图目录：校验「场景切换后网格与判定数据一致」（见 kittenSceneChangeRebuildsGrid）
target_compile_definitions(test_smoke PRIVATE
    "WHALEPET_MAPS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/maps\"")
# `-o -,txt` 强制把 QTest 日志写到 stdout：
# Windows 上 QTest 默认日志器在「无控制台」时改走 OutputDebugString，
# 导致 CTest / 管道 / 文件重定向都拿不到用例结果。见 docs/traps-P2.md。
add_test(NAME test_smoke COMMAND test_smoke -o -,txt)
set_tests_properties(test_smoke PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

qt_add_executable(test_state_machine tests/test_state_machine.cpp)
target_link_libraries(test_state_machine PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_state_machine COMMAND test_state_machine -o -,txt)
set_tests_properties(test_state_machine PROPERTIES TIMEOUT 60)

# 台词表：解析 + 取用 + 「真实语料覆盖状态机输出场景」的一致性校验
qt_add_executable(test_line_table tests/test_line_table.cpp)
target_link_libraries(test_line_table PRIVATE whalepet_core Qt6::Test)
target_compile_definitions(test_line_table PRIVATE
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_line_table COMMAND test_line_table -o -,txt)
set_tests_properties(test_line_table PROPERTIES TIMEOUT 60)

# 数据层：建表 / 迁移幂等 / 单例往返 / 事务回滚 / 目录三级降级
qt_add_executable(test_database tests/test_database.cpp)
target_link_libraries(test_database PRIVATE whalepet_model Qt6::Test)
add_test(NAME test_database COMMAND test_database -o -,txt)
set_tests_properties(test_database PROPERTIES TIMEOUT 60)

# 养成：升级曲线 / 增量表 / 夹取 / 饱食衰减 / 跨天签到 / 升级信号 / 持久化
qt_add_executable(test_growth tests/test_growth.cpp)
target_link_libraries(test_growth PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_growth COMMAND test_growth -o -,txt)
set_tests_properties(test_growth PROPERTIES TIMEOUT 60)

# P4 内容层：39 项成就判定 / 每日任务抽签与领取 / 周签到里程碑 / 日记去重与上限
qt_add_executable(test_content tests/test_content.cpp)
target_link_libraries(test_content PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_content COMMAND test_content -o -,txt)
set_tests_properties(test_content PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# P5 聊天：分时问候 / 深夜静默 / 心情分层 / 羁绊专属 / 关键词映射与开关 / 语料覆盖
qt_add_executable(test_chat tests/test_chat.cpp)
target_link_libraries(test_chat PRIVATE whalepet_view Qt6::Test)
target_compile_definitions(test_chat PRIVATE
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_chat COMMAND test_chat -o -,txt)
set_tests_properties(test_chat PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# P6 热词：自定义热词优先匹配 / 归一化与去重 / 热词表 CRUD / 显式录入不受开关门控
# 链 whalepet_view：热词匹配既走 core::matchKeyword，也走 viewmodel::ChatService
# （显式录入 vs 被动监听），同 test_chat。
qt_add_executable(test_hotword tests/test_hotword.cpp)
target_link_libraries(test_hotword PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_hotword COMMAND test_hotword -o -,txt)
set_tests_properties(test_hotword PROPERTIES TIMEOUT 60)

# P6 设置项「生效」：深夜静默开关 / 立绘尺寸与粒子·惯性开关 / 气泡开关
qt_add_executable(test_settings tests/test_settings.cpp)
target_link_libraries(test_settings PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_settings COMMAND test_settings -o -,txt)
set_tests_properties(test_settings PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# 小游戏：扫雷纯逻辑（预设与自定义校验 / 首点安全布雷 / 翻格·插旗 / 胜负与连翻）
qt_add_executable(test_minesweeper tests/test_minesweeper.cpp)
target_link_libraries(test_minesweeper PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_minesweeper COMMAND test_minesweeper -o -,txt)
set_tests_properties(test_minesweeper PROPERTIES TIMEOUT 60)

# 小游戏：鲸鱼娘找小猫纯逻辑（物体表与地图解析 / 移动与撞墙 / 物体交互与消费 / 场景切换 /
# 通关判定与结算折算）
qt_add_executable(test_kitten tests/test_kitten.cpp)
target_link_libraries(test_kitten PRIVATE whalepet_core Qt6::Test)
# 随包资源目录：校验真实地图可达性与台词覆盖（与运行期 qrc 同源文件）
target_compile_definitions(test_kitten PRIVATE
    "WHALEPET_MAPS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/maps\""
    "WHALEPET_LINES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines\"")
add_test(NAME test_kitten COMMAND test_kitten -o -,txt)
set_tests_properties(test_kitten PROPERTIES TIMEOUT 60)

# 小游戏：国际象棋纯逻辑（FEN 往返 / 合法着法 / 王车易位 / 过路兵 / 升变 / 将军将死逼和 /
# UCI 着法串 / 结算折算与难度表）
qt_add_executable(test_chess tests/test_chess.cpp)
target_link_libraries(test_chess PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_chess COMMAND test_chess -o -,txt)
set_tests_properties(test_chess PROPERTIES TIMEOUT 60)

# 小游戏结算：档位奖励 / 每日上限（每日 3 局）/ 个人最快与跨天清零 / 落库往返
qt_add_executable(test_minigame tests/test_minigame.cpp)
target_link_libraries(test_minigame PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_minigame COMMAND test_minigame -o -,txt)
set_tests_properties(test_minigame PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P7：插件化智能桌宠与本地 Context API（docs/ROADMAP-P7-Fin.md P7.0）
# ---------------------------------------------------------------------------

# 能力总线：插件注册 / 能力收集 / 冲突仲裁（Builtin > Dll > Process）/ 同步与异步调用契约 /
# 生命周期容错 / 内置层装载器 / 小游戏兼容适配（minigame.* 能力）
qt_add_executable(test_plugin_registry tests/test_plugin_registry.cpp)
target_link_libraries(test_plugin_registry PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_plugin_registry COMMAND test_plugin_registry -o -,txt)
set_tests_properties(test_plugin_registry PROPERTIES TIMEOUT 60)

# 感知层骨架：空实现恒「无数据」/ 组合观察者的类别·切换·停留·滚动窗口 / 失败不伪造数据
qt_add_executable(test_platform_skeleton tests/test_platform_skeleton.cpp)
target_link_libraries(test_platform_skeleton PRIVATE whalepet_platform Qt6::Test)
add_test(NAME test_platform_skeleton COMMAND test_platform_skeleton -o -,txt)
set_tests_properties(test_platform_skeleton PROPERTIES TIMEOUT 60)

# P7.1 真实 Win32 感知：文本工具（宽字符→UTF-8 / 路径取文件名）、三个子采样器的
# 「注入替身读数」行为（含钩子不可用时的差分降级、失败不伪造）、会话锁定→Afk 端到端。
# 全部用注入替身驱动，**不安装任何系统钩子**、不依赖真实前台窗口（可在 CI/headless 下稳定运行）。
if(WIN32)
    qt_add_executable(test_win32_observer tests/test_win32_observer.cpp)
    target_link_libraries(test_win32_observer PRIVATE whalepet_platform Qt6::Test)
    add_test(NAME test_win32_observer COMMAND test_win32_observer -o -,txt)
    set_tests_properties(test_win32_observer PROPERTIES TIMEOUT 60)
endif()

# 工作状态：应用类别归一化 / 各状态判据（含 Coding vs Vibe Coding）/ 置信度与滞回 /
# 状态机工作态通道（专注态静默与 work.* 豁免、不打断一次性表现、Unknown 零回归）
qt_add_executable(test_work_state tests/test_work_state.cpp)
target_link_libraries(test_work_state PRIVATE whalepet_core Qt6::Test)
add_test(NAME test_work_state COMMAND test_work_state -o -,txt)
set_tests_properties(test_work_state PROPERTIES TIMEOUT 60)

# Context API：JSON-RPC 校验与错误码 / 能力别名路由 / 门控与回环绑定 /
# 双通道（本机 HTTP + MCP stdio）共用同一 dispatcher 与能力表
qt_add_executable(test_context_dispatch tests/test_context_dispatch.cpp)
target_link_libraries(test_context_dispatch PRIVATE whalepet_contextapi Qt6::Test)
add_test(NAME test_context_dispatch COMMAND test_context_dispatch -o -,txt)
set_tests_properties(test_context_dispatch PROPERTIES TIMEOUT 120)

# P7.2 通道启用与 MCP 桥接：命名管道承载完整 MCP 会话 / token 门控 / 总开关同时启停两通道 /
# 以及**真实桥接进程 whalepet-mcp.exe 端到端**（stdio ↔ 命名管道 ↔ 宿主，含 --token 注入）
qt_add_executable(test_context_pipe tests/test_context_pipe.cpp)
target_link_libraries(test_context_pipe PRIVATE whalepet_contextapi Qt6::Test)
target_compile_definitions(test_context_pipe PRIVATE
    "WHALEPET_MCP_EXE=\"$<TARGET_FILE:whalepet-mcp>\"")
add_dependencies(test_context_pipe whalepet-mcp)
add_test(NAME test_context_pipe COMMAND test_context_pipe -o -,txt)
set_tests_properties(test_context_pipe PROPERTIES TIMEOUT 120)

# P7.5 ACP / IDE 显式信号：JSONL 信号源增量读取 / 非法行忽略 / 未换行尾部 / 截断重置；
# Agent 会话桥接（生命周期幂等 + 事件落盘）；信号→工作态映射；显式信号覆盖推断与过期回落
qt_add_executable(test_acp tests/test_acp.cpp)
target_link_libraries(test_acp PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_acp COMMAND test_acp -o -,txt)
set_tests_properties(test_acp PROPERTIES TIMEOUT 60)

# P7.6 ACP 事件映射：以**真实 dsh 报文**夹具（`dsh --profile acp` 实测采集）驱动
# AcpEventMapper（ACP session/update → CoreSignal）与 AcpSignalRules（→ 工作态）
qt_add_executable(test_acp_event_mapper tests/test_acp_event_mapper.cpp)
target_link_libraries(test_acp_event_mapper PRIVATE whalepet_contextapi Qt6::Test)
target_compile_definitions(test_acp_event_mapper PRIVATE
    "WHALEPET_ACP_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/acp-real-events.json\"")
add_test(NAME test_acp_event_mapper COMMAND test_acp_event_mapper -o -,txt)
set_tests_properties(test_acp_event_mapper PROPERTIES TIMEOUT 60)

# P7.6 ACP 客户端：以一个真实子进程（acp_test_agent）端到端验证
# 启动 + initialize 握手 / session/new / session/list + session/resume /
# session/prompt 事件映射 / 权限自动应答 / Agent 崩溃隔离。
# `realDshSmokeOrSkip` 在设置环境变量 WHALEPET_ACP_REAL_DSH=<dsh>/lib/bin.js 时
# 会用**真实 DeepSeek Harness**（`dsh --profile acp`）跑一遍，否则跳过（CI 友好）。
qt_add_executable(acp_test_agent tests/acp_test_agent.cpp)
target_link_libraries(acp_test_agent PRIVATE Qt6::Core)

qt_add_executable(test_acp_client tests/test_acp_client.cpp)
target_link_libraries(test_acp_client PRIVATE whalepet_contextapi Qt6::Test)
add_dependencies(test_acp_client acp_test_agent)
add_test(NAME test_acp_client COMMAND test_acp_client -o -,txt)
set_tests_properties(test_acp_client PROPERTIES TIMEOUT 120)

# P7.4 外部进程插件（MCP Client）：以一个真实子进程（mcp_test_server）端到端验证
# 配置校验 / initialize 握手 + tools/list 能力发现 / tools/call 异步转发 /
# 工具错误透传 / 调用超时 / 子进程崩溃隔离（pending 回投 + 能力标记不可用）
qt_add_executable(mcp_test_server tests/mcp_test_server.cpp)
target_link_libraries(mcp_test_server PRIVATE Qt6::Core)

qt_add_executable(test_process_plugin tests/test_process_plugin.cpp)
target_link_libraries(test_process_plugin PRIVATE whalepet_plugin Qt6::Test)
add_dependencies(test_process_plugin mcp_test_server)
add_test(NAME test_process_plugin COMMAND test_process_plugin -o -,txt)
set_tests_properties(test_process_plugin PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P7.3 动态插件（DLL）示例与负例
#   —— **仅供构建与自动化测试，不随安装包分发**（安装期**不**创建 plugins/ 目录；
#      用户自行创建 <安装目录>/plugins 并放入 DLL 即生效；见 docs/packages.md §8）。
#   ext_hello  : 合法插件（元数据 apiVersion=1），注册 ext.hello.greet
#   ext_badabi : 元数据 apiVersion=99 的不兼容负例
#   WIN32 专属：DllPluginLoader 按 `*.dll` 扫描，插件目标亦为 Windows 共享库。
# ---------------------------------------------------------------------------
if(WIN32)
    qt_add_library(whalepet_ext_hello MODULE
        src/plugin/examples/hello/HelloPlugin.h
        src/plugin/examples/hello/HelloPlugin.cpp
    )
    target_link_libraries(whalepet_ext_hello PRIVATE whalepet_plugin)
    set_target_properties(whalepet_ext_hello PROPERTIES OUTPUT_NAME "ext_hello")

    qt_add_library(whalepet_ext_badabi MODULE
        src/plugin/examples/badabi/BadAbiPlugin.h
        src/plugin/examples/badabi/BadAbiPlugin.cpp
    )
    target_link_libraries(whalepet_ext_badabi PRIVATE whalepet_plugin)
    set_target_properties(whalepet_ext_badabi PROPERTIES OUTPUT_NAME "ext_badabi")

    # 动态插件装载器：装载 / ABI 协商（不兼容被跳过且不影响其它插件）/ 失败降级 /
    # 非插件文件与缺失目录不报错 / 能力可见且可调用
    qt_add_executable(test_dll_plugin tests/test_dll_plugin.cpp)
    target_link_libraries(test_dll_plugin PRIVATE whalepet_plugin Qt6::Test)
    target_compile_definitions(test_dll_plugin PRIVATE
        "WHALEPET_HELLO_PLUGIN=\"$<TARGET_FILE:whalepet_ext_hello>\""
        "WHALEPET_BADABI_PLUGIN=\"$<TARGET_FILE:whalepet_ext_badabi>\"")
    add_dependencies(test_dll_plugin whalepet_ext_hello whalepet_ext_badabi)
    add_test(NAME test_dll_plugin COMMAND test_dll_plugin -o -,txt)
    set_tests_properties(test_dll_plugin PROPERTIES TIMEOUT 60)
endif()
