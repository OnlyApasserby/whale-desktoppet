# `test_context_http_security` 并行运行时偶发失败

> **原编号**：`TRAP-P8-006`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：测试环境（**非 P8 回归**） ｜ **影响**：全套并行执行时该用例偶发失败，易误判为本次改动引入。

### 现象

`ctest -C Debug`（默认并行度）中出现：

```
28 - test_context_http_security (Failed)   9.18 sec
```

而**单独复跑**与**顺序全量复跑**均通过（4.28s / 4.32s，33/33）。

### 根因（判定）

该用例绑定本地 HTTP 端口并做超时/连接清理断言，与其它绑定端口的用例
（`test_context_pipe` / `test_process_plugin`）在并行调度下可能互相抢占或受时序抖动影响。
本次改动未触碰 `src/contextapi/**`，且单跑/顺序跑稳定通过 → 判定为**并行环境偶发**，不是回归。

### 解决

未修改该用例（不掩盖）。验收口径统一为：**顺序执行** `ctest -C Debug` /
`ctest -C Release`，并要求全绿；如后续需要并行 CI，另立专项处理端口隔离。

### 影响与关联文档

`tests/test_context_http_security.cpp`（未改动）；`TESTING.md` §2 执行口径。

---
