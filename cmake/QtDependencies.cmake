# ---------------------------------------------------------------------------
# 模块：QtDependencies —— Qt6 依赖查找与运行期插件构建期校验
# 职责：
#   - find_package(Qt6 ...) 查找所需 Qt 官方模块
#   - qt_standard_project_setup() + 显式开启 AUTORCC
#   - 构建期校验 WebP 图像插件、SQLite 驱动插件是否存在（缺失即 FATAL_ERROR）
# 依赖：顶层 CMakeLists.txt 已直接调用 project()（工程语言为 CXX）。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域。
#       本模块仅产出“构建期校验”结果与全局变量，不定义任何 target。
# ---------------------------------------------------------------------------

# P7：新增 Qt6::Network（QTcpServer / QLocalServer）用于 Context API 的**本机回环**通道。
# 【EX3 已移除】原 EX1.3 的 Qt6::WebSockets 随外部游戏陪玩（CDP 通道）一并移除，不再查找。
# 依赖口径见 docs/README.md §5.1：零第三方依赖，允许 Qt 官方模块。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Test)

qt_standard_project_setup()

# qt_standard_project_setup() 只开启 AUTOMOC/AUTOUIC，**不开启 AUTORCC**。
# 若不显式开启，加入目标的 .qrc 会被当作未知源文件忽略，资源不会编译进产物，
# 运行期资源路径全部失效（链接期表现为 qInitResources_* 未解析）。见 docs/pitfalls/。
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
