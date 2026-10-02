# 踩坑记录 · P7（插件化智能桌宠与本地 Context API）

> 规范见 `docs/README.md` §二.5：每条记录必须**实际复现并排查过**，格式为
> **现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档**；
> 禁止编造未发生条目，问题解决前不得美化、删除或提前标记完成。
>
> 与 `ROADMAP-P7.md` 一一对应。崩溃类问题按 `docs/README.md` §六 处理：
> **AI 不得自行排查崩溃**，必须立即停止编译/构建/测试与复现尝试，如实记录并交回用户。
> 本阶段**未出现崩溃**（无异常退出 / 访问违例），故无交回调试条目。

---

## 记录清单

| 编号 | 现象摘要 | 类别 | 状态 |
|---|---|---|---|
| TRAP-P7-001 | `emit` 作成员函数名 → Qt 关键字宏展开，`InvokeContext::emit` 解析崩坏并引发连锁报错 | 编译期 | 已解决 |
| TRAP-P7-002 | `QStringLiteral(常量指针)` → C2146 | 编译期 | 已解决 |
| TRAP-P7-003 | lambda **不能带默认参数** → 错误回调两参调用全部失败（C2064/C2664） | 编译期 | 已解决 |
| TRAP-P7-004 | `QVERIFY` 宏参数内含花括号初始化列表 / 多层模板 → moc `missing ')' in macro usage` | 构建期（moc） | 已解决 |
| TRAP-P7-005 | `SimpleCapability::invoke` 把「同步失败」透传为 `false`，被分发侧当作「异步已受理」→ 请求永久挂起 | **契约缺陷（运行时）** | 已解决 |
| TRAP-P7-006 | 判定顺序把「无数据」放在「会话暂停」之前：锁屏（前台窗口读不到）被判 `unknown` 而不是 `afk` | **判定缺陷（运行时，P7.1 接入真实采集后暴露）** | 已解决 |
| TRAP-P7-007 | 默认装配（真实读数）把 `m_useHooks` 置为 `false`，低层钩子永不安装 → 生产路径静默退化为差分降级 | **配置缺陷（P7.1，新单测发现）** | 已解决 |

---

### TRAP-P7-001：`emit` 不能用作成员函数名

- **现象**：`whalepet_plugin` 目标编译失败，报错原文（节选，逐字）：
  ```
  src\plugin\Capability.cpp(98,25): error C2589: “(”:“::”右边的非法标记
  src\plugin\Capability.cpp(113,21): error C2511: “void whalepet::plugin::InvokeContext::respond(const int)”
                                      :“whalepet::plugin::InvokeContext”中没有找到重载的成员函数
  src\plugin\Capability.h(79,73): error C2664: 无法从“unknown”转换为“const QJsonObject”
  ```
  复现步骤：直接 `cmake --build build --config Debug`（`whalepet_plugin` 为本阶段新增目标）。
- **根因**：`InvokeContext` 的私有方法命名为 `emit`。`emit` 是 Qt 的关键字宏（**展开为空**），
  于是声明 `void emit(QJsonObject response);` 变成 `void (QJsonObject response);`，
  类体解析在此崩坏；`InvokeContext::emit(...)` 退化为 `InvokeContext::(...)`（C2589）。
  后续关于 `QJsonObject` 的 “unknown” 报错都是这一次解析失败的**连锁反应**，不是头文件缺失。
- **解决或规避**：改名为 `deliver()`，并在头文件写下「不能命名为 emit」的注释；
  同时把 `respond/fail` 内部改为 `deliver(std::move(response))`。
- **影响与关联文档**：`src/plugin/Capability.{h,cpp}`；同类风险词还包括 `signals` / `slots` / `foreach`。

---

### TRAP-P7-002：`QStringLiteral` 只接受字面量

- **现象**：
  ```
  src\contextapi\builtin\ContextCapabilities.cpp(69,15): error C2146: 语法错误: 缺少“)”
      (在标识符“kContextCapabilitiesPluginId”的前面)
  src\contextapi\builtin\ContextCapabilities.cpp(69,15): error C2612: 基/成员初始值设定项列表中的非法后缀“)”
  ```
- **根因**：写了 `QStringLiteral(kContextCapabilitiesPluginId)`。`QStringLiteral` 是需要**字符串字面量**
  才能计算长度/类型的宏，不接受变量。
- **解决或规避**：改为 `QString::fromLatin1(kContextCapabilitiesPluginId)`；
  文件内其它「常量指针 → QString」的位置统一用 `fromLatin1`。
- **影响与关联文档**：`src/contextapi/builtin/ContextCapabilities.cpp`；
  `src/minigame/MiniGameCompatAdapter.cpp` 早已用 `QString::fromLatin1(kCapabilityPrefix)`（正确写法）。

---

### TRAP-P7-003：lambda 不能带默认参数 → 错误回调改用显式类型别名

