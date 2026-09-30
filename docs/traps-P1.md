# traps · P1 — 工程骨架与桌宠外壳（真实踩坑记录）

> 对应 `ROADMAP-P1.md`。按 `README.md` §二.5 约定，**仅记录 P1 实施过程中真实复现并已排查解决**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> 环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。
> 首次记录时间：P1 实施首次跑通 configure / Debug+Release 构建 / CTest / windeployqt 部署。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P1-001` | `Q_INIT_RESOURCE` 写在命名空间内 → 符号被名称修饰 → LNK2019 | 编译/链接 | 已解决 |
| `TRAP-P1-002` | `qt_standard_project_setup()` 不启用 `AUTORCC`，`.qrc` 被忽略 → 资源符号未解析 | 构建配置 | 已解决 |
| `TRAP-P1-003` | `windeployqt --dir` 只部署依赖，**不复制 exe 本体** | 部署 | 已解决 |
| `TRAP-P1-004` | PowerShell 管道吞掉退出码，链接失败被误判为「构建成功」 | 验证方法 | 已规避 |
| `TRAP-P1-005` | 进程内存单次读数抖动（28→76MB），易误判为超阈值/泄漏 | 观测方法 | 已澄清 |

---

## TRAP-P1-001 — `Q_INIT_RESOURCE` 放入命名空间导致 LNK2019

**类别**：编译/链接 ｜ **影响**：`WhalePet` 与 `test_smoke` 无法链接，P1 卡在构建阶段

### 现象

立绘以 Qt 资源（`.qrc`）方式内嵌到静态库 `whalepet_view`。由于静态库的 Qt 资源
不会自动注册，在 `PoseView.cpp` 中调用 `Q_INIT_RESOURCE(assets)` 显式注册。构建报：

```
whalepet_view.lib(PoseView.obj) : error LNK2019: 无法解析的外部符号
  "int __cdecl whalepet::`anonymous namespace'::qInitResources_assets(void)"
  (?qInitResources_assets@?A0x3607271c@whalepet@@YAHXZ)，
  函数 "void __cdecl whalepet::`anonymous namespace'::ensureResources(void)" 中引用了该符号
WhalePet.exe : fatal error LNK1120: 1 个无法解析的外部命令
```

注意被引用符号的名字里出现了 **`whalepet::`anonymous namespace'::`** 前缀。

### 根因

`Q_INIT_RESOURCE(name)` 宏会展开为对 `qInitResources_<name>()` 的**声明 + 调用**。
`rcc` 生成的实现函数位于**全局作用域**；而当该宏出现在命名空间（含匿名命名空间）内部时，
宏内的声明被视为该命名空间的成员，C++ 名称修饰（MSVC 为 `?qInitResources_assets@...@whalepet@@`）
与实际生成的全局符号不一致，于是链接期找不到定义。

> Qt 官方文档对此有明确约束：**该宏不能用于命名空间内**。

### 解决

把资源初始化辅助函数移到**全局作用域**（不在任何命名空间内）定义：

```cpp
// PoseView.cpp —— 必须在全局作用域，不能放进 namespace whalepet / 匿名命名空间
static void whalepetInitAssetsResource()
{
    static bool initialized = false;
    if (!initialized) {
        Q_INIT_RESOURCE(assets);
        initialized = true;
    }
}

namespace whalepet {
// ... PoseView::loadPoseFile() 内调用 whalepetInitAssetsResource();
}
```

### 影响与关联文档

- 约束所有后续「静态库内嵌 Qt 资源」的写法：初始化调用点必须在全局作用域。
- 静态库资源需显式初始化这一点，是 P2+ 迁移全部 92 张立绘 / 台词库到 `assets/*.qrc` 时的**前置约束**（见 `ARCHITECTURE.md` §5、`BUILD.md` §4）。
- 关联：`TRAP-P1-002`（即便移到全局作用域，只要 `AUTORCC` 未开启，仍会报同一符号未解析）。

---

## TRAP-P1-002 — `qt_standard_project_setup()` 不启用 `AUTORCC`，`.qrc` 被静默忽略

