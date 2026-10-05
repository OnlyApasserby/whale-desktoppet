# 无控制台环境下 QTest 结果「消失」

> **原编号**：`TRAP-P2-003`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：验证方法 ｜ **影响**：用例结果无法被 CTest / 管道 / 重定向捕获，看起来像「测试没跑」

### 现象

`add_test(NAME test_state_machine COMMAND test_state_machine)` 手工执行无任何输出；
即使把 stdout 重定向到文件，文件也是空的，但进程退出码正常。
排查过程中还留下了 `-_out` 之类的空日志（已清理）。

### 根因

Windows 上 QTest 的默认日志器在**进程没有控制台**时（CTest 启动、隐藏窗口、
`Start-Process` 无 stdio 继承等场景）会退化为 `OutputDebugString` 输出，
因此标准输出/文件重定向都拿不到内容。

### 解决

给测试命令显式指定日志目标（stdout + txt 格式），CMake 中固化：

```cmake
add_test(NAME test_state_machine COMMAND test_state_machine -o -,txt)
add_test(NAME test_line_table    COMMAND test_line_table    -o -,txt)
add_test(NAME test_smoke         COMMAND test_smoke         -o -,txt)
```

### 影响与关联文档

- 后续新增测试目标**必须**带 `-o -,txt`，否则无法在 CI/CTest 中取证。
- 关联：`docs/TESTING.md` §1（禁止用「看不到结果」蒙混变绿）、`CMakeLists.txt`。

---
