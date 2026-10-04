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
| TRAP-P7-013 | HTTP 通道「token 为空即不校验」+ 不校验 `Origin`/`Content-Type` ⇒ 浏览器可对本机端口发起跨站 JSON-RPC 调用 | **安全缺陷（SECURITY-REVIEW.md #1）** | 已解决 |
| TRAP-P7-014 | `std::numeric_limits<T>::max()` 被 `windows.h` 的 `max` 宏替换 → C2589「`::` 右边的非法标记」 | **编译期（Windows 宏污染）** | 已解决 |
| TRAP-P7-015 | JSON 数字在 `QJsonDocument` 里一律是 `double`：**小数静默截断**、`1e30` 触发 `double`→`uint64` **未定义行为** | **数值边界缺陷（profile 加载）** | 已解决 |
| TRAP-P7-016 | 桥接 socket 用「单次 `waitForReadyRead(1500)` 未超时就继续」的循环 ⇒ 每 100ms 发 1 字节的流**永不退出**（永久阻塞 GUI 线程） | **阻塞缺陷（桥接输入）** | 已解决 |
| TRAP-P7-017 | `QWebSocket` 需真实 HTTP Upgrade 握手：单测用裸 `QTcpServer` 冒充 CDP 端点 ⇒ 握手本身超时，测不到「连上之后」的断连/超时路径 | **测试缺陷（CDP 生命周期）** | 已解决 |
| TRAP-P7-018 | 假 TCP 服务与被测代码同线程 ⇒ 被测的阻塞读饿死服务端事件循环（`waitForReadyRead` 期间无人喂数据） | **测试缺陷（阻塞被测对象）** | 已解决 |
| TRAP-P7-019 | 指针链 `staticRoot + offset` / `pointer + offset` 无溢出检查 ⇒ 野指针加偏移绕回低地址，读到**无关内存** | **地址边界缺陷（只读内存链路）** | 已解决 |
| TRAP-P7-020 | `static_cast<long long>(NaN/±Inf/1e300)` 是 C++ **未定义行为**（MSVC C4244） | **数值边界缺陷（float/double 字段）** | 已解决 |

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

### TRAP-P7-013：本地 Context API 的 HTTP 通道可被浏览器跨站调用

- **现象（可复现步骤）**：
  1. 用浏览器打开任意第三方页面（无需与 WhalePet 有任何关系）。
  2. 页面内执行：
     ```js
     fetch('http://127.0.0.1:<port>/rpc', {
       method: 'POST',
       headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
       body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'context.snapshot' })
     })
     ```
  3. 服务端**照常执行**并返回 200 与完整 `ContextSnapshot`（含前台窗口标题、
     应用名、空闲时长、会话统计等隐私数据）；对**有副作用**的能力同样可被触发。
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，配置 Debug/Release，
  受影响文件 `src/contextapi/transport/LocalHttpTransport.{h,cpp}`。
- **根因**：两条独立的缺陷叠加。
  1. `handleRequestLine` 里 `if (!m_token.isEmpty()) { …校验… }`——
     **token 为空即完全跳过认证**。而 `context_api_token` 默认为空串，
     于是「开启 Context API」= 「对全世界的网页开放一次 JSON-RPC 工具调用」。
     开发者把「仅监听 127.0.0.1」当成了授权机制，但**回环绑定不是授权**：
     浏览器可以向任意本机端口发请求。
  2. 既不校验 `Origin`、也不限制 `Content-Type`。而 `text/plain` /
     `application/x-www-form-urlencoded` 属 CORS **简单请求**，浏览器**不触发预检**、
     不管服务端是否回了 CORS 头都会把请求发出去（只是读不到响应）——
     对「只写不读」的攻击（如有副作用的工具调用）足够了。
  3. 附带：单连接缓冲、并发连接数、单请求等待时长**均无上限**，
     慢速客户端可长期占用描述符与内存。
