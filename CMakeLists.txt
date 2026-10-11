cmake_minimum_required(VERSION 3.21)

# ===========================================================================
# WhalePet 顶层构建入口（编排脚本）
# ---------------------------------------------------------------------------
# 本文件仅负责工程元信息声明与按依赖顺序引入各功能模块；具体配置拆分在
# cmake/ 目录下的独立模块文件中。所有模块统一通过 include() 引入
# （**不使用 add_subdirectory**）：
#   —— include() 不创建新的变量作用域，模块与顶层共享同一作用域，
#      从而保证拆分前后的变量可见性、target 属性与编译参数完全一致；
#   —— add_subdirectory() 会引入新的作用域（变量会被隔离），因此不采用。
#
# project() 说明：CMake 要求 project() 必须是**顶层文件中的字面、直接调用**，
# 不能仅存在于被 include() 的文件中（否则会告警并注入一个占位 project），
# 因此工程元信息保留在此处；其余全局编译选项见 cmake/CompileOptions.cmake。
#
# 模块职责与引入顺序（顺序即模块依赖关系，不可随意调换）：
#   1. CompileOptions  全局编译选项与 C++ 语言标准（C++17 / MSVC /utf-8 /MP）
#   2. QtDependencies  Qt6 依赖查找、AUTORCC、WebP/SQLite 插件构建期校验
#   3. Libraries       全部静态库目标（core/model/platform/gamestate/plugin/contextapi/view）
#   4. OutputLayout    产物落盘目录与发布模式开关（供可执行目标消费）
#   5. Executables     可执行目标 WhalePet / whalepet-mcp
#   6. Tests           测试目标与 CTest 注册
#   7. PluginExamples  P7.3 动态插件示例与 DLL 装载器测试（仅构建，不分发）
#
# 模块间依赖说明：
#   - CompileOptions / QtDependencies / Libraries / OutputLayout / Executables
#     均依赖此处的 project()；
#   - Libraries 依赖 CompileOptions、QtDependencies；
#   - Executables 依赖 Libraries 与 OutputLayout；
#   - Tests 依赖 Libraries、Executables（test_context_pipe 依赖 whalepet-mcp）；
#   - PluginExamples 依赖 Libraries。
#   上述依赖由本文件按上述顺序统一编排管理。
# ===========================================================================
project(WhalePet
    VERSION 0.3.0
    DESCRIPTION "WhalePet desktop pet (Qt Widgets)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Qt Creator 兼容：Qt 前缀回落（docs/BUILD.md §2 环境基线、qt-msvc-cmake 第 2 节）
# ---------------------------------------------------------------------------
# 背景：CMakePresets.json 已为「预设方式」提供了 CMAKE_PREFIX_PATH。但 Qt Creator 也允许
# **按套件（kit）而非预设**配置本工程，此时若不手填前缀，find_package(Qt6 REQUIRED) 会直接
# 失败，Qt Creator 侧表现为「无法识别 Qt 项目 / 配置失败 / 没有 Qt 版本」。
#
# 因此这里做一次**显式且可观测**的回落（仅当调用方完全没有给出 Qt 前缀时）：
#   1) 环境变量 QT_ROOT（若已设置，尊重使用者的选择）；
#   2) 否则用本项目的固定环境基线 D:/Qt-debug。
# 说明：
#   - 使用**普通变量**而非 cache 变量：不写入 CMakeCache.txt，因此既不会覆盖调用方
#     显式传入的空值/其它前缀，也不会让后续 configure 锁死在回落值上；
#   - 命中回落时打印 STATUS，Qt Creator 的「General Messages / CMake 输出」可直接看到，
#     不做静默行为；
#   - 正常走 CMakePresets.json（含 CI 与命令行）时 CMAKE_PREFIX_PATH 已非空，本分支不生效。
if(NOT CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR)
    if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
        set(CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
        message(STATUS "未指定 Qt 前缀，回落环境变量 QT_ROOT: ${CMAKE_PREFIX_PATH}")
    else()
        set(CMAKE_PREFIX_PATH "D:/Qt-debug")
        message(STATUS "未指定 Qt 前缀，回落项目基线（docs/BUILD.md §2）: ${CMAKE_PREFIX_PATH}")
    endif()
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileOptions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QtDependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Libraries.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OutputLayout.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Executables.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginExamples.cmake)
