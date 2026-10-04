# ---------------------------------------------------------------------------
# 模块：PluginExamples —— P7.3 动态插件（DLL）示例与负例
# 职责：构建仅供自动化测试使用的动态插件示例（合法 / ABI 不兼容负例）
#       及其装载器测试；这些目标**不随安装包分发**。
#   ext_hello  : 合法插件（元数据 apiVersion=1），注册 ext.hello.greet
#   ext_badabi : 元数据 apiVersion=99 的不兼容负例
#   WIN32 专属：DllPluginLoader 按 `*.dll` 扫描，插件目标亦为 Windows 共享库。
# 依赖：Libraries（whalepet_plugin）。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域。
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
