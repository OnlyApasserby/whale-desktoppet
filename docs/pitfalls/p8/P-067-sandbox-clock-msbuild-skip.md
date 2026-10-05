# 沙箱时钟落后于构建产物 → MSBuild 增量构建**静默跳过重编译**

> **原编号**：`TRAP-P8-009`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：环境 / 验证（**非代码缺陷**） ｜ **影响**：改了源码、`cmake --build` 成功、`ctest` 仍复现
**改动前**的行为，极易把环境假象误判为逻辑缺陷（本次为此多轮排查）。

### 现象

修改 `src/core/PetStateMachine.cpp` 后重新构建并测试，断言仍按**旧语义**失败；
构建输出只有 `xxx.vcxproj -> xxx.lib`，**看不到 `PetStateMachine.cpp` 编译行**。

### 根因

本机 shell 时钟落后于仓库已有产物的 mtime（sandbox 时钟 ≈ 00:2x，而 `build/Debug/*` 为 20:5x，
即约 20 小时"未来"）；工具写出的源文件 mtime 又比产物更旧：

```
src/core/PetStateMachine.cpp                        2026/10/4 00:22:22   ← 源比产物旧
build/Debug/whalepet_core.lib                       2026/10/4 20:52:22   ← 产物"未来"
build/whalepet_core.dir/Debug/PetStateMachine.obj   2026/10/4 00:27:48
```

MSBuild 按「输入是否比输出新」做增量 → 判定一切最新 → 只打印目标输出行，
**不重新编译 / 不重新归档 / 不重新链接** → `ctest` 继续运行旧 `.exe`（旧核心已静态链进旧 exe）。

### 解决（可复现）

任选其一，并**确认构建日志出现对应 `.cpp` 的编译行**：

1. 把修改过的源文件 mtime 设到产物之后（如 `(Get-Date).AddDays(1)`）再构建；
2. 或删除受影响产物强制重建：`build/Debug/{whalepet_core.lib, whalepet_view.lib, test_*.exe}`。

> 校验口径：只出现 `xxx.vcxproj -> xxx.lib/.exe` 表示该步骤被**跳过**；
> 必须出现 `PetStateMachine.cpp` / `test_xxx.cpp` 之类的**编译行**才算真的重建。
> 修正后 Debug / Release 全量 CTest 各 **35/35 通过**（与"旧二进制"的错误结论完全相反，
> 反证此前失败是环境假象）。

### 影响与关联文档

`docs/BUILD.md`（增量构建）、`docs/TESTING.md`（验收口径）。与代码无关，
但会直接污染「测试结论」的可信度：**先确认二进制是新的，再解释断言失败**。

---
