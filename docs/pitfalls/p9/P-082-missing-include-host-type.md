# P-082 新增插件漏 include 宿主类型头 → C2027 / C2039

- **阶段**：P9 渐进式插件化（主阶段/开发阶段）
- **模块**：src/viewmodel/builtin（及 tests）
- **报错分层**：build
- **严重度**：minor
- **日期 / 版本**：2026-10-05，工作区未提交（P9-A / P9-B 实现轮次）
- **环境**：CMake 4.4.2 / VS 18 2026 / Qt 6.8.4 / PowerShell 5.1 / 生成器 Visual Studio 18 2026 / Debug
- **关联**：`docs/ROADMAP-P9.md` §P9-A；`src/plugin/PluginInterface.h`（前向声明处）

## 一、问题描述

`plugin::PluginContext` 中的 `controller` / `db` 是**前向声明**指针
（`PluginInterface.h` 保持「能力总线不依赖 view / model」）。插件实现里解引用它们时
若未 include 具体类型头，编译报「使用了未定义类型」。同类问题在测试中再次出现：
测试使用 `ProcessPluginConfig` 却只 include 了 `ProcessPluginLoader.h`。

- 复现命令：`& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug --parallel`
- 复现率：必现（两处独立命中）
- 影响面：`whalepet_view` 与 `test_process_plugin` 编译失败
- 证据：
```
F:\develop\desktoppet\src\viewmodel\builtin\GrowthServicePlugin.cpp(71,12):
  error C2027: 使用了未定义类型“whalepet::PetController”
F:\develop\desktoppet\tests\test_process_plugin.cpp(31,25):
  error C2039: "ProcessPluginConfig": 不是 "whalepet::plugin" 的成员
```

## 二、原因分析

前向声明只够用于**指针传递与声明**，不足以调用成员函数（需要完整定义）。
`GrowthServicePlugin::start()` 调 `ctx.controller->setGrowthService(...)`，
而该翻译单元只 include 了 `GrowthService.h`；`PetController` 完整定义在
`viewmodel/PetController.h`，前向声明无法提供成员签名。

同理，`ProcessPluginConfig` 定义在独立头 `plugin/process/ProcessPluginConfig.h`，
只 include `ProcessPluginLoader.h` 不会传递该定义（P9-B 特意把 Config 拆为独立头，
避免与 Loader 互相包含）。

- 定位过程：只看首条 error（C2027），直接定位到缺定义；第二条（C2039）在修复后
  的下一次构建中成为新的首条 error。
- 根因：把「指针可用」误当作「类型可用」。

## 三、解决方案

- `GrowthServicePlugin.cpp` 增 `#include "viewmodel/PetController.h"`
- `test_process_plugin.cpp` 增 `#include "plugin/process/ProcessPluginConfig.h"`
- 验证：`cmake --build build --config Debug --parallel` 退出码 0；
  Debug 全量 CTest 36/36；Release 构建退出码 0

## 四、预防措施

- 新写 builtin 插件时，凡解引用 `PluginContext` 内的前向声明类型，先在 .cpp 补 include；
  `BuiltinServicePlugins.h` 顶部已补提示：「插件实现需自行 include 宿主类型头（`PluginContext`
  中的类型为前向声明）」。
- 新增独立头（如 `ProcessPluginConfig.h`）后，使用处必须显式 include 该头，不能依赖传递包含。
- 回灌规则：无（属通用 C++ 依赖常识，已在头文件注释中固化提示）。
