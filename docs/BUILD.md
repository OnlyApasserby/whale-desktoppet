# 构建基线与环境（BUILD）

> 严格遵循工作区固定基线，**不得替换版本**，**不得引入新依赖**。

## 1. 环境基线（固定）

| 组件 | 版本 | 路径 / 取值 |
|---|---|---|
| CMake | **4.4.2** | `C:\Program Files\CMake\bin\cmake.exe` |
| 生成器 | **Visual Studio 18 2026** | `-G "Visual Studio 18 2026" -A x64` |
| MSVC | VS 2026 Community | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| Qt | **6.8.4**（含 debug + release） | 前缀即 **`D:/Qt-debug`**（无 `D:/Qt-debug/6.8.4` 层） |
| Ninja | 1.12.0 | `D:\Strawberry\c\bin\ninja.exe` |
| PowerShell | **5.1** | 禁用 `??` / `?.` / `-Parallel` |

## 2. 环境初始化

```powershell
$env:QT_ROOT = 'D:/Qt-debug'
$env:VS_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$env:Path = "$env:QT_ROOT\bin;D:/Strawberry/perl/bin;D:/Strawberry/c/bin;D:\Program Files\NASM;C:\Program Files\CMake\bin;" + $env:Path
```

> ⚠️ `D:/Strawberry/c/bin` 下**另有一个旧 `cmake.exe`**，会遮蔽基线 CMake 4.4.2，
> 表现为 `Could not create named generator Visual Studio 18 2026`。
> **调用 CMake / CTest 一律用绝对路径**：`& 'C:\Program Files\CMake\bin\cmake.exe'`、
> `& 'C:\Program Files\CMake\bin\ctest.exe'`（详见 `docs/pitfalls/` TRAP-P3-004）。

## 3. 构建 / 测试 / 部署

```powershell
# 方案 A：VS 生成器（多配置，推荐）
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug   --parallel
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Release --parallel
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Release --output-on-failure --timeout 120

# 方案 B：Ninja（单配置，需与 vcvars 同一命令）
cmd /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" >nul && cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=`"D:/Qt-debug`""
cmake --build build-debug --parallel

