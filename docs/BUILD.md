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
> `& 'C:\Program Files\CMake\bin\ctest.exe'`（详见 `traps-P3.md` TRAP-P3-004）。

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

# 部署：Release 产物已直接生成在 deploy-release/，此步只补齐 Qt 运行库
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime --dir .\deploy-release .\deploy-release\WhalePet.exe
```

- `-DCMAKE_PREFIX_PATH="D:/Qt-debug"` 为 configure **必填**；缺失是「找不到 Qt6」的唯一常见原因。
- Debug/Release **必须使用不同构建目录**（`build` + `--config`，或 `build-debug`/`build-release`）。
- **Release 产物目录 = 部署目录**：`CMakeLists.txt` 以 `WHALEPET_DEPLOY_DIR`（默认 `deploy-release/`）
  设置 `WhalePet` 的 `RUNTIME_OUTPUT_DIRECTORY_RELEASE`，所以 `--config Release` 构建后
  `WhalePet.exe` **直接出现在 `deploy-release/`**，与 Qt 运行库同目录，**无需再 `Copy-Item`**。
  Debug 产物仍在 `build/Debug/`；测试可执行文件仍在 `build/<Config>/`（不污染部署目录）。
- 无显示环境跑 GUI 测试前：`$env:QT_QPA_PLATFORM = 'offscreen'`。
  **前提是该 exe 同级的 `platforms/` 目录里有对应配置的 offscreen 插件**，否则见下方「插件补齐」。
- **插件补齐（按配置，二者都要）**：`windeployqt` **只部署 `platforms/qwindows[d].dll`**，
  从不带 offscreen 插件；而一旦 exe 同级出现 `platforms/`，Qt 就把**该目录**当作插件目录、
  *不再回退*到 Qt 前缀 `D:/Qt-debug/plugins/`。缺插件时 `QApplication` 构造阶段即异常终止
  （退出码 `0x80000003`，且**不产生任何测试输出/日志文件**），极易误判为代码崩溃。

  ```powershell
  # Debug 目标（build/Debug/）：插件名带 d 后缀
  Copy-Item "D:/Qt-debug/plugins/platforms/qoffscreend.dll" "build/Debug/platforms/" -Force
  # Release 部署目录（deploy-release/）：插件名不带后缀
  Copy-Item "D:/Qt-debug/plugins/platforms/qoffscreen.dll"  "deploy-release/platforms/" -Force
  ```

  两者均属**验证辅助，不随正式发布**（`build/` 已整体被 `.gitignore` 忽略；
  `deploy-release/platforms/` 若被提交需排除）。详见 `traps-P4.md` `TRAP-P4-001`
  与 `traps-P3.md` `TRAP-P3-005`。

## 4. CMake 要点

- `cmake_minimum_required(VERSION 3.21)`，`CMAKE_CXX_STANDARD 17`。
- 使用 `qt_standard_project_setup()` + `qt_add_executable()`（不要手写旧式组合）。
- 必需组件：`Core Gui Widgets Sql Test`（`Sql` 提供 QSQLITE，**非第三方依赖**）。
- **Windows 可执行文件图标**：`assets/icon/whalepet.ico`。CMake 在配置期生成
  `${CMAKE_CURRENT_BINARY_DIR}/whalepet_app_icon.rc`（写入图标的**绝对路径**并挂到
  `WhalePet` 目标），避免 `rc.exe` 按工作目录解析相对路径导致「静默用了默认图标」；
  图标缺失时 `FATAL_ERROR` 直接失败，不静默降级。运行时窗口 / 对话框图标由
  `main.cpp` 从 `:/icon/whalepet.ico` 设置（同一份 `.ico`）。
- 测试须固化超时：`set_tests_properties(<t> PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)`。
- 资源：立绘与台词经 `.qrc` 或随程序分发（见 `PRESENTATION.md` / `CHAT.md`）。

## 5. 依赖策略（不可协商）

- **零新依赖**：不使用 vcpkg / Conan / FetchContent / ExternalProject，不联网拉取。
- SQLite → `Qt6::Sql`(QSQLITE)；测试 → `Qt6::Test`；图像 webp → Qt 内建图像插件。
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

`deploy-release/` 是**自包含**目录：干净 PATH 下可直接运行。每次改动部署后按此表验证，
**判据必须是「副作用」而不是「进程还活着」**：

| 步骤 | 命令（PowerShell 5.1） |
|---|---|
| 1. 干净 PATH 启动 | `$env:Path='C:\Windows\System32;C:\Windows'`；`Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue`；`$p=Start-Process .\deploy-release\WhalePet.exe -PassThru` |
| 2. 采样 | `Start-Sleep -Seconds 8`；`Get-Process -Id $p.Id \| Select-Object Threads,WorkingSet64` |
| 3. **产物断言（权威判据）** | `Test-Path .\deploy-release\data\whalepet.db` |
| 4. 收尾 | `Stop-Process -Id $p.Id -Force` |

正常基线：线程 ≈ 30、WS ≈ 100MB、`data/whalepet.db` 生成（≈ 53248 字节）。

- **不要**在部署目录用 `QT_QPA_PLATFORM=offscreen`：`windeployqt` 不部署 `platforms/qoffscreen.dll`，
  结果是「进程存活但 `main()` 之后的逻辑根本没跑」，极易误判为通过。
  确需无桌面验证时，把 `D:\Qt-debug\plugins\platforms\qoffscreen.dll` 拷进 `deploy-release/platforms/`
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
> [`packages.md`](packages.md)；改动 `packaging/*` 前先读该文档。

一键脚本：

```powershell
powershell -ExecutionPolicy Bypass -File packaging/make-package.ps1
```

流程（`packaging/make-package.ps1`）：

1. 以 `-DWHALEPET_PACKAGE=ON` 配置**独立**构建目录 `build-package`：
   Release 产物输出到 `dist/WhalePet`，且**不生成调试符号**（不加 `/Zi` `/DEBUG`，
   CMake 对 Release 默认不产出 PDB）；
2. 构建 Release；
3. `windeployqt --release` 补齐 Qt 运行库与插件到 `dist/WhalePet`；
4. 清理残留调试文件（`*.pdb` / `*.ilk` / `*.exp` / `*.lib`），并复制 `README.md` / `LICENSE`；
5. `makensis /INPUTCHARSET UTF8 packaging/whalepet.nsi` → `dist/WhalePet-Setup-<版本>.exe`。

**产出**：

| 产物 | 说明 |
|---|---|
| `dist/WhalePet/` | 免安装版（zip 后即分发） |
| `dist/WhalePet-Setup-<版本>.exe` | NSIS 安装包（开始菜单 / 桌面快捷方式 + 卸载程序） |

**要点**：

- **开发部署与正式发布分离**：`WHALEPET_PACKAGE=OFF`（默认）→ `deploy-release/` 含 PDB，供 §9 崩溃分析；
  `ON` → `dist/WhalePet/` 无调试符号。两者互不影响，**不为发布牺牲崩溃可分析性**。
- **NSIS 编码**：`whalepet.nsi` 为 UTF-8，必须 `/INPUTCHARSET UTF8`（中文界面）；已由 `make-package.ps1` 传入。
- **PowerShell 编码**：`make-package.ps1` 刻意保持**纯 ASCII**——Windows PowerShell 5.1 会把无 BOM 的
  UTF-8 脚本按 ANSI 解析，中文字面量乱码并破坏语法（`traps-P6.md` `TRAP-P6-004`）。
- **发布目录不含用户数据**：`data/`（存档）与 `stomach/`（胃袋，拖拽投喂落点）都是运行期生成的数据；
  NSIS 打包时以 `/x "data"`、`/x "stomach"` 等排除目录本身及内容，卸载时可选择保留。
- **安装目录写权限**：程序以普通用户运行，需在 `<安装目录>/stomach` 落盘；安装程序用
  `icacls` 给内置 Users 组授权（详见 `packages.md` §3）。
- 覆盖部署时**不要删除 `dist/WhalePet/data/`**（若已运行过，那是真实存档）。