- **解决或规避**：改为**三层纵深防御**（`docs/CONTEXT-API.md` §5.1）：
  1. `LocalHttpTransport::start()` 在 `m_token.trimmed().isEmpty()` 时
     **直接返回 false**（fail closed，**根本不监听**）；`ContextApiService::start()`
     在无令牌时只启用命名管道通道（浏览器不可达 + Windows 命名管道 ACL 保护）。
     组合根 `PetWindow::setContextApiEnabled` 在「用户启用但未配置令牌」时用
     `ContextApiService::generateToken()`（256 bit CSPRNG → base64url）生成并落盘，
     避免功能形同虚设。
  2. `Content-Type` 必须 `application/json`（允许 `; charset=utf-8` 等参数与大小写变体），
     否则 `415`——`application/json` 会触发预检，而本通道**从不**回
     `Access-Control-Allow-*`，预检必然失败。
  3. 带 `Origin` 时必须同源同端口，否则 `403`；`Origin: null`、`file://`、userinfo、
     非回环主机、异端口全部拒绝。
  4. 令牌改**定长比较**（`secureEquals`，不因首个不同字节短路），避免计时侧信道。
  5. 补齐资源上限：请求头 16 KiB、正文 1 MiB、并发连接 32、单请求等待 10s
     （250ms 巡检，无需每连接一个定时器），超限即回错误并关闭连接。
- **影响与关联文档**：`docs/CONTEXT-API.md` §5.1 / §5.2、`docs/SETTINGS.md` §2；
  回归守卫 `tests/test_context_http_security.cpp`（11 例，含**有副作用假工具**
  `ext.test.sideEffect` 的「调用次数保持 0」断言）与
  `tests/test_context_dispatch.cpp::httpChannelRefusesToStartWithoutToken`。
  **既有测试必须同步更新**：`serviceIsOffByDefaultAndGatedOnDemand` 与
  `httpChannelAnswersJsonRpc` 原先依赖「无 token 也能起 HTTP」，已按新策略改造
  （并新增「无 token ⇒ 只监听管道」的显式断言），非断言削弱。

### TRAP-P7-014：`windows.h` 的 `max` 宏污染 `std::numeric_limits<T>::max()`

- **现象（可复现步骤）**：单测里写 `std::numeric_limits<std::uint64_t>::max()`，
  编译报错（逐字）：
  ```
  tests\test_gamestate_boundaries.cpp(1419,68): error C2589: “(”:“::”右边的非法标记
  tests\test_gamestate_boundaries.cpp(1419,68): error C2059: 语法错误:“)”
  ```
- **环境**：Qt 6.8.4（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake 4.4.2，Debug。
- **根因**：`windows.h` 定义了对象式宏 `#define max(a,b) …`；预处理器把
  `::max()` 里的 `max` 也替换掉，展开成 `::(((a) > (b)) ? …)`，于是 `::` 后面
  成了非法标记。与 TRAP-P7-009（`signals` 宏）**同源**：Windows 头里的宏会污染
  看起来像标准库标识符的名字。
- **解决或规避**：写成 `(std::numeric_limits<std::uint64_t>::max)()`——
  多一层括号让预处理器认出它不是函数式宏调用。同类名字（`min`/`near`/`far`/
  `interface`/`small`）一律同样处理。
- **影响与关联文档**：`tests/test_gamestate_boundaries.cpp`；与 TRAP-P7-009 同类。

### TRAP-P7-015：JSON 数字是 `double` ⇒ 小数静默截断、超范围值触发未定义行为

- **现象（可复现步骤）**：`ProfileLoader::loadFromJson` 处理
  `{"engine":"generic",…,"fields":[{"name":"hp","kind":"int32","chain":[1.5]}]}`
  竟然**加载成功**并把偏移写成 `1`（静默改写档案）；
  `chain: [1e30]` 与 `maxBytesPerRound: 1e30` 则进入
  `static_cast<std::uint64_t>(double)`——C++ 规定该转换在值域外是**未定义行为**。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2，Debug/Release；
  `src/gamestate/GameProfile.cpp`。
- **根因**：`QJsonDocument` 把所有 JSON 数字统一存成 `double`，
  原 `parseU64()` 只判了 `d < 0.0` 就直接转换，既没判「是不是整数」，
  也没判「是否还在 `uint64` 可表示范围内」；`maxBytesPerRound` 与
  `validation.maxJumps` 同理（后者用 `QJsonValue::toInt(4)`，超范围时**静默回落默认值**，
  等于悄悄改了链深度上限）。
- **解决或规避**：`parseU64` 增补「有限性（NaN 一并落负分支）→ 必须为整数
  （`d != std::floor(d)` 即拒）→ 小于 2^64（以 `2^64` 本身为拒界）」三道判定；
  `maxBytesPerRound` 与 `maxJumps` 增补整数性与上限校验，
  上限取 `kMaxProfileBytesPerRound = 1 MiB`（默认仅 4 KiB，留 250 倍余量）与
  `kMaxProfileJumps = 16`（真实档案 ≤ 4 级）。**越界一律拒绝，不静默回落默认值。**
