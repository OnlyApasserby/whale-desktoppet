# 测试桩 MCP server 用 `QFile(FILE*)` 读 stdin，子进程在管道下完全不可用

> **原编号**：`TRAP-P7-008`　**阶段**：P7　**来源**：原按阶段聚合的 `traps-P7.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

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
