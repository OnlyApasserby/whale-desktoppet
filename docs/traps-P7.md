# 踩坑记录 · P7（插件化智能桌宠与本地 Context API）

> 规范见 `docs/README.md` §二.5：每条记录必须**实际复现并排查过**，格式为
> **现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档**；
> 禁止编造未发生条目，问题解决前不得美化、删除或提前标记完成。
>
> 与 `ROADMAP-P7-Fin.md` 一一对应。崩溃类问题按 `docs/README.md` §六 处理：
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
| TRAP-P7-008 | 测试桩 MCP server 用 `QFile(FILE*)` 读 stdin，在 `QProcess` 管道下子进程完全不可用 → 握手全部超时 | **测试桩缺陷（P7.4，端到端测试暴露）** | 已解决 |
| TRAP-P7-009 | 用 `signals` 作为变量名（Qt 关键字宏 `#define signals public`）→ 编译期大量 `语法错误: "public"` 且定位误导 | **编译期（P7.6，Qt 宏污染）** | 已解决 |
| TRAP-P7-010 | 桥接把**裸 JSON 载荷**写入命名管道，而管道对端 `StdioTransport` 只认 `Content-Length` 分帧 → 对端死等头部，表现为「无响应」 | **协议缺陷（P7.2，端到端排查）** | 已解决 |
| TRAP-P7-011 | 桥接用 `std::fread(buf, 1, 4096, stdin)` 读 stdin，MCP 一问一答永远凑不满 4096 字节 → **永久阻塞**，stderr 全空 | **阻塞缺陷（P7.2）** | 已解决 |
| TRAP-P7-012 | 单测 `QLocalSocket::connectToServer()` 后直接 `exec()` 等 `connected` 信号，而连接常**同步**完成、信号早于 `exec()` 发出 → 每个用例白等 5s 超时 | **测试缺陷（P7.2）** | 已解决 |

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
  `docs/PLUGIN-ARCHITECTURE.md` §6.2（工作态优先级）、`docs/ROADMAP-P7-Fin.md` P7.1（验收与调参）。

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
  `docs/ROADMAP-P7-Fin.md` P7.1（「钩子优先、失败降级」这一承诺必须由本用例守住）；
  `docs/CONTEXT-API.md` §5（输入采集实现口径）。

---

### TRAP-P7-008：测试桩 MCP server 用 `QFile(FILE*)` 读 stdin，子进程在管道下完全不可用

- **现象（可复现步骤 / 报错原文）**：P7.4 新增的 `test_process_plugin` **5 个用例全失败**，
  共同表现是「握手拿不到响应」——`ProcessPluginLoader::start()` 返回 0（期望 1），
  子进程以 `CrashExit` 结束。复现：构建 Debug 后运行
  `build\Debug\test_process_plugin.exe -o <file>,txt`。关键日志（逐字）：
  ```
  QWARN  : ProcessPluginTest::loaderStartsAndDiscoversCapabilities() [McpPluginSession] initialize 失败: "t" "等待外部插件响应超时（3000 ms）"
  QWARN  : ProcessPluginTest::loaderStartsAndDiscoversCapabilities() [McpStdioClient] 子进程未响应 terminate，强制结束: "...\mcp_test_server.exe"
  QINFO  : ProcessPluginTest::loaderStartsAndDiscoversCapabilities() [McpStdioClient] 子进程退出: "...\mcp_test_server.exe" exitCode = 62097 exitStatus = 1
  FAIL!  : ProcessPluginLoader::start(registry) ... Actual (0) Expected (1)
  Totals: 3 passed, 5 failed, 0 skipped, 0 blacklisted, 17385ms
  ```
  （17.4s ≈ 5 × 3s 握手超时 + 各自 terminate 等待，与「每次都在等同一个超时」一致。）
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug，
  二进制 `build\Debug\test_process_plugin.exe` 与被测子进程 `build\Debug\mcp_test_server.exe`；
  子进程为**控制台**程序（`qt_add_executable` 未加 `WIN32`），父子通过 `QProcess` 的 stdio 管道通信。
