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

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileOptions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QtDependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Libraries.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OutputLayout.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Executables.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginExamples.cmake)
