# `Q_INIT_RESOURCE` 放入命名空间导致 LNK2019

> **原编号**：`TRAP-P1-001`　**阶段**：P1　**来源**：原按阶段聚合的 `traps-P1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

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