- **根因**：测试桩 server 的 I/O 层用了
  `QFile in; in.open(stdin, QIODevice::ReadOnly)`（Qt 的 `FILE*` 构造）并配合
  `QCoreApplication` 事件循环前的阻塞读取；在 `QProcess` 的**匿名管道**（不可 seek）上该路径不可用——
  子进程既未读到请求，也未写回响应，只能被父进程超时后 kill。
  **已验证**：同一份 server 逻辑，仅把 I/O 换成标准 C stdio（`fread`/`fwrite` + `_setmode(_O_BINARY)`）
  并去掉 `QCoreApplication` 后，6 个用例（Totals 8，含 init/cleanup）全部通过。
  **推测（未逐行隔离复现）**：`QFile` 对 `FILE*` 句柄会做可用于文件的假设（如 `fstat`/`ftell`/seek 前置），
  在管道上返回异常后行为未定义；具体失败点未做最小隔离实验，故不作为结论。
- **解决或规避**：`tests/mcp_test_server.cpp` 的帧读写改为标准 C stdio（二进制模式），
  不再依赖 Qt 设备层；`QJsonDocument`/`QByteArray` 仍用于解析与组装（不涉及 I/O）。
  修复后 Debug / Release `ctest` 各 **20/20 通过**。
- **影响与关联文档**：`tests/mcp_test_server.cpp`；`docs/ROADMAP-P7-Fin.md` P7.4（验收与验证记录）；
  `docs/PLUGIN-ARCHITECTURE.md` §4.2（外部进程插件的 stdio 约定）。
  **回灌规则**：任何「以 stdio 管道与外部进程通信」的测试桩或桥接程序，
  其 I/O 一律走标准 C stdio 二进制模式，**不得**用 `QFile(FILE*)` 包装 stdin/stdout。

---

### TRAP-P7-009：`signals` 是 Qt 关键字宏，用作变量名导致「语法错误: public」

