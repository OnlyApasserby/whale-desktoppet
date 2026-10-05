# 锁屏被判成 `unknown` 而不是 `afk`（判定顺序缺陷）

> **原编号**：`TRAP-P7-006`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：P7.1 接入真实采集后，按下 `Win + L` 锁屏时桌宠不会进入「离开」表现。
  根因确认前的复现方式（把 `candidate()` 的顺序临时改回「先 `isEmpty()`」）实测失败原文（逐字）：
  ```
  FAIL!  : WorkStateTest::pausedSessionWinsOverMissingData() Compared values are not the same
     Actual   (static_cast<int>(stateOf(rules.candidate(locked)))): 0
     Expected (static_cast<int>(WorkState::Afk))                  : 9
  F:\develop\desktoppet\tests\test_work_state.cpp(286) : failure location
  Totals: 19 passed, 1 failed, 0 skipped, 0 blacklisted, 3ms
  ```
  同一缺陷在真实感知链路上的端到端表现（`test_win32_observer`）：
  ```
  FAIL!  : Win32ObserverTest::lockedSessionWithoutForegroundIsAfk() Compared values are not the same
     Actual   (static_cast<int>(rules.candidate(sample).state)): 0
     Expected (static_cast<int>(WorkState::Afk))               : 9
  F:\develop\desktoppet\tests\test_win32_observer.cpp(319) : failure location
  Totals: 12 passed, 1 failed, 0 skipped, 0 blacklisted, 3ms
  ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，
  配置 Debug，二进制路径 `build\Debug\test_work_state.exe` / `build\Debug\test_win32_observer.exe`
  （`QT_QPA_PLATFORM=offscreen`，用例用注入替身读数，不依赖真实锁屏）。
- **根因**：`WorkStateRules::candidate()` 原先**先**判 `sample.isEmpty()`（无数据 → `Unknown`），
  **后**判 `sample.systemPaused`（→ `Afk`）。而锁屏时 Win32 的读数恰好是
  「`GetForegroundWindow()` 返回 `NULL` → 无标题、无进程名、无输入」，
  即 `isEmpty() == true`；于是「主人明确离开了」这一**确定信息**被
  「无数据」抢先吞掉，降级成 `Unknown`——表现层看起来就和「没开感知」一模一样。
  P7.0 的普通单测全部构造了带 `appId` 的样本，所以这个顺序缺陷在空实现阶段**不可见**。
- **解决或规避**：把 `systemPaused` 分支提到 `isEmpty()` 之前（系统状态本身就是「有数据」），
  并在 `test_work_state::pausedSessionWinsOverMissingData` 与
  `test_win32_observer::lockedSessionWithoutForegroundIsAfk` 中同时固化「空样本 + `systemPaused`
  → `afk`」与「纯空样本 → `unknown`」两侧断言（后者是零回归红线）。
  修复后 Debug / Release `ctest` 各 **17/17 通过**。
- **影响与关联文档**：`src/core/WorkStateRules.cpp`（`candidate()` 顺序）；
  `docs/PLUGIN-ARCHITECTURE.md` §6.2（工作态优先级）、`docs/ROADMAP-P7-Fin.md` P7.1（验收与调参）。

---
