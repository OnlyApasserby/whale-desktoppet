# `qt_standard_project_setup()` 不启用 `AUTORCC`，`.qrc` 被静默忽略

> **原编号**：`TRAP-P1-002`　**阶段**：P1　**来源**：原按阶段聚合的 `traps-P1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

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
