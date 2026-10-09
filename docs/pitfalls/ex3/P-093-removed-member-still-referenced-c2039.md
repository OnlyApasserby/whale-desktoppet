# P-093 · 移除服务成员后遗漏调用方，编译期 C2039

- **模块**：`src/viewmodel/GameCompanionService.h` → `src/viewmodel/PetContextProvider.cpp`
- **报错分层**：build（编译）
- **严重度**：minor
- **原编号**：—（EX3 新增）

## 现象（可复现步骤 / 报错原文）

EX3 把 `GameCompanionService` 的数据源由 `gamestate::IGameStateAdapter` 换成中立的
`IGameCompanionSource`，随 `gamestate` 一并移除了 `profile()` 访问器与 `m_profile` 成员。
重新构建 Debug 时报错：

```
src\viewmodel\PetContextProvider.cpp(90,66): error C2039: "profile":
不是 "whalepet::viewmodel::GameCompanionService" 的成员
    src\viewmodel\GameCompanionService.h(25,7):
    参见"whalepet::viewmodel::GameCompanionService"的声明
```

## 根因

`PetContextProvider::snapshot()` 用 `m_gameCompanion->profile().engine` 填充 `gameEngine` 字段；
该调用方不在 `gamestate` 目录内。移除成员前的全仓检索只覆盖了 `gamestate` / `GameProfile` 关键字，
**未覆盖「服务成员访问点」**（`->profile()`），于是出现调用方遗漏。属接口收缩时的连带修改漏项。

## 解决或规避

- 修复：改为 `out.gameEngine.clear();`（EX3 起无数据源提供引擎标识；EX4 由小游戏状态源填充），
  并加注释说明来源变化。
- 规避：接口 / 成员收缩后，除按被移除的**类型名**检索外，还应对**成员访问模式**
  （如 `->profile()`、`->adapter()`）做一次全仓检索，再进入编译。

## 影响与关联文档

- 关联：`src/viewmodel/PetContextProvider.cpp`、`src/viewmodel/GameCompanionService.{h,cpp}`、
  `src/contextapi/ContextSnapshot.h`（`gameEngine` 字段保留，值暂空）。
- 结果：修复后 Debug / Release 构建退出码 0，CTest 各 **32/32**。
