# P-095 · `T x(U())` 被解析成函数声明（most vexing parse）→ C2228

- **模块**：`tests/test_minigame_companion.cpp`
- **报错分层**：build（编译）
- **严重度**：minor
- **原编号**：—（EX4 新增）

## 现象（可复现步骤 / 报错原文）

EX4 新增单测中，为验证「空访问器时 `attach()` 必须失败」，写了：

```cpp
MiniGameCompanionSource source(whalepet::viewmodel::MiniGameCompanionCandidates());
QVERIFY(!source.attach(&error));
```

编译报错（行号指向 `.attach`）：

```
tests\test_minigame_companion.cpp(274,5): error C2228:
“.attach”的左边必须有类/结构/联合 [F:\develop\desktoppet\build\test_minigame_companion.vcxproj]
```

## 根因

经典的 **most vexing parse**：`T x(U())` 中 `U()` 是「返回 `U` 的无参函数类型」，
于是整行被 C++ 解析为**函数声明** `T x(U (*)())`，`source` 变成函数名而非对象，
后续 `.attach` 自然不成立。仅当实参是**纯类型名 / 无参构造表达式**时才触发；
`source(candidatesOf(list))` 之类的函数调用表达式不受影响。

## 解决或规避

- 修复：用具名空变量传入，消除歧义：

  ```cpp
  const whalepet::viewmodel::MiniGameCompanionCandidates none; // 空访问器
  MiniGameCompanionSource source(none);
  ```

- 规避：单参数构造、且实参写成 `Type()` 或类似形态时，改用**具名变量**或**花括号初始化**
  （`T x{U()};`）——两者都不会退化成函数声明。

## 影响与关联文档

- 关联：`tests/test_minigame_companion.cpp`、`src/viewmodel/MiniGameCompanionSource.h`。
- 结果：改写后 Debug / Release 构建退出码 0；`test_minigame_companion` 16 项（含 init/cleanup）全 PASS。
