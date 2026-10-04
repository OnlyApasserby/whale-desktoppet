#pragma once

// assets 资源（立绘 / 台词语料 / 应用图标 / 小游戏地图）的初始化入口。
//
// 立绘 qrc 挂在**静态库** `whalepet_view` 上（cmake/Libraries.cmake），
// 静态库的资源不会自动注册，必须在使用前显式 `Q_INIT_RESOURCE(assets)`。
//
// 收敛为单一入口的原因（路径 A 实施中发现并修复）：
//   * 旧实现把同一段初始化在 `PoseView.cpp` 与 `main.cpp` 各写了一份 static 函数；
//   * 而 `PoseLibrary::loadOne()` **自身不做初始化**，只是恰好依赖
//     `main.cpp` 在构造 `PetWindow` 前抢先初始化过。一旦有入口先调
//     `startPreload()` 而没碰 `PoseView`，93 张会全部静默加载失败
//     （只留 93 条 qWarning），且单元测试也无法独立驱动 PoseLibrary。
//
// `Q_INIT_RESOURCE` 宏**不能出现在任何命名空间内**（包括匿名命名空间），
// 否则宏内声明的 `qInitResources_assets` 会被 C++ 名称修饰成带命名空间的符号，
// 与 rcc 在全局作用域生成的符号不匹配，链接期报 LNK2019（见 docs/traps-P1.md）。
// 因此**定义**必须留在全局作用域（AssetsResource.cpp），本头文件只作声明。
//
// 幂等性：`qInitResources_assets()` 自带保护，重复调用安全，故多处调用无副作用。

void whalepetInitAssetsResource();
