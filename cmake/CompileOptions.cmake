# ---------------------------------------------------------------------------
# 模块：CompileOptions —— 全局编译选项与 C++ 语言标准
# 职责：
#   - 设定 C++17 语言标准（强制、禁用编译器扩展）
#   - 设定 MSVC 全局编译选项（/utf-8 源码编码、/MP 多进程编译）
# 依赖：project() 须已由顶层 CMakeLists.txt 直接调用
#       （CMake 要求 project() 必须是顶层文件中的字面调用，故工程元信息保留在顶层）。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域；
#       本模块只影响全局编译选项，不定义 / 修改任何 target。
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /MP)
endif()