# 部署（可选，仅供本地自包含运行）：Release 产物已在 build/Release/，此步只补齐 Qt 运行库
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime --dir .\build\Release .\build\Release\WhalePet.exe
```

- `-DCMAKE_PREFIX_PATH="D:/Qt-debug"` 为 configure **必填**；缺失是「找不到 Qt6」的唯一常见原因。
- Debug/Release **必须使用不同构建目录**（`build` + `--config`，或 `build-debug`/`build-release`）。
- **Release 产物落点在构建目录内**：`cmake/OutputLayout.cmake` 的 `WHALEPET_RELEASE_DIR`（默认
  `${CMAKE_BINARY_DIR}/Release`）设置 `WhalePet` / `whalepet-mcp` 的 `RUNTIME_OUTPUT_DIRECTORY_RELEASE`，
  所以 `--config Release` 构建后两个 exe **直接出现在 `build/Release/`**，**PDB 与之同目录**
  （崩溃分析仍用 Release 产物 + PDB，见 `README.md` §六）。Debug 产物在 `build/Debug/`，
  测试可执行文件在 `build/<Config>/`。**2026-10-11 起 `deploy-release/` 与 `dist/WhalePet/`
  这两个产物目录已删除**（构建目录统一为 `build/`，见 §10）。
- 无显示环境跑 GUI 测试前：`$env:QT_QPA_PLATFORM = 'offscreen'`。
  **前提是该 exe 同级的 `platforms/` 目录里有对应配置的 offscreen 插件**，否则见下方「插件补齐」。
- **插件补齐（按配置，二者都要）**：`windeployqt` **只部署 `platforms/qwindows[d].dll`**，
  从不带 offscreen 插件；而一旦 exe 同级出现 `platforms/`，Qt 就把**该目录**当作插件目录、
  *不再回退*到 Qt 前缀 `D:/Qt-debug/plugins/`。缺插件时 `QApplication` 构造阶段即异常终止
  （退出码 `0x80000003`，且**不产生任何测试输出/日志文件**），极易误判为代码崩溃。

  ```powershell
  # Debug 目标（build/Debug/）：插件名带 d 后缀
  Copy-Item "D:/Qt-debug/plugins/platforms/qoffscreend.dll" "build/Debug/platforms/" -Force
  # Release 目标（build/Release/）：插件名不带后缀
  Copy-Item "D:/Qt-debug/plugins/platforms/qoffscreen.dll"  "build/Release/platforms/" -Force
  ```

  两者均属**验证辅助，不随正式发布**（`build/` 整体已被 `.gitignore` 忽略）。详见
  `docs/pitfalls/` `TRAP-P4-001` 与 `docs/pitfalls/` `TRAP-P3-005`。

## 4. CMake 要点

- `cmake_minimum_required(VERSION 3.21)`，`CMAKE_CXX_STANDARD 17`。
- 使用 `qt_standard_project_setup()` + `qt_add_executable()`（不要手写旧式组合）。
- 必需组件：`Core Gui Widgets Sql Network Test`（`Sql` 提供 QSQLITE、`Network` 提供
  `QTcpServer`，**均为 Qt 官方模块、非第三方依赖**）。
- **P7 新增目标**（依赖方向自上而下，见 `PLUGIN-ARCHITECTURE.md` §3.2）：
  `whalepet_platform`（`Qt6::Core` + core；**Windows 另链 `user32`** 以调用
  `GetForegroundWindow` / `GetLastInputInfo` / `SetWindowsHookExW` 等系统 API，见 P7.1）、
  `whalepet_plugin`（`Qt6::Core` + core）、
  `whalepet_contextapi`（`Qt6::Core` + `Qt6::Network` + core + plugin），
  均由 `whalepet_view` PUBLIC 链接。
  `Qt6::HttpServer` **刻意不使用**（本地通道用 `QTcpServer` 手写最小 HTTP，
  少一个模块依赖，见 `CONTEXT-API.md` §4）。
- **Windows 可执行文件图标**：`assets/icon/whalepet.ico`。CMake 在配置期生成
  `${CMAKE_CURRENT_BINARY_DIR}/whalepet_app_icon.rc`（写入图标的**绝对路径**并挂到
  `WhalePet` 目标），避免 `rc.exe` 按工作目录解析相对路径导致「静默用了默认图标」；
  图标缺失时 `FATAL_ERROR` 直接失败，不静默降级。运行时窗口 / 对话框图标由
  `main.cpp` 从 `:/icon/whalepet.ico` 设置（同一份 `.ico`）。
- 测试须固化超时：`set_tests_properties(<t> PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)`。
- 资源：立绘与台词经 `.qrc` 或随程序分发（见 `PRESENTATION.md` / `CHAT.md`）。

### 4.1 CMake 模块化拆分（强制约定）

顶层 `CMakeLists.txt` **只做编排**，所有具体配置拆分到 `cmake/` 目录下的独立 `.cmake`
模块文件中。新增构建配置 / 改构建逻辑时**必须遵循**以下约定，不得再把全部配置平铺回顶层文件：

1. **拆分维度（按功能模块）**：识别并拆分为独立模块，典型模块包括
   `CompileOptions`（全局编译选项与语言标准）、`QtDependencies`（Qt 依赖查找与插件校验）、
   `Libraries`（全部静态库目标）、`Executables`（可执行目标与产物落盘）、
   `Tests`（测试目标与 CTest 注册）、`OutputLayout`（产物目录与发布模式开关）、
   `PluginExamples`（动态插件示例，仅构建不分发）等；文件名须清晰反映模块用途。
2. **一律用 `include()`，禁止 `add_subdirectory()`**：本工程为单目录构建，
   `include()` 不创建新变量作用域，模块与顶层共享同一作用域，能保证拆分前后
   **变量可见性、target 属性、编译参数完全一致**；`add_subdirectory()` 会隔离作用域，
   导致 `WHALEPET_RELEASE_DIR`、`_qt_prefix` 等变量 / target 在后序模块不可见，破坏构建。
3. **`cmake_minimum_required` 与 `project()` 保留在顶层字面调用**：CMake 强制要求
   `project()` 必须是顶层文件中的字面、直接调用，仅放进被 `include()` 的文件会触发
   author 告警并注入占位的 `project(Project)`；因此工程元信息（名称/版本/语言）
   留在顶层 `CMakeLists.txt`，其余全局编译选项归入 `cmake/CompileOptions.cmake`。
4. **引入顺序即依赖顺序**：顶层按依赖先后 `include()` 各模块，顺序不可随意调换
   （例如 `Libraries` 依赖 `CompileOptions`/`QtDependencies`；`Executables` 依赖
   `Libraries`/`OutputLayout`；`Tests` 依赖 `Libraries`/`Executables`）。
5. **每个子文件顶部声明职责与依赖**：模块文件开头用注释写明本模块职责、依赖的前序模块、
   以及模块间依赖关系，由顶层统一编排管理。
6. **行为零改动**：模块化仅做文件级拆分，**不得改变任何编译目标、链接库或编译参数**；
   原有注释、变量命名风格（`WHALEPET_*`、`_qt_*`、子目标名等）原样保留。

> 当前模块清单与职责见顶层 `CMakeLists.txt` 顶部注释；验证改动是否等价可用
> `cmake -S . -B <临时目录> -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=D:/Qt-debug`
> 仅做 configure（不编译），确认无告警且生成全部 target / 测试即视为等价。

## 5. 依赖策略（不可协商）

- **零第三方依赖**（P7 修订口径，见 `README.md` §5.1）：不使用 vcpkg / Conan / FetchContent /
  ExternalProject，不联网拉取；**允许 Qt 官方模块**（`Core Gui Widgets Sql Network Test`）。
- SQLite → `Qt6::Sql`(QSQLITE)；测试 → `Qt6::Test`；图像 webp → Qt 内建图像插件；
  JSON → `Qt6::Core` 的 `QJsonDocument`（**不引第三方 JSON 库**）；
  本机通道 → `Qt6::Network` 的 `QTcpServer`（**只绑定 127.0.0.1**）。
- 版本一律保持基线，不得为绕过错误而放宽 `find_package` 版本。

## 6. 图像插件确认（WebP）

构建后须确认 `D:/Qt-debug/plugins/imageformats/` 存在 `qwebp.dll`（release）/ `qwebpd.dll`（debug）：
- 存在 → 直接支持 webp 立绘。
- 缺失 → 在 configure 期显式报错并给出 `windeployqt` 部署说明，不得静默失败。

## 7. 降级与可观测

| 情况 | 降级动作 | 观测点 |
|---|---|---|
| 可选 Qt 模块缺失 | `find_package(Qt6 QUIET COMPONENTS X)` + `if(Qt6X_FOUND)` | CMake 输出显式提示 |
| 无 GUI 环境 | `QT_QPA_PLATFORM=offscreen`；仍失败则 `SKIP_RETURN_CODE 77` | CTest 输出 `Skipped` + 原因 |
| 安装目录不可写 | 数据目录降级到用户目录 | 日志 `data dir fallback` |

> 禁止以删除断言 / 注释用例 / 放宽比较 / 吞异常等方式让结果「变绿」。

## 8. 失败排查（常见）

| 症状 | 修复 |
|---|---|
| 找不到 Qt6 包 | 补 `-DCMAKE_PREFIX_PATH="D:/Qt-debug"`，勿写成带 `6.8.4` 的路径 |
| Ninja 找不到编译器 | 与 `vcvars64.bat` 放同一条 `cmd /c` 内执行，或改用 VS 生成器 |
| 链接 `unresolved external symbol` | debug/release 混用；按 `--config` / `CMAKE_BUILD_TYPE` 分目录 |
| 运行缺 `Qt6Core.dll` | 用 `windeployqt` 配当前配置（`--debug`/`--release`） |
| 测试无显示崩溃 | 设 `QT_QPA_PLATFORM=offscreen`，并确认该 exe 同级 `platforms/` 内有对应配置的 offscreen 插件（`TRAP-P4-001`） |
| 某测试「失败且耗时数十秒」、**无任何输出**、退出码 `-2147483645`(`0x80000003`) | 初始化阶段即死，与业务代码无关：缺 `platforms/qoffscreend.dll`（跑过 `windeployqt` 后 Qt 不再回退前缀）。按 §3「插件补齐」处理（`TRAP-P4-001`） |
| `Could not create named generator Visual Studio 18 2026` | PATH 里的旧 `cmake.exe`（`D:/Strawberry/c/bin`）遮蔽了基线 CMake；改用绝对路径 `& 'C:\Program Files\CMake\bin\cmake.exe'`（`TRAP-P3-004`） |
| 数据库不落盘、`data/` 不生成 | 部署目录缺 `sqldrivers/qsqlite.dll`（静默降级内存库）；重跑 `windeployqt`，构建期已有插件检查（`TRAP-P3-003`） |
| 部署目录 offscreen 启动「进程存活但没反应」 | 缺 `platforms/qoffscreen.dll`；改用默认平台 + 产物断言验证（`TRAP-P3-005`，见 §9） |

## 9. 部署后最小化验证（Release）

`build/Release/` 在 `windeployqt` 补齐运行库后是**自包含**目录：干净 PATH 下可直接运行。
每次改动部署后按此表验证，**判据必须是「副作用」而不是「进程还活着」**：

| 步骤 | 命令（PowerShell 5.1） |
|---|---|
| 1. 干净 PATH 启动 | `$env:Path='C:\Windows\System32;C:\Windows'`；`Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue`；`$p=Start-Process .\build\Release\WhalePet.exe -PassThru` |
| 2. 采样 | `Start-Sleep -Seconds 8`；`Get-Process -Id $p.Id \| Select-Object Threads,WorkingSet64` |
| 3. **产物断言（权威判据）** | `Test-Path .\build\Release\data\whalepet.db` |
| 4. 收尾 | `Stop-Process -Id $p.Id -Force` |

正常基线：**以 `data/whalepet.db` 生成（≈ 53248 字节）为权威判据**；
参考量级 WS ≈ 100MB。线程数**不是稳定判据**（P6 实测 `Threads=16`，与早期记载的「≈ 30」不同，
差异未定位、不作结论——见 `ROADMAP-P6-Fin.md` 验证记录）。

- **不要**在部署目录用 `QT_QPA_PLATFORM=offscreen`：`windeployqt` 不部署 `platforms/qoffscreen.dll`，
  结果是「进程存活但 `main()` 之后的逻辑根本没跑」，极易误判为通过。
  确需无桌面验证时，把 `D:\Qt-debug\plugins\platforms\qoffscreen.dll` 拷进 `build/Release/platforms/`
  （仅供验证，不随正式发布）。
- **同样的问题也发生在构建目录**：若在 `build/Debug/` 下跑过 `windeployqt`，
  `platforms/` 里只有 `qwindowsd.dll`，此时 `ctest -C Debug` 的 GUI 测试会**异常退出且无输出**。
  需补 `qoffscreend.dll`（注意 **Release 无 `d` 后缀 / Debug 有 `d` 后缀**）。
  补齐命令见 §3「插件补齐」；判据同样是「有测试输出」而非「进程活着」（`TRAP-P4-001`）。
- 需要看运行日志时：`$env:QT_FORCE_STDERR_LOGGING='1'` 配合
  `Start-Process -RedirectStandardError <file>`（GUI 子系统否则看不到 `qWarning`）。
- **覆盖部署时不要删除 `deploy-release/data/`**：那是应用真实存档（`TRAP-P3-003`）。

## 10. 正式发布打包（免安装版 + NSIS 安装包）

> 打包脚本的**安装/卸载同步清单、运行期写权限（`stomach/`）与维护规范**见
> [`packages.md`](packages.md)；改动 `scripts/*` 前先读该文档。

一键脚本（**固定复用 `build/`**，不新建）：

```powershell
# 前置：build 已配置并完成 Release 构建（Release 产物落 build/Release/，含 PDB）
powershell -ExecutionPolicy Bypass -File scripts/package-release.ps1 `
    -AppName WhalePet -Version 0.3.0
```

流程（`scripts/package-release.ps1`）：

1. 校验并**复用固定构建目录 `build/`**（无 `-BuildDir` 参数）：目录**必须已存在**；单配置生成器
   （Ninja / Makefiles）下拒绝 `CMAKE_BUILD_TYPE=Debug` 的目录，VS 多配置生成器豁免（见 `P-105`）；
   脚本自身**从不新建 / 清理 / 切换**构建目录；
2. 暂存 `WhalePet.exe` 与 `whalepet-mcp.exe` 到 `dist/WhalePet-<版本>-portable/`；
3. `windeployqt --release --compiler-runtime` 补齐 Qt 运行库与插件到暂存目录；
4. 清理残留调试文件（`*.pdb` / `*.ilk` / `*.exp` / `*.lib`）；**清空并重建空的 `engine/` 目录**
   （用户自备 UCI 象棋引擎的落点，打包机上残留的引擎**绝不分发**）、清掉 `plugins/` 残留，
   并复制 `README.md` / `LICENSE`；
5. 打 `dist/WhalePet-<版本>-portable.zip`；
6. `makensis /V2 /INPUTCHARSET UTF8`（传 `/DAPP_NAME` `/DAPP_VERSION` `/DAPP_VERSION4`
   `/DSRC_DIR` `/DOUT_FILE`，路径必须绝对）→ `dist/WhalePet-<版本>-setup.exe`。

> `engine/` 与 `stomach/` 的安装 / 授权 / 卸载对应关系见 [`packages.md`](packages.md) §3 / §3.1。

**构建目录归属**（发布前必须已知，脚本只复用、不创建；2026-10-11 收敛为单一目录）：

| 构建目录 | 配置 | Release 产物落点 | 用途 |
|---|---|---|---|
| `build/` | VS 多配置，`--config Release` | `build/Release/`（含 PDB） | 开发部署（§9 崩溃分析）**与**发布打包源，二合一 |

> 首次发布前需自行完成（此后脚本只复用该目录）：
> `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"`
> 然后 `cmake --build build --config Release`。
> **旧的双目录方案已删除**：`build-package/`（`-DWHALEPET_PACKAGE=ON`）与 `deploy-release/`
> 一并移除，`dist/WhalePet/`（CMake 旧 exe 落点）不再产生。

**产出**（命名与台账见 [`release.md`](release.md)）：

| 产物 | 说明 |
|---|---|
| `dist/WhalePet-<版本>-portable/` | 免安装版目录 |
| `dist/WhalePet-<版本>-portable.zip` | 免安装版压缩包（直接分发） |
| `dist/WhalePet-<版本>-setup.exe` | NSIS 安装包（开始菜单 / 桌面快捷方式 + 卸载程序） |

**要点**：

- **开发产物与发布产物同一份 Release 构建**：`build/Release/` 同时是 §9 的部署与崩溃分析目录
  （PDB 与 exe 同目录），发布打包从该目录暂存、并在暂存阶段清除调试符号。
  **不为发布牺牲崩溃可分析性**（PDB 始终留在构建目录里，只是不随包分发）。
- **构建目录一律复用**：发布脚本禁止新建构建目录（历史痛点：旧 `package-release.ps1` 每次自行新建
  `build-package/`，与「复用既有构建目录」约束冲突，已退役；该目录已于 2026-10-11 删除）。
- **NSIS 编码**：`installer.nsi` 为 UTF-8，必须 `/INPUTCHARSET UTF8`（中文界面）；已由 `package-release.ps1` 传入。
- **PowerShell 编码**：`scripts/*.ps1` 刻意保持**纯 ASCII**——Windows PowerShell 5.1 会把无 BOM 的
  UTF-8 脚本按 ANSI 解析，中文字面量乱码并破坏语法（`docs/pitfalls/p6/P-035-ps51-ansi-utf8-no-bom.md`）。
- **发布目录不含用户数据**：`data/`（存档）、`stomach/`（胃袋，拖拽投喂落点）与
  `engine/`（用户自备的 UCI 象棋引擎）都是运行期数据；**随包只创建空的 `engine/`**，
  NSIS 打包时以 `/x "data"`、`/x "stomach"`、`/x "engine"` 等排除目录本身及内容，
  卸载时可选择保留（`installer.nsi` 同时排除并提示删除这三者）。
- **安装目录写权限**：程序以普通用户运行，需在 `<安装目录>/stomach` 与 `<安装目录>/engine` 落盘；
  安装程序用 `icacls` 给内置 Users 组授权（详见 `packages.md` §3 / §3.1）。
- 开发运行（§9）的存档在 **`build/Release/data/`**（构建目录内，打包不触碰）；
  免安装版目录 `dist/WhalePet-<版本>-portable/` **每次打包都会被重建**，不要把它当日常运行目录
  （在其中产生的 `data/` 会随下次打包消失；原 `deploy-release/data/` 的存档已于 2026-10-11 迁入 `build/Release/data/`）。
- **发布脚本回归是「全量回归」的组成部分**：改动过 `CMakeLists.txt` / `cmake/*`（尤其
  `OutputLayout.cmake`、`Executables.cmake`）/ `scripts/*` / 随包二进制 / 资源清单后，
  除 Debug 与 Release 的 CTest 外**必须**重跑本节打包流程，并逐条核验
  [`TESTING.md`](TESTING.md) **§8.2** 的 11 项清单（含**干净 PATH 启动**与**安装包静默往返**）。
  清单未过即视为发布门禁未通过；安装包静默往返需提权（UAC），自动化会话无法完成时
  **如实登记为待人工验收**，不得记作通过。

## 11. Qt Creator 集成（IDE 侧）

Qt Creator 20.0.2 实测可用于本工程（**含断点调试**，配合 §9 的 Release + PDB 流程）。三条约定：

1. **Qt 前缀回落（顶层 `CMakeLists.txt`，2026-10-10 增补）**：`CMakePresets.json` 已为预设方式
   提供 `CMAKE_PREFIX_PATH`；若在 IDE 里**按套件（kit）而非预设**配置，未手填前缀会让
   `find_package(Qt6 REQUIRED)` 直接失败（表现为「无法识别 Qt 项目 / 没有 Qt 版本」）。
   为此顶层增加一次**可观测回落**——仅当调用方完全没有给出 Qt 前缀时，依次尝试
   环境变量 `QT_ROOT`、项目基线 `D:/Qt-debug`，并打印 `-- 未指定 Qt 前缀，回落…`。
   它使用**普通变量而非 cache 变量**，不写 `CMakeCache.txt`，因此不会覆盖调用方的显式取值。
2. **`compile_commands.json` 不要写进 CMakeLists**：本机 CMake 4.4.2 官方文档明确
   `CMAKE_EXPORT_COMPILE_COMMANDS`「仅由 Makefile / Ninja 生成器实现，其它生成器忽略」。
   本工程用 VS 生成器，写它无效；Qt Creator 会自行生成
   `build/.qtc_clangd/compile_commands.json`（实测 142 个编译条目，覆盖全部源文件），
   无需人工干预。
3. **`.qtcreator/` 是 IDE 本机状态**：`.gitignore` 忽略其全部内容，仅保留可共享的
   `.qtcreator/project.json`（与 `.vscode/settings.json` 同一口径）。Qt Creator 的
   「预设探测」会在该目录下生成临时构建树（`qtc-cmake-presets-*/`），**不要**把它们当成
   工程构建目录，也不要手工清理正在使用的探测目录。

> 排查提示：若 Qt Creator 侧出现识别/配置异常，先看 **General Messages（CMake 输出）** 的
> 首条 error，再看 `build/.cmake/api/v1/reply/` 与 `build/.qtc_clangd/` 的时间戳是否刷新；
> 三者都正常时通常只是 IDE 需要重新配置（Build → Run CMake），而不是工程配置有误。
