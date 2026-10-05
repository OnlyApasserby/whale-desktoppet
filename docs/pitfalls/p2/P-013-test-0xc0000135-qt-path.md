# 未注入 Qt `bin` 时 CTest 全部 `0xC0000135`，形似崩溃

> **原编号**：`TRAP-P2-008`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：环境/验证 ｜ **影响**：3 个用例「全挂」且退出码形似崩溃，极易误判为代码回归

### 现象

在未把 Qt `bin` 加入 `PATH` 的 shell 中直接跑 CTest：

```
1/3 Test #1: test_smoke .........Exit code 0xc0000135***Exception:   3.04 sec
2/3 Test #2: test_state_machine .Exit code 0xc0000135***Exception:   1.02 sec
3/3 Test #3: test_line_table ....Exit code 0xc0000135***Exception:   0.99 sec
0% tests passed, 3 tests failed out of 3
```

### 根因

`0xC0000135` = **`STATUS_DLL_NOT_FOUND`**：测试可执行文件依赖 `Qt6Core.dll` 等 Qt 运行时，
而该 shell 的 `PATH` 中没有 `D:\Qt-debug\bin`，进程**在入口点之前**就被加载器终止。
CTest 把这类「加载失败」同样记为 `***Exception`，与真正的运行期崩溃在输出上难以区分。

### 解决

运行测试时显式注入 Qt `bin`（与 `TESTING.md` §5 的环境准备一致）：

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
$env:PATH = "D:\Qt-debug\bin;$env:PATH"
ctest --test-dir build -C Debug --output-on-failure --timeout 120
```

修复后 Debug / Release 均 **3/3 Passed**。

### 影响与关联文档

- **判据约定**：`0xC0000135` 先判为**环境问题（DLL 缺失）**，与 `0xC0000409` 那类运行期崩溃区别对待；
  排查顺序是 `PATH` / 部署目录 → 之后才怀疑代码。
- 关联：`README.md` §六（崩溃判定）、`TESTING.md` §5、`TRAP-P1-003`（windeployqt 部署）。

---
