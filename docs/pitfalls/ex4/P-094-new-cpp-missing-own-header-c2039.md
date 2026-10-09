# P-094 · 新增 `.cpp` 漏 include 声明所属头，编译期 C2039 / C3861

- **模块**：`src/viewmodel/MiniGameCompanionSource.cpp`
- **报错分层**：build（编译）
- **严重度**：minor
- **原编号**：—（EX4 新增）

## 现象（可复现步骤 / 报错原文）

EX4 新增 `MiniGameCompanionSource.cpp`（陪玩侧通用聚合数据源），实现 `read()` 时调用
`core::gameSampleFromSnapshot(...)`。增量构建 Debug 时报错：

```
src\viewmodel\MiniGameCompanionSource.cpp(66,18): error C2039: "gameSampleFromSnapshot":
不是 "whalepet::core" 的成员 [F:\develop\desktoppet\build\whalepet_view.vcxproj]
    F:\develop\desktoppet\src\core\GameState.h(17,11):
    参见"whalepet::core"的声明

src\viewmodel\MiniGameCompanionSource.cpp(66,18): error C3861:
“gameSampleFromSnapshot”: 找不到标识符
```

## 根因

该 `.cpp` 只包含了两个头：

- 自身头 `viewmodel/MiniGameCompanionSource.h`（其 `IGameCompanionSource.h` 间接带来
  `core/GameSnapshot.h` 与 `core/GameState.h`）；
- 接口头 `minigame/MiniGameCompanionSource.h`。

但 `gameSampleFromSnapshot()` 声明在**另一个**头 `core/MiniGameCompanion.h` 中，而
`GameState.h` / `GameSnapshot.h` 都不含它。**头文件之间不传递该函数声明**，故符号不可见。
这是「新增源文件时按记忆写 include、依赖传递包含」的典型漏项。

## 解决或规避

- 修复：在 `MiniGameCompanionSource.cpp` 补 `#include "core/MiniGameCompanion.h"`。
- 规避：新增 `.cpp` 后，对**本文件用到的每个非本文件符号**逐一确认其**声明所属头**并显式 include，
  不假设「已被别的头间接带进来」；完整类型前向声明只够用于指针 / 引用，调用成员或自由函数必须见声明。

## 影响与关联文档

- 关联：`src/viewmodel/MiniGameCompanionSource.{h,cpp}`、`src/core/MiniGameCompanion.h`、
  `src/viewmodel/IGameCompanionSource.h`。
- 结果：补 include 后 Debug / Release 构建退出码 0，CTest 各 **33/33**（含新增 `test_minigame_companion`）。