**类别**：构建配置 ｜ **影响**：资源不编译进产物，运行期所有 `:/...` 路径失效

### 现象

修掉 `TRAP-P1-001`（初始化函数已移到全局作用域）后，链接错误变为：

```
whalepet_view.lib(PoseView.obj) : error LNK2019: 无法解析的外部符号
  "int __cdecl qInitResources_assets(void)" (?qInitResources_assets@@YAHXZ)，
  函数 "void __cdecl whalepetInitAssetsResource(void)" 中引用了该符号
```

即 `qInitResources_assets` 这个**函数本身根本没被生成**。

### 根因

`CMakeLists.txt` 起初只用 `qt_standard_project_setup()`，并把 `assets/assets.qrc`
列入了 `qt_add_library(...)` 的源文件列表。但：

- `qt_standard_project_setup()` 只保证开启 **`AUTOMOC` / `AUTOUIC`**，**不会开启 `AUTORCC`**；
- `CMAKE_AUTORCC` 关闭时，`.qrc` 被 CMake 当作「未知类型的源文件」**静默忽略**（既不报错也不生成 `qrc_assets.cpp`）。

实测印证：

```powershell
Get-ChildItem -Recurse build -Filter 'qrc_*'   # 空 —— 没有生成任何 qrc_*.cpp
Select-String build/CMakeCache.txt -Pattern 'AUTORCC'   # 空 —— 未启用
lib /list build/Debug/whalepet_view.lib | Select-String 'qrc'   # 空 —— 库中无资源对象
```

### 解决

在 `CMakeLists.txt` 中显式开启 `AUTORCC`（放在 `qt_standard_project_setup()` 之后）：

```cmake
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)   # 必须显式开启，否则 .qrc 被忽略
```

修复后构建日志出现 `Automatic RCC for assets/assets.qrc` 与 `qrc_assets_Release.cpp`，
资源正常编入静态库。

### 影响与关联文档

- 资源文件名带配置后缀（`qrc_assets_Debug.cpp` / `qrc_assets_Release.cpp`），Debug/Release 互不干扰，符合 `BUILD.md` §3「Debug/Release 必须分目录」的整体要求。
- 关联：`BUILD.md` §4（资源经 `.qrc` 分发）、`ARCHITECTURE.md` §5（`assets/*.qrc`）。
- 后续阶段若改用 Qt6 官方推荐的 `qt_add_resources()`，可替代 `AUTORCC` 方案，但需同步修改资源初始化函数名（`Q_INIT_RESOURCE(<资源名>)`）。

---

## TRAP-P1-003 — `windeployqt --dir` 不复制 exe 本体，部署目录缺主程序

**类别**：部署 ｜ **影响**：clean PATH 下「双击运行」验证失败

### 现象

按 `BUILD.md` §3 执行部署：

```powershell
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime `
    --dir .\deploy-release .\build\Release\WhalePet.exe
```

部署日志正常（拷贝了 `Qt6Widgets.dll`、`platforms/qwindows.dll`、`imageformats/qwebp.dll` 等），
但随后启动报：

```
Start-Process : 由于出现以下错误，无法运行此命令: The system cannot find the file specified。
```

检查发现 `deploy-release\WhalePet.exe` **不存在**。

### 根因

`windeployqt` 的语义是「为**指定路径的 exe** 部署它依赖的 Qt 运行时」；`--dir` 只决定
**依赖 DLL 的落地目录**，**不会把 exe 本身复制过去**。因此 `--dir` 指向一个非 exe 所在目录时，
该目录里有全套 DLL，却没有主程序。

### 解决

先把 exe 复制到部署目录，再在其上部署（或直接把 `--dir` 指到 exe 所在目录）：

```powershell
Copy-Item .\build\Release\WhalePet.exe .\deploy-release\ -Force
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime `
    --dir .\deploy-release .\deploy-release\WhalePet.exe
```

修复后部署目录可独立启动，`WhalePet.exe` 运行正常。

### 影响与关联文档

- 关联：`BUILD.md` §3（部署命令）、`BUILD.md` §8（「运行缺 `Qt6Core.dll` → 用 windeployqt 配当前配置」）。
- 建议后续把「复制 exe → windeployqt」固化为一条部署脚本 / CMake `POST_BUILD`，避免每次手动补拷贝。

