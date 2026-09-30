cmake_minimum_required(VERSION 3.21)

project(WhalePet
    VERSION 0.1.0
    DESCRIPTION "WhalePet desktop pet (Qt Widgets)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /MP)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Test)

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
    src/core/PetTypes.h
    src/core/IRandom.h
    src/core/LineTable.h
    src/core/LineTable.cpp
    src/core/PetStateMachine.h
    src/core/PetStateMachine.cpp
    src/core/GrowthRules.h
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
)
target_include_directories(whalepet_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(whalepet_model
    PUBLIC Qt6::Core Qt6::Sql whalepet_core
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
    src/viewmodel/PosePresenter.h
    src/viewmodel/PosePresenter.cpp
    src/viewmodel/PetController.h
    src/viewmodel/PetController.cpp
    src/viewmodel/GrowthService.h
    src/viewmodel/GrowthService.cpp
    assets/assets.qrc
    resources/qt-ui/qt-ui.qrc
)
target_include_directories(whalepet_view PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# whalepet_core 用 PUBLIC：viewmodel 头文件（如 PetController.h）公开包含 core 头，
# 消费方（WhalePet / test_smoke）需要同样的 include 路径。
# whalepet_model 用 PUBLIC：PetWindow 持有 Database/GrowthService，消费方需要其 include 与 Qt6::Sql。
target_link_libraries(whalepet_view
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets whalepet_core whalepet_model
)

# ---------------------------------------------------------------------------
# 主程序
# ---------------------------------------------------------------------------
qt_add_executable(WhalePet WIN32
    src/app/main.cpp
)
target_link_libraries(WhalePet PRIVATE whalepet_view)

# ---------------------------------------------------------------------------
# 测试（Linux 无显示 / CI 无桌面时返回 77 跳过，见 docs/TESTING.md）
# ---------------------------------------------------------------------------
enable_testing()

qt_add_executable(test_smoke tests/test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE whalepet_view Qt6::Test)
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
    "WHALEPET_LINES_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/assets/lines/lines.txt\"")
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
