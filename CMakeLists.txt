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
    src/contextapi/ISignalSource.h
    src/contextapi/IAgentBridge.h
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

# 小游戏结算：档位奖励 / 每日上限（每日 3 局）/ 个人最快与跨天清零 / 落库往返
qt_add_executable(test_minigame tests/test_minigame.cpp)
target_link_libraries(test_minigame PRIVATE whalepet_view Qt6::Test)
add_test(NAME test_minigame COMMAND test_minigame -o -,txt)
set_tests_properties(test_minigame PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# P7：插件化智能桌宠与本地 Context API（docs/ROADMAP-P7.md P7.0）
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