- **现象（可复现步骤 / 报错原文）**：`tests/test_acp_event_mapper.cpp` 编译失败，报错全部指向
  **无关位置**——`for` / `if` 语句处，且报的是关键字 `public`。复现：`cmake --build build --config Debug --target test_acp_event_mapper`。
  报错原文（逐字，节选）：
  ```
  tests\test_acp_event_mapper.cpp(49,23): error C2059: 语法错误:“public”
  tests\test_acp_event_mapper.cpp(57,13): error C2059: 语法错误:“public”
  tests\test_acp_event_mapper.cpp(63,46): error C2143: 语法错误: 缺少“)”(在“public”的前面)
  tests\test_acp_event_mapper.cpp(64,1): error C2447: “{”: 缺少函数标题(是否是老式的形式表?)
  ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug，
  目标 `build\Debug\test_acp_event_mapper.exe`。
- **根因**：Qt 为「信号 / 槽」定义了**关键字宏**（`qobjectdefs.h`）：
  `signals` → `public`、`slots` → 空、`emit` → 空。
  测试里把 `QList<CoreSignal> signals;` 用作**变量名**，预处理后变成 `QList<CoreSignal> public;`，
  于是 `for (const CoreSignal &signal : signals)` 展开为 `... : public)` ——
  编译器在范围 for 处看到 `public`，报「语法错误: public」，而行号指向**使用点**而非定义点，极难定位。
  （首轮误判为 `QVector` 模板问题，改为 `QList` + 输出参数后错误**完全不变**，正是本陷阱的特征。）
- **解决或规避**：把该变量改名为 `mapped`（避开全部 Qt 关键字宏）。修复后 Debug / Release
  `ctest` 各 **21/21 通过**。
- **影响与关联文档**：`tests/test_acp_event_mapper.cpp`。
  **回灌规则**：Qt 项目中**禁止**把 `signals` / `slots` / `emit` / `foreach` 等 Qt 关键字宏
  用作标识符；遇到「语法错误: public」且指向 `for` / `if` 时，**优先检查标识符是否撞宏**。

---

### TRAP-P7-010：桥接把裸 JSON 写入命名管道，对端 `StdioTransport` 只认分帧 → 无响应

- **现象（可复现步骤 / 报错原文）**：`whalepet-mcp.exe`（P7.2 桥接）启动后**不产生任何响应**，
  stderr 为空、进程常驻不退出，看上去像「卡死」。复现：主程序勾选「本地 Context API」后运行桥接，
  向其 stdin 写入一帧 `Content-Length` 包裹的 `initialize`，stdout 无任何输出。
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug，
  二进制 `build\Debug\whalepet-mcp.exe`；宿主为进程内 `LocalPipeTransport`（`QLocalServer`）。
- **根因**：桥接从 stdin **解开** `Content-Length` 帧后，把帧内**裸 JSON 载荷**直接写进命名管道；
  而管道对端是 `StdioTransport`，其分帧解析器**只认** `Content-Length: N\r\n\r\n{...}`——
  收到裸 JSON 后一直等待头部，于是既不分发也不报错，表现为静默死锁。
  与「写 stdio 必须分帧」是同一件事，**两条边路都要分帧**。
- **解决或规避**：桥接新增 `makeFrame()`，**写管道前重新按 `Content-Length` 分帧**；
  修复后 `test_context_pipe` 8 个用例全绿，全量 CTest **24/24 passed**（Debug / Release）。
- **影响与关联文档**：`src/app/mcp_bridge_main.cpp`、`src/contextapi/transport/StdioTransport.cpp`（分帧）。
  **回灌规则**：任何把字节**转发**给 `StdioTransport` 的中间层，都必须**重新分帧**；
  排障时先确认「对端到底收到的是分帧还是裸载荷」——此前已用临时诊断确认对端收到 **88 字节裸载荷**。

### TRAP-P7-011：`std::fread` 读 stdin / 管道会阻塞到读满请求字节数 → 永久死锁

- **现象（可复现步骤 / 报错原文）**：桥接进程在写入第一帧后**再无动静**，stderr 完全为空；
  临时探针显示 stdin **确有字节到达**，但进程停在读取处不前进。
- **环境**：同 TRAP-P7-010（Debug，`build\Debug\whalepet-mcp.exe`，Windows 命名管道 + 控制台 stdin）。
- **根因**：C 运行时的 `std::fread(buf, 1, 4096, stdin)` 语义是「**读到 4096 字节或 EOF** 才返回」；
  MCP 是「一问一答」，客户端只发一帧（如 64 字节）就等响应，**永远不会凑满 4096 字节**，
  于是 `fread` 永久阻塞（探针实测：请求 64 字节正常返回，请求 4096 字节永久挂起）。
  管道 / 控制台在无更多数据时会阻塞，故这不是「读空就返回」的场景。
- **解决或规避**：改用 `_read`（MSVC）/ `read`（POSIX），返回**当前已到达**的字节数
  （`readStdinChunk()`）；修复后桥接可即时拿到请求并转发。
- **影响与关联文档**：`src/app/mcp_bridge_main.cpp`。
  **回灌规则**：管道 / 控制台的「读一批可用字节」**禁止**用 `std::fread`；
  用 `_read` / `read`，或先 `PeekNamedPipe` 探明可读字节数再读。

### TRAP-P7-012：单测 `connectToServer()` 后同步等信号 → 每个用例白等 5s

- **现象（可复现步骤 / 报错原文）**：`test_context_pipe` 8 个用例**全部通过**，但单个用例约 **5s**、
  全目标约 **21.5s**（接近每个用例都触发一次 5s 超时）。复现：`ctest --test-dir build -C Debug -R test_context_pipe -V`。
- **环境**：同 TRAP-P7-010（Debug）；被测对象为 `QLocalServer` / `QLocalSocket` 本机命名管道。
- **根因**：`PipeClient::connectTo()` 调 `QLocalSocket::connectToServer()` 后**无条件** `loop.exec()`
  等 `connected` 信号；而本机命名管道连接常**同步**完成，`connected` 信号在 `exec()` 之前就已发出，
  于是 `exec()` 一直等到兜底 `QTimer` 的 5s 超时——功能不受影响，但测试慢且掩盖真实等待。
- **解决或规避**：连接后**先查 `state() == QLocalSocket::ConnectedState`**，已连接则直接返回；
  否则再 `exec()` 等信号。修复后单个用例耗时降到约 **1.7s**（8 用例）。
- **影响与关联文档**：`tests/test_context_pipe.cpp`。
  **回灌规则**：Qt 异步 API 等待完成时，先判断「是否已同步完成」，再决定是否进事件循环。

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
