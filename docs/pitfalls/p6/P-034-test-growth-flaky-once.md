# `test_growth` 全量运行时一次性失败、无断言输出，随后不可复现

> **原编号**：`TRAP-P6-003`　**阶段**：P6　**来源**：原按阶段聚合的 `traps-P6.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：测试 / 时序 ｜ **影响**：P6 首次 Debug 全量 `ctest` 时 `test_growth` 判定 Failed，
但**无任何 QTest 断言文本**；随后同二进制单独运行、以及连续 3 次全量运行**均通过**。

### 现象

```powershell
cd f:/develop/desktoppet
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Debug --output-on-failure --timeout 120
```

- 首次：`5 - test_growth ... ***Failed 0.45 sec`，`1 tests failed out of 8`；
  尽管启用了 `-o -,txt`，CTest 仍**未打印任何断言文本**（该失败不具备可诊断输出）。
- `ctest -R test_growth`（单独）→ **Passed**。
- 随后连续 **3 次**全量 `ctest -C Debug` → **每次 100% passed（8/8）**。
- 加入新增 `test_settings` 后，Debug / Release 全量（9 项）→ **均 100% passed**。

### 状态

**未定位 / 暂缓（不可复现）**。按 `README.md` §六，异常一律交回用户调试、AI 不自行插桩排查；
本项非崩溃（进程正常退出，仅测试判 Failed），且无输出，无法据此定位。

**已排除项（已验证）**：

- 与本次 P6 改动无关：本次未触碰 `GrowthService` / `core::GrowthRules` / `PetStateRepo`；
  改动集中在 `SettingsRepo`（`json_ext` 扩展键）、`PetStateMachine`（`night_quiet` 开关）、
  视图层（`PoseView` / `SpeechBubble` / `ContentPanel` / 新增 `SettingsDialog`）与 `PetWindow`。
- 非插件问题（已验证）：`build/Debug` 未执行过 `windeployqt`，Qt 回退到前缀
  `D:/Qt-debug/plugins`，`qoffscreend.dll` 可用——同一批次其余 GUI / 逻辑测试均正常。

**推测（未验证，仅供参考）**：可能与该用例内部的时间/临时目录时序有关，但**未取得证据**，不作结论。

### 影响与关联文档

- 关联：`docs/TESTING.md`（`test_growth`）、`docs/BUILD.md` §9。
- 与 `TRAP-P2-007` 同类：**未复现的长期观察项**，不阻塞 P6 验收。若再次出现且**带断言输出**，
  按 `docs/pitfalls/` 规范补记根因。

---
