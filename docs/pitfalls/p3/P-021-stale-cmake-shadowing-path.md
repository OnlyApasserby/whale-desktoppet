# PATH 里的旧 `cmake.exe` 遮蔽基线 CMake，生成器不可用

> **原编号**：`TRAP-P3-004`　**阶段**：P3　**来源**：原按阶段聚合的 `traps-P3.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：环境 / 构建 ｜ **影响**：configure 直接失败，且报错指向「生成器不存在」，容易误判为 CMake 版本坏了

### 现象

按 `BUILD.md` §2 初始化 PATH（把 `D:/Strawberry/c/bin` 等一并前置）后执行基线命令：

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"
```

报错原文（节选）：

```
CMake Error: Could not create named generator Visual Studio 18 2026
Generators
  Visual Studio 17 2022        = Generates Visual Studio 2022 project files.
  ...
```

即**报错列出的生成器清单里根本没有 `Visual Studio 18 2026`**。但与此同时
`cmake --version` 却是 `cmake version 4.4.2`（基线版本）。

### 根因

`D:/Strawberry/c/bin/cmake.exe` **确实存在**，且在 PATH 中排在
`C:\Program Files\CMake\bin` 之前，于是 `cmake` 解析到了这个随 Strawberry 附带的旧 CMake。
旧版不认识 VS 2026 生成器，所以清单里没有它。

判定依据：

```powershell
(Get-Command cmake).Source        # 未前置 PATH 时 → C:\Program Files\CMake\bin\cmake.exe
Test-Path 'D:/Strawberry/c/bin/cmake.exe'   # True ← 遮蔽源
(& 'C:\Program Files\CMake\bin\cmake.exe' --help) -match 'Visual Studio 18'
# → * Visual Studio 18 2026  = Generates Visual Studio 2026 project files.
```

### 解决 / 规避

- **一律用绝对路径调用基线工具**：
  `& 'C:\Program Files\CMake\bin\cmake.exe' ...`、`& 'C:\Program Files\CMake\bin\ctest.exe' ...`。
- 或把 `C:\Program Files\CMake\bin` 放到 PATH **最前**，不要放在 Strawberry 之后。
- 只按需把 Qt/Perl/NASM 加入 PATH：为跑测试注入 `D:\Qt-debug\bin` 时，**不要**顺手带上 `D:/Strawberry/c/bin`。

### 影响与关联文档

- `BUILD.md` §2/§3/§8（已补「用绝对路径调用 CMake」与排查项）。
- 与 `TRAP-P2-008` 同类：都是「环境注入不当导致命令解析到错的东西」，不是代码问题。

---
