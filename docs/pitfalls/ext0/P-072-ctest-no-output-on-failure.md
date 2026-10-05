# QTest 用例失败时 CTest 拿不到任何输出（`-o -,txt` 静默失效）

> **原编号**：`TRAP-EXT0-005`　**阶段**：EXT0　**来源**：原按阶段聚合的 `traps-extend0.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**阶段**：P8 立绘加载路径 A（2026-10-04）· **类型**：测试基础设施 · **影响面**：所有 GUI 测试的失败定位

### 现象

新增 `test_pose_assets` 后 CTest 报 `***Failed`，但**看不到任何用例结果**：

- `ctest --output-on-failure`：无输出；
- `ctest -V`（verbose）：同样无 QTest 用例行，只有汇总；
- 直接跑 `.\build\Debug\test_pose_assets.exe -o -,txt`（管道 / `Start-Process -RedirectStandardOutput`）：stdout 与 stderr 均为 **0 字节**；
- 退出码为 `1`（正常失败，非崩溃）。

同期 `test_smoke` 等既有 GUI 测试通过——它们只校验退出码，故从未暴露该问题。

### 根因

Qt 6.8.4 在 Windows 上，`QTest` 的 plain-text logger 写 stdout 这一路在
「无控制台 / 输出被重定向」的组合下不可靠：`qt_add_executable` 产出的 exe 走
Qt 的默认消息处理器，stdout 句柄在部分宿主下不可写，QTest **静默丢弃**全部用例行
（既不报错也不影响退出码）。这与 `docs/pitfalls/` 记录的
「QTest 在无控制台时改走 `OutputDebugString`」是同一类问题的不同表现。

### 解决

不要依赖 stdout。**让 QTest 直接写文件**，再读该文件：

```powershell
$p = Start-Process -FilePath ".\build\Debug\test_pose_assets.exe" `
     -ArgumentList '-o','build\pose_result.txt,txt' `
     -WorkingDirectory "f:\develop\desktoppet" -NoNewWindow -PassThru -Wait
Get-Content build\pose_result.txt -Encoding Default   # 注意：QTest 输出非 UTF-8
```

关键点：

1. 参数用 `-o <文件>,txt`（**文件路径**，不是 `-`），绕开 stdout；
2. 读文件用 `-Encoding Default`（UTF-8 读取会得到空行——QTest 默认按本地代码页输出）；
3. CTest 侧保持既有 `-o -,txt` 不变：**退出码判定是可靠的**，只是失败详情拿不到；
   需要详情时用上面的文件法，不要因为「CTest 没输出」就去删断言或放宽比较。

### 影响与关联文档

- 关联：`cmake/Tests.cmake`（`-o -,txt` 的由来）、`docs/pitfalls/`。
- 教训：**`ctest` 只报 `***Failed` 而无详情 ≠ 测试没有断言输出**。
  Windows + QtTest 环境下，定位失败必须准备「QTest 写文件」这条备用通道；
  且这条通道要**在写测试时就会用**，而不是等到失败了才临时找。

---
