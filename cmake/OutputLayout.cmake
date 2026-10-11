# ---------------------------------------------------------------------------
# 模块：OutputLayout —— 产物落盘目录约定（Release 产物目录）
# 职责：集中声明可执行目标的 Release 产物落点（CACHE 变量），
#       供 Executables 模块设置 RUNTIME/PDB 输出目录时消费。
#   WHALEPET_RELEASE_DIR  Release 产物目录（构建目录内，含 PDB，供崩溃分析）
# 依赖：顶层 CMakeLists.txt 已直接调用 project()。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域；
#       本模块只声明变量，不定义 / 修改任何 target。
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Release 产物目录（2026-10-11 收敛，见 docs/BUILD.md §10）：
#   **固定落在构建目录内**——build/Release/，与 Debug 的 build/Debug/ 对称；
#   不再使用工作区根下的 deploy-release/ 这类平行产物目录（已删除）。
#   同时取消「打包专用构建目录 + WHALEPET_PACKAGE 开关」双轨：发布脚本
#   scripts/package-release.ps1 **固定复用 build/**，从 build/Release/ 暂存，
#   并在暂存阶段清除调试符号（*.pdb / *.ilk / *.exp / *.lib）。
#   保留 `/Zi` + `/DEBUG`（见 Executables 模块）：Release 版 PDB 与 exe 同目录，
#   用户侧崩溃分析统一用 Qt Creator 打开 Release 产物
#   （docs/README.md §六、docs/BUILD.md §9）。
# ---------------------------------------------------------------------------
set(WHALEPET_RELEASE_DIR "${CMAKE_BINARY_DIR}/Release"
    CACHE PATH "Release 产物目录（构建目录内，含 PDB，供崩溃分析）")