- **影响与关联文档**：`src/gamestate/GameProfile.{h,cpp}`；
  `tests/test_gamestate_boundaries.cpp` 的 `profileRejectsOutOfRangeNumbers`
  （含「上限边界本身允许」的正例，避免只测拒绝侧）。

### TRAP-P7-016：桥接 socket 的「单次未超时就继续」循环可被永久阻塞

- **现象（可复现步骤）**：`bridge.path = "127.0.0.1:<port>"`，让对端**每 100ms 发 1 字节**
  且永不发 `\n`。`RpgMakerBridgeAdapter::read()` 永不返回——
  原循环是 `while (!buffer.contains('\n') && socket.waitForReadyRead(1500)) { buffer += readAll(); }`，
  每轮「等不到数据就当失败退出」，但外层只在**拿到换行**时才结束，
  于是 100ms < 1500ms 的慢速流让每轮都"成功超时"并继续，**总时长无上限**，
  GUI 线程被一个用户自备脚本永久占住。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；`src/gamestate/RpgMakerBridgeAdapter.cpp`。
- **根因**：只约束了**单次**等待，没约束**总量**（字节数与总时长）；
  文件侧同样是无界 `file.readAll()`。
- **解决或规避**：新增三个常量（`RpgMakerBridgeAdapter.h`，可被单测直接核验）：
  `kBridgeMaxSnapshotBytes = 1 MiB`、`kBridgeSocketReadTimeoutMs = 1500`、
  **`kBridgeSocketTotalTimeoutMs = 4000`**。读循环改为「按剩余总时长切片
  `waitForReadyRead`」，字节累积超限即 `abort()`；文件侧改为**先看 `file.size()`
  再读，且多读 1 字节**识别「读取期间被替换/追加」。所有失败路径显式 `abort()`。
- **影响与关联文档**：`src/gamestate/RpgMakerBridgeAdapter.{h,cpp}`；
  `tests/test_gamestate_boundaries.cpp` 的 `bridgeSocketTimesOutOnSlowDripAndCloses`
  （断言耗时落在 `[总时长上限, 总时长上限+4s]` 区间）与
  `bridgeSocketRejectsUnterminatedOversizedStream`（断言**字节上限先于总时长生效**）。

### TRAP-P7-017：裸 `QTcpServer` 冒充 CDP 端点 ⇒ 握手就超时，测不到真正的路径

- **现象（可复现步骤）**：用 `QTcpServer` 起一个「连上就永不回应」的端点，
  期望验证 `CdpWebSocketClient` 的「求值超时 → 反复重连不留悬挂请求」，
  结果 `connectToUrl()` 直接失败：
  `CDP WebSocket 连接失败：超时或地址无效`。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；`tests/test_gamestate_boundaries.cpp`。
- **根因**：`CdpWebSocketClient` 用的是 **`QWebSocket`**，它必须先完成
  HTTP `Upgrade` 握手；裸 TCP 服务端永远不回握手报文，于是**连接阶段**就超时，
  根本走不到「已连接 → 求值超时」那段逻辑。测试前提错误，不是被测代码有问题。
- **解决或规避**：改用同线程的 `QWebSocketServer`（`NonSecureMode`），
  在 `newConnection` 里按用例脚本决定「永不回应」或「握手后立刻 `close()`」。
  **注意**：`CdpWebSocketClient::connectToUrl` / `evaluate` 内部跑的是**嵌套**
  `QEventLoop`，所以同线程的 `QWebSocketServer` 会被正常驱动，**不需要**额外线程。
- **影响与关联文档**：`tests/test_gamestate_boundaries.cpp` 的
  `cdpReconnectAfterTimeoutLeavesNoActiveConnection`；与 TRAP-P7-012 同源
  （「同步完成的信号」/「事件循环归属」类误判）。

### TRAP-P7-018：假 TCP 服务与被测代码同线程 ⇒ 被测的阻塞读饿死服务端

- **现象（可复现步骤）**：想让假 socket 服务「分多次、每次间隔一段时间」发数据，
  于是和被测适配器放在同一个线程：被测侧 `waitForReadyRead()` 阻塞 →
  该线程的事件循环停摆 → 服务端的 `QTcpServer` **永远不派发** `newConnection` /
  `readyRead` → 表现为「连接失败」，看起来像被测代码有 bug。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；`tests/test_gamestate_boundaries.cpp`。
