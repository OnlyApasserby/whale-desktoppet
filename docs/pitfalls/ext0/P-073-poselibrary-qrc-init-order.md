# `PoseLibrary` 自身不初始化 qrc，静默依赖 `main()` 的调用顺序

> **原编号**：`TRAP-EXT0-006`　**阶段**：EXT0　**来源**：原按阶段聚合的 `traps-extend0.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**阶段**：P8 立绘加载路径 A（2026-10-04）· **类型**：隐式顺序依赖 · **影响面**：静态库资源初始化

### 现象

为 `PoseLibrary` 写单元测试时，单独构造 `PoseLibrary` 并调用 `startPreload()`，
12 张 core 立绘**全部加载失败**（`load()` 返回 false），只留 12 条
`[PoseLibrary] 立绘加载失败（跳过）` 告警。而同样的代码在应用里跑得好好的。

### 根因

立绘 qrc 挂在**静态库** `whalepet_view` 上，静态库资源不会自动注册，必须显式
`Q_INIT_RESOURCE(assets)`。旧实现里这段初始化被**写了两份**（`PoseView.cpp` 与
`main.cpp` 各一份 static 函数），而 `PoseLibrary::loadOne()` **自己不调**，
只是恰好依赖 `main.cpp` 在构造 `PetWindow` 之前抢先初始化过一次：

```
main() → whalepetInitAssetsResource()   ← 只有这一处真正保证了「先初始化」
      → PetWindow 构造 → PoseView::loadSourcePixmap() 才调 whalepetInitAssetsResource()
      → PoseLibrary::loadOne()          ← 从不自己调，靠上面那条隐式链路
```

于是「库可独立工作」这件事是**假的**。任何绕过 `main()` 的入口（单元测试、
将来的外部资源加载入口、将来的命令行工具）都会踩空，且失败形态是
「93 张全缺 + 只有 stderr 告警、界面停在首帧」——正是 `TRAP-EXT0-003` 的静默缺图家族。

### 解决

把初始化收敛为**全局作用域的单一入口** `src/view/AssetsResource.{h,cpp}`，
由 `PoseView` / `PoseLibrary::loadOne` / `main` 三处共用：

- `AssetsResource.cpp` 里的**定义必须在全局作用域**——`Q_INIT_RESOURCE` 宏不能进任何
  命名空间（含匿名命名空间），否则宏内声明的 `qInitResources_assets` 会被名称修饰成
  带命名空间的符号，与 rcc 在全局作用域生成的符号不匹配，链接期报 LNK2019
  （`docs/pitfalls/` 已记录，此处是**第二处**需要该约束的地方）；
- `loadOne()` 开头自行调用，库不再依赖任何调用顺序；
- `qInitResources_assets()` 自带幂等保护，三处重复调用无害。

### 验证

- `test_pose_assets::libraryPreloadsCoreTierSynchronously`：裸构造 `PoseLibrary`
  （不经过 `main`、不经过 `PetWindow`）→ core 12 张全部就绪，`failedCount()==0`；
- `test_pose_assets::everyRegisteredPosePassesStrictValidation`：93 张逐张严格校验通过。

### 影响与关联文档

- 关联：`src/view/AssetsResource.{h,cpp}`、`src/view/PoseLibrary.cpp`、
  `src/view/PoseView.cpp`、`src/app/main.cpp`、`docs/pitfalls/`。
- 教训：**静态库里的资源，「谁用谁初始化」比「在 main 里统一初始化」更可靠**。
  隐式的调用顺序依赖不会编译报错、不会链接报错，只在绕开它时静默失败；
  库级单测正是用来暴露这类「假的独立性」的。