- **现象**：
  ```
  src\contextapi\JsonRpcDispatcher.cpp(79,9): error C2064: 项不会计算为接受 2 个参数的函数
  src\contextapi\JsonRpcDispatcher.cpp(90,9): error C2064: 项不会计算为接受 3 个参数的函数
  ... (同类共 10 处)
  src\contextapi\JsonRpcDispatcher.cpp(201,9): error C2664: 无法将参数 4 从
      “const JsonRpcDispatcher::handle::<lambda_2>”转换为“const JsonRpcDispatcher::Responder &”
  ```
- **根因**：错误回调写成 `[](int code, const QString &msg, const QJsonObject &data = QJsonObject())`；
  **C++ 的 lambda 参数不允许默认实参**，故两参调用点全部无法编译。
  同时它在头文件里以 `const Responder &`（即 `std::function<void(const QJsonObject&)>`）传递，
  三参 lambda 无法转换（C2664）——两个错误同源：**一个类型别名承担了两种签名**。
- **解决或规避**：`JsonRpcDispatcher` 增加 `using ErrorResponder = std::function<void(int, const QString&, const QJsonObject&)>`；
  `invokeCapability` 的第 4 个参数改用 `ErrorResponder`；全部错误路径**显式传三个实参**
  （无 data 时传 `QJsonObject()`），不再依赖默认参数。
- **影响与关联文档**：`src/contextapi/JsonRpcDispatcher.{h,cpp}`。

---

### TRAP-P7-004：moc 无法解析断言宏内的花括号初始化列表

- **现象**：
  ```
  tests/test_plugin_registry.cpp(184:1): error: missing ')' in macro usage
  MSB8066: “...test_plugin_registry_autogen.vcxproj”的自定义生成已退出，代码为 1
  ```
  发生在 **AutoMoc** 阶段（编译器本身尚未运行），仅 `test_plugin_registry` 失败，其余目标正常。
- **根因**：在 `QVERIFY(...)` 内直接写了 `std::vector<std::pair<QString, PluginOrigin>>{...}`
  这类「花括号初始化列表 + 多层模板」表达式；moc 的轻量预解析对宏参数中的这种写法匹配括号失败。
- **解决或规避**：把复杂表达式移出断言宏——先落到局部变量或经小工具函数（`caps({...})`）构造，
  宏内只保留简单调用（`QVERIFY(added)`）。此约定写入测试文件顶部注释，供后续新增测试沿用。
- **影响与关联文档**：`tests/test_plugin_registry.cpp`；`docs/TESTING.md` 的测试编写约定。

---

### TRAP-P7-005：同步能力失败被误判为「异步已受理」（契约缺陷）

- **现象**：`test_plugin_registry::invokeRoutesAndReportsErrors` 失败，报错原文：
  ```
  FAIL!  : PluginRegistryTest::invokeRoutesAndReportsErrors()
     'registry.invoke(QStringLiteral("t.echo"), failParams, failCtx, failOut, failError)' returned FALSE.
  ```
  复现步骤：注册一个 `SimpleCapability` 子类，令其 `call()` 在收到 `{"fail": true}` 时填 error 并返回 `false`，
  然后调用 `CapabilityRegistry::invoke(...)`。
- **根因**：`ICapability::invoke` 的返回值语义是「**是否已同步完成**」：
  `true` = 已完成（成功见 `out`、失败见 `error`）；`false` = **异步已受理**。
  而 `SimpleCapability::invoke` 直接把 `call()` 的布尔值透传，于是「同步失败」被下游
  （`JsonRpcDispatcher` / 未来的通道）理解为「已受理，等回调」——**请求会永久挂起**，
  且不会有任何错误返回。这是接口适配层的语义错误，静态编译期无法发现。
- **解决或规避**：`SimpleCapability::invoke` 明确翻译为「同步」语义：
  ```cpp
  call(in, out, error);   // 结果（成功或失败）都写入 out / error
  return true;            // 对分发侧而言：本次调用已同步结束
  ```
  并把该契约写入 `Capability.h` 注释与 `docs/PLUGIN-ARCHITECTURE.md` §5，避免后续插件作者踩同样的坑。
- **影响与关联文档**：`src/plugin/Capability.cpp`、`src/plugin/Capability.h`；
  `docs/PLUGIN-ARCHITECTURE.md` §5；该缺陷由新增单测发现（**未放宽断言、未改用例**）。

---

### TRAP-P7-006：锁屏被判成 `unknown` 而不是 `afk`（判定顺序缺陷）

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
  `docs/PLUGIN-ARCHITECTURE.md` §6.2（工作态优先级）、`docs/ROADMAP-P7.md` P7.1（验收与调参）。

---

### TRAP-P7-007：默认装配误关低层钩子，生产路径静默退化为差分降级

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
  `docs/ROADMAP-P7.md` P7.1（「钩子优先、失败降级」这一承诺必须由本用例守住）；
  `docs/CONTEXT-API.md` §5（输入采集实现口径）。

---

## 待登记模板（仅作格式示例，条目必须由真实问题产生后才可写入）

### TRAP-P7-XXX：<一句话现象>

- **现象（可复现步骤 / 报错原文）**：
  1. …
  - 报错原文（逐字）：
    ```
    …
    ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug/Release，二进制路径 …
- **根因**：
- **解决或规避**：
- **影响与关联文档**：
