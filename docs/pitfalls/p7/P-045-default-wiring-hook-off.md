# 默认装配误关低层钩子，生产路径静默退化为差分降级

> **原编号**：`TRAP-P7-007`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

- **现象**：默认（真实读数）装配出的观察者「倾向低层钩子」为 `false`，
  即**永远不安装** `WH_KEYBOARD_LL` / `WH_MOUSE_LL`，输入计数长期走
  `GetLastInputInfo` 差分降级路径。后果：每次采样至多计 1 个事件，10s 窗口内上限 10，
  而 Vibe Coding 判据要求 ≥20（`kVibeBurstMinEvents`）——**该状态在真实运行中永远不可达**，
  且表现为「能跑但错」（程序不报错、日志也只说降级，看起来像杀软拦截）。
  由新增单测暴露，失败原文（逐字，日志器输出为 UTF-8 源码文本）：
  ```
  FAIL!  : Win32ObserverTest::defaultConfigurationPrefersLowLevelHooks() 'observer.hooksPreferred()' returned FALSE. (默认装配必须倾向低层钩子（真实读数路径不得退化为差分降级）
  F:\develop\desktoppet\tests\test_win32_observer.cpp(223) : failure location
  Totals: 13 passed, 1 failed, 0 skipped, 0 blacklisted, 2ms
  ```
  （`'...' returned FALSE.` 后的中文断言文本在 GBK 控制台下显示为乱码，属既有日志编码现象，
  不影响判定；以 `test_win32_observer -o <file>,txt` 输出为准。）
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，
  配置 Debug，二进制 `build\Debug\test_win32_observer.exe`，`QT_QPA_PLATFORM=offscreen`；
  用例只查「配置意图」，**不安装真实钩子**，因此可在 CI/headless 稳定复现。
- **根因**：`Win32ActivitySampler(Win32ActivityReader reader)` 在**初始化列表**里恒置
  `m_useHooks(false)`，本意是「注入替身读数时不要装系统钩子」；
  但 `Win32DesktopObserver()` 的默认构造委托到同一构造函数（传入三个空 reader = 真实读数），
  于是**真实读数路径也被判成替身**。判据本该是「reader 是否为空」，却写成了
  「走了这个构造函数就不装钩子」——语义错位。
- **解决或规避**：改为 `m_useHooks = !m_reader;`（空 reader = 真实读数 → 启用钩子；
  非空 = 替身 → 不装钩子）；并新增 `hooksPreferred()`（只反映**配置意图**、
  不需要安装动作，故可无副作用单测）与用例
  `test_win32_observer::defaultConfigurationPrefersLowLevelHooks`
  （同时校验「默认装配 = 倾向钩子」与「注入替身 = 不倾向钩子」两侧）。
  修复后 Debug / Release `ctest` 各 **17/17 通过**。
- **影响与关联文档**：`src/platform/Win32DesktopObserver.{h,cpp}`；
  `docs/ROADMAP-P7-Fin.md` P7.1（「钩子优先、失败降级」这一承诺必须由本用例守住）；
  `docs/CONTEXT-API.md` §5（输入采集实现口径）。

---