---

## TRAP-P1-004 — PowerShell 管道吞掉退出码，构建失败被误判为成功

**类别**：验证方法（环境） ｜ **影响**：可能把「链接失败」当成「构建成功」，误导验收

### 现象

构建命令带上管道做输出裁剪时：

```powershell
cmake --build build --config Debug --parallel 2>&1 | Select-Object -Last 40
```

结果里明显有 `error LNK2019` / `fatal error LNK1120`，但命令返回的 `exitCode` 却是 **0**。

### 根因

管道场景下，返回给调用方的是**管道的退出码**（`Select-Object` 本身成功即 0），
原生命令（`cmake --build`）的失败退出码被**吞掉**，不能作为成功/失败判据。

### 解决 / 规避

- 需要判定成败时，读 `$LASTEXITCODE`（紧跟在原生命令之后），或**不使用管道**；
- 构建/测试验收统一以 `ctest ... --output-on-failure` 与 `$LASTEXITCODE` 为准，不依赖裁剪后的文字。

### 影响与关联文档

- 关联：`TESTING.md` §1（禁止以「变绿」为目的的宽松判定）、`BUILD.md` §8。
- 属于**验证方法**层面的坑：后续每个阶段的验收都应显式检查退出码，而非只看日志尾部。

---

## TRAP-P1-005 — 进程内存单次读数抖动（28MB → 76MB），易误判为超阈值 / 泄漏

**类别**：观测方法 ｜ **影响**：可能误判「空闲内存 < 30MB」验收项

### 现象

启动部署版后按单次采样读取 `WorkingSet64`：第 1 次读到约 **28.56MB**，1 秒后再读却跳到约 **76.75MB**，
看起来像内存暴涨 / 泄漏，与 `ROADMAP-P1.md`「内存 < 30MB」验收项冲突。

### 根因

单点采样受进程启动阶段（首次加载 DLL、首次绘制、字体/图标缓存、系统内存回收时机等）影响，
并不能代表**稳态**占用；不同启动时刻的瞬时读数不可比。

### 解决 / 规避

固定间隔多次采样取稳态值（实测 0.6s × 10 次）：

```
t=0.6s WS=29.27MB Private=6.69MB
t=1.2s WS=29.27MB Private=6.69MB
... （10 次全部持平）
```

**稳态结论**：WorkingSet ≈ **29.27MB**（< 30MB）、Private ≈ 6.69MB、空闲 CPU ≈ **0%**，达标。

### 影响与关联文档

- 结论：内存/CPU 验收需以「稳态多次采样」为准，禁止用启动瞬间的单次读数下判断。
- 关联：`ROADMAP-P1.md` 验收标准第 4 条。

---

## P1 实测结论（供 `ROADMAP-P1.md` 验收参考）

| 验收项 | 结果 | 说明 |
|---|---|---|
| Debug 可构建 | ✅ | `cmake --build build --config Debug --parallel` |
| Release 可构建 | ✅ | `cmake --build build --config Release --parallel` |
| CTest 通过 | ✅ | `test_smoke` Passed（offscreen，0.18s） |
| 部署后独立启动 | ✅ | `deploy-release/` 补齐 exe 后可运行，`qwebp.dll` 已随部署 |
| 空闲内存 < 30MB | ✅ | 稳态 WorkingSet ≈ 29.27MB |
| 空闲 CPU < 5% | ✅ | 实测 ≈ 0% |
| 立绘显示 / 无方框 / 置顶 / 拖拽 / 右键菜单 / 托盘 / 位置持久化 | ⏳ 待人工交互确认 | 已实现且进程不崩溃；透明置顶、拖拽、菜单交互需在真实桌面目视确认 |

> 结论：P1 的**工程骨架与外壳代码已跑通构建 / 测试 / 部署 / 启动**；
> 因「透明无方框、置顶不抢焦点、拖拽与右键菜单交互」需人工在桌面核验，
> 故 `ROADMAP-P1.md` **暂不追加 `Fin`**，待目视验收通过后再改名。
