# ---------------------------------------------------------------------------
# 模块：OutputLayout —— 产物落盘目录与发布模式开关
# 职责：集中声明可执行目标的输出/部署目录（CACHE 变量）与发布模式选项，
#       供 Executables 模块设置 RUNTIME/PDB 输出目录时消费。
#   WHALEPET_DEPLOY_DIR  开发部署目录（含 PDB，供崩溃分析）
#   WHALEPET_DIST_DIR    正式发布目录（免安装版内容，不含调试符号与用户数据）
#   WHALEPET_PACKAGE     发布模式开关（-DWHALEPET_PACKAGE=ON）
# 依赖：顶层 CMakeLists.txt 已直接调用 project()。
# 说明：由顶层通过 include() 引入，与顶层共享同一变量作用域；
#       本模块只声明变量与选项，不定义 / 修改任何 target。
# ---------------------------------------------------------------------------

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