- **根因**：Qt 的事件驱动只在**事件循环运行时**才推进。`waitForReadyRead` /
  `waitForConnected` 是**阻塞**调用（内部只跑 socket 自己的等待，不跑用户的
  事件循环），因此同线程的其它 `QObject` 在此期间完全收不到事件。
  这与 `tests/test_context_pipe.cpp` 里 `PipeClient` 顶部注释、
  以及 `docs/traps-P7.md` TRAP-P7-012 是**同一条约定**的另一面。
- **解决或规避**：**要喂数据的假服务必须放到独立线程**（本文件里的 `ScriptedServer`：
  `QThread::run()` 里建 `QTcpServer` + `QEventLoop`，端口用「轮询到非 0」等待就绪）。
  反之，被测代码内部跑**嵌套事件循环**的（如 `CdpWebSocketClient`），
  假服务**可以**留在同线程。
- **影响与关联文档**：`tests/test_gamestate_boundaries.cpp` 的 `ScriptedServer`。

### TRAP-P7-019：指针链地址加法无溢出检查 ⇒ 野指针 + 偏移绕回，读到无关内存

- **现象**：`chain = [0x100, 0x200]` 且第一跳指针为 `0xFFFFFFFFFFFFFF00` 时，
  `address = pointer + chain[i]` **回绕**成 `0x100`，后续读取的是模块起始处
  ——一段**与该字段毫无关系**的内存。同理 `staticRoot + chain[0]` 与
  `ChainSampler` 的 `moduleBase + moduleBaseOffset` 均可回绕。
- **环境**：Qt 6.8.4 + MSVC + CMake 4.4.2；
  `src/gamestate/PointerChainResolver.cpp`、`src/gamestate/ChainSampler.cpp`。
- **根因**：无符号整数加法回绕是**定义良好**的行为（不报错、不崩），
  因此这类缺陷不会以「异常」形式暴露，只会静默返回错误数据——
  恰恰违背 `docs/ROADMAP-ex1.md` §4.3「异常即退回，不伪造」。
- **解决或规避**：新增 `addAddress(base, offset, out, error, what)`，
  以 `base > UINT64_MAX - offset` 判溢出并**明确失败**；
  `resolve()`（静态根 + 每一跳）、`verifyMagic()`（`base + magicOffset`）、
  `ChainSampler::sample()`（`moduleBase + moduleBaseOffset`）全部改用它。
  `readRaw()` 另加「拒绝读空地址」与「预算比较改用减法形式」（`已用 + 请求` 本身也会溢出）。
- **影响与关联文档**：`src/gamestate/**`；
  `tests/test_gamestate_boundaries.cpp` 用「记录每次被读地址」的假读取器
  （`RecordingMemoryReader::reads()`）**断言溢出时一次读取都没发起**，
  而不是只断言返回 false。

### TRAP-P7-020：`static_cast<long long>` 转换 NaN/±Inf 是未定义行为

- **现象（可复现步骤）**：让假内存里某 `float` 字段读到 `quiet_NaN()`，
  `readField()` 走到 `value.integer = static_cast<long long>(raw);`——
  MSVC 报 C4244（`'float': conversion from 'double' to 'long long'`，possible loss of data），
  而按 C++ 标准，浮点转整数在**值域外或 NaN** 时是**未定义行为**
  （x86 上通常得到 `0x8000000000000000`）。
- **环境**：Qt 6.8.4 + MSVC（VS 18 2026）+ CMake 4.4.2；
  `src/gamestate/PointerChainResolver.cpp` 的 `GameFieldKind::Float` / `Double` 分支。
- **根因**：只判了「读取是否成功」，没判**读到的数值是否可表示为整数**。
  游戏内存里的字段值来自目标进程，**不可信**。
- **解决或规避**：新增 `toInteger(raw, out, error, what)`，先判
  `std::isnan` / `std::isinf`，再判是否落在 `(-2^63, 2^63)` 开区间内，
  **不合格即明确失败**（不写半成品结果）。正常负小数（如 `-1234.75`）
  仍能正确截断为 `-1234`，有正例覆盖。
- **影响与关联文档**：`src/gamestate/PointerChainResolver.cpp`；
  `tests/test_gamestate_boundaries.cpp` 的 `resolverRejectsNonFiniteAndOutOfRangeFloats`。

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
