# P-083 断言宏内含逗号的花括号初始化列表被当作多参数 → C2187/C2958

- **阶段**：P9 渐进式插件化（主阶段/开发阶段）
- **模块**：tests
- **报错分层**：build
- **严重度**：minor
- **日期 / 版本**：2026-10-05，工作区未提交（P9-B 单测轮次）
- **环境**：CMake 4.4.2 / VS 18 2026 / Qt 6.8.4 / PowerShell 5.1 / 生成器 Visual Studio 18 2026 / Debug
- **关联**：`docs/TESTING.md` §2（既有约定）；`docs/pitfalls/p7/P-042`（TRAP-P7-004，同源问题）

## 一、问题描述

为新测试写断言时使用
`QCOMPARE(config.servers.at(0).arguments, QStringList{ QStringLiteral("--a"), QStringLiteral("b") });`，
初始化列表内的逗号被 `QCOMPARE` 宏当作参数分隔符 → 展开后语法错误。

- 复现命令：`& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug --parallel`
- 复现率：必现
- 影响面：`test_process_plugin` 编译失败
- 证据：
```
F:\develop\desktoppet\tests\test_process_plugin.cpp(179,5): error C2187: 语法错误: 此处出现意外的“)”
F:\develop\desktoppet\tests\test_process_plugin.cpp(179,1): error C2958: 左 大括号“{”未能正确匹配
F:\develop\desktoppet\tests\test_process_plugin.cpp(179,5): error C2440:
  “<function-style-cast>”: 无法从“initializer list”转换为“QStringList”
```

## 二、原因分析

`QCOMPARE` 是**宏**，实参以未加括号的逗号分隔。`QStringList{ "a", "b" }` 中的逗号处于
未加括号的花括号初始化列表内，预处理阶段即被切分为第 3、4 个实参 → 展开后语法错误。
同类的 `QStringList{ "dup" }`（单元素、无逗号）不受影响，故既有用例一直正常，
本次属「首次写出**含逗号**的初始化列表」才暴露。

- 定位过程：首条 error 指向第 179 行，对照该行写法与宏定义即可确认；
  被排除的假设是 `QStringList` 与 `QList<QString>` 类型不匹配（错误发生在语法层，而非重载解析）。
- 根因：断言宏的「参数切分」与「初始化列表逗号」冲突。

## 三、解决方案

先把初始化列表落到**局部变量**（不在宏内），再比较：

```cpp
// 注意：断言宏内不放含逗号的初始化列表（会被宏参数切分，见 TESTING.md §2）
const QStringList expectedArgs{ QStringLiteral("--a"), QStringLiteral("b") };
QCOMPARE(config.servers.at(0).arguments, expectedArgs);
```

- 验证：`cmake --build build --config Debug --parallel` 退出码 0；
  `test_process_plugin` 通过（Debug 5.86s / Release 5.85s）；Debug 与 Release 全量均 36/36
- 降级：无

## 四、预防措施

- 该约束**早已**写在 `docs/TESTING.md` §2，并在 P7 由 `P-042` 登记过——本次属
  **未先查约束即书写**导致的重复命中，条目留作证据。
- 书写规则（重申）：断言宏内不放**含逗号**的花括号初始化列表 / 多层模板；先构造局部变量再断言。
- 回灌规则：无新增条款（既有条款已足够），执行面要求「写测试前先过一遍 TESTING.md §2」。
