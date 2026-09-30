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

## 3. 构建 / 测试 / 部署

```powershell
# 方案 A：VS 生成器（多配置，推荐）
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"
cmake --build build --config Debug   --parallel
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --timeout 120

# 方案 B：Ninja（单配置，需与 vcvars 同一命令）
cmd /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" >nul && cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=`"D:/Qt-debug`""
cmake --build build-debug --parallel

# 部署
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime --dir .\deploy-release .\build\Release\WhalePet.exe
```

- `-DCMAKE_PREFIX_PATH="D:/Qt-debug"` 为 configure **必填**；缺失是「找不到 Qt6」的唯一常见原因。
- Debug/Release **必须使用不同构建目录**（`build` + `--config`，或 `build-debug`/`build-release`）。
- 无显示环境跑 GUI 测试前：`$env:QT_QPA_PLATFORM = 'offscreen'`。

## 4. CMake 要点

- `cmake_minimum_required(VERSION 3.21)`，`CMAKE_CXX_STANDARD 17`。
- 使用 `qt_standard_project_setup()` + `qt_add_executable()`（不要手写旧式组合）。
- 必需组件：`Core Gui Widgets Sql Test`（`Sql` 提供 QSQLITE，**非第三方依赖**）。
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
| 测试无显示崩溃 | 设 `QT_QPA_PLATFORM=offscreen` |
