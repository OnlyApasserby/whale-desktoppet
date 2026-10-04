# P7.2 / P7.3 交付核查报告（原「剩余接口」已清零）

> **文件名说明**：本文件原名「P7 剩余接口 · 配置核查报告」，用于跟踪 P7.2 / P7.3 两组**尚未交付**
> 的接口。二者已于 **2026-10-02** 交付，故正文改为**交付后核查**；文件名保留以不破坏既有交叉引用。
>
> **范围**：P7.2（MCP Server 侧命名管道通道 + `whalepet-mcp.exe` 控制台桥接）与
> P7.3（`plugins/` 动态 DLL 插件装载）。
> **方法**：源码 / CMake / 打包脚本 / 设置项**静态核对** + `test_context_pipe` / `test_dll_plugin`
> 自动化测试；构建与 CTest 结果见 `ROADMAP-P7-Fin.md`「验证记录（P7.2 / P7.3）」。
> **关联**：`ROADMAP-P7-Fin.md`、`CONTEXT-API.md` §4、`PLUGIN-ARCHITECTURE.md` §4.1、
> `packages.md` §2 / §2.1 / §8、`SETTINGS.md` §7、`traps-P7.md` TRAP-P7-010~012。

---

## 1. 结论摘要

| 接口 | 交付物 | 代码实现 | 配置接线 | 打包/分发 | 测试 | 运行期可用 |
|---|---|---|---|---|---|---|
| **P7.2** | 本地回环 HTTP 通道 | ✅ 已实现 | ✅ 已接线 | —（不落文件） | ✅ `test_context_dispatch` | ✅ 勾选即启动 |
| **P7.2** | `context_api_enabled` / `_port` / `_token` 设置项 | ✅ | ✅ | — | ✅ | ✅ |
| **P7.2** | 命名管道通道（`QLocalServer`） | ✅ 已实现 | ✅ 已接线 | —（内核对象） | ✅ `test_context_pipe` | ✅ 与 HTTP 同开关 |
| **P7.2** | `whalepet-mcp.exe` 桥接 exe | ✅ 已实现 | ✅ | ✅ 安装/卸载清单已同步 | ✅ `test_context_pipe`（真实进程端到端） | ✅ |
| **P7.3** | `DllPluginLoader`（元数据 / ABI 协商 / 降级） | ✅ 已实现 | ✅ 已接线（`PetWindow::setupDllPlugins`） | — | ✅ `test_dll_plugin` | ✅ |
| **P7.3** | `plugins/` 目录扫描 | ✅ | ✅ `<applicationDirPath>/plugins` | ✅ 不随包分发（/x 排除 + 非递归卸载兜底） | ✅ | ✅ |
| **P7.3** | 示例插件（`ext.hello` / 负例 `ext_badabi`） | ✅ | —（构建期） | ✅ 仅供测试、不随包分发 | ✅ | —（测试用） |

**总体判断**：原报告登记的**两组剩余接口均已清零**，P7.0–P7.6 全部交付，CTest **24/24 passed**
（Debug / Release）。两组新通道 / 装载器**均不影响默认运行行为**——默认配置
（`context_api_enabled = false`、无 `plugins.json`、无 `plugins/` 目录）下不监听管道、不扫描目录。

---

## 2. P7.2 逐项核查（已交付）

`ROADMAP-P7-Fin.md` 对 P7.2 的验收要求：
> 设置项真实生效（HTTP 回环监听 + 命名管道）；`whalepet-mcp.exe` 控制台桥接（stdio ↔ 本地通道）；
> `initialize` / `tools/list` / `tools/call` 全链路。

| # | 项 | 事实 | 证据 |
|---|---|---|---|
| 1 | HTTP 回环监听 | ✅ 只绑 `QHostAddress::LocalHost` | `src/contextapi/transport/LocalHttpTransport.cpp` |
| 2 | 端口 0 = 系统分配 | ✅ 生效 | `LocalHttpTransport::port()`、`PetWindow` `started(port)` 日志 |
| 3 | token 校验（`X-WhalePet-Token`） | ✅ HTTP 401 + `kRpcErrorUnauthorized` | `LocalHttpTransport.cpp` |
| 4 | token 校验（MCP `initialize.params.token`） | ✅ 已实现（stdio / 管道共用） | `src/contextapi/transport/StdioTransport.cpp` |
| 5 | `StdioTransport`（MCP Server 侧） | ✅ 以「两个 `QIODevice`」构造，可绑定任意对 | `tests/test_context_dispatch.cpp` |
| 6 | **`StdioTransport` 的运行期载体** | ✅ **命名管道**：每条连接 new 一个 `StdioTransport` 并 `bind(socket, socket)` | `LocalPipeTransport::onNewConnection` |
| 7 | 命名管道（`QLocalServer` / `QLocalSocket`） | ✅ 已实现；启动前 `removeServer` 清理残留名 | `src/contextapi/transport/LocalPipeTransport.{h,cpp}` |
| 8 | 管道名唯一约定源 | ✅ `kDefaultContextPipeName = "whalepet-context-v1"`（两侧共用） | `LocalPipeTransport.h:31`、`mcp_bridge_main.cpp`、`packages.md` §2.1 |
| 9 | **`whalepet-mcp.exe` 桥接目标** | ✅ CMake 目标存在，**控制台子系统**（故意不加 `WIN32`），产物落 `dist/WhalePet` / `deploy-release` | `cmake/Executables.cmake`、`src/app/mcp_bridge_main.cpp` |
| 10 | 桥接语义 | ✅ 只做字节转发：stdin `Content-Length` 帧 → 管道帧；请求等一帧响应写回 stdout；通知不等待 | `mcp_bridge_main.cpp`（`takeFrame` / `makeFrame` / `readPipeFrame`） |
| 11 | 桥接鉴权 | ✅ `--token` 非空时注入 `initialize.params.token`（仅 initialize） | `injectTokenIntoInitialize` |
| 12 | 总开关语义 | ✅ **同一开关同时启停两通道**；管道启动失败即**回滚**已启动的 HTTP（原子） | `ContextApiService::start()` §`// P7.2` |
| 13 | 打包/分发 | ✅ 与 `WhalePet.exe` 同目录随包；安装 `File /r` 落入、卸载 `Delete` + `taskkill` 逐条对应 | `packaging/whalepet.nsi`、`packaging/make-package.ps1`、`packages.md` §2 / §5 |
| 14 | 测试覆盖 | ✅ `test_context_pipe`（8 用例）：管道承载完整 MCP 会话 / token 门控（`-32003`）/ 总开关同时启停 / **真实桥接进程端到端** | `tests/test_context_pipe.cpp`、`cmake/Tests.cmake` |

**结论**：P7.2 的**主通道（HTTP 回环）与第二通道（命名管道）均可用且门控正确**；
`whalepet-mcp.exe` 提供 MCP Server 的运行期真实进程中转，
「任一 MCP 客户端可列举并调用 `context.snapshot`」这条验收标准**成立**（端到端测试守卫）。

---

## 3. P7.3 逐项核查（已交付）

`ROADMAP-P7-Fin.md` 对 P7.3 的验收要求：
> `plugins/` 目录扫描装载、IID/`apiVersion` 协商、失败降级；示例插件（如 `ext.hello`）。
> 放入合法 DLL 后 `capabilities.list` 出现其能力；版本不匹配的 DLL 被跳过且主程序正常启动。

| # | 项 | 事实 | 证据 |
|---|---|---|---|
| 1 | `IPluginFactory` ABI 边界 | ✅ IID `ai.whalepet.PluginFactory/1.0`、`kPluginApiVersion = 1` | `src/plugin/dll/IPluginFactory.h` |
| 2 | 元数据 `apiVersion` 校验 | ✅ 缺失 / 高于宿主 → 跳过并记原因 | `DllPluginLoader::loadAll` |
| 3 | IID 不匹配 / 实例化失败降级 | ✅ `qobject_cast` 失败 → `unload()` + 跳过 | `DllPluginLoader.cpp` |
| 4 | `factory->apiVersion()` 二次协商 | ✅ | `DllPluginLoader.cpp` |
| 5 | 注册冲突处理（不静默） | ✅ 注册被拒 → `unload()` + 返回原因 | `DllPluginLoader.cpp` |
| 6 | `QPluginLoader` 保活（不析构卸载） | ✅ 存入 `m_loaders` | `DllPluginLoader.cpp` |
| 7 | **组合根接线** | ✅ `PetWindow::setupDllPlugins()` 构造并 `loadAll(m_plugins)`，且在**构建菜单之前**（菜单项由已装载插件动态生成） | `src/view/PetWindow.cpp:218, 464-475` |
| 8 | **`plugins/` 目录扫描** | ✅ 目录 = `QCoreApplication::applicationDirPath() + "/plugins"` | `PetWindow.cpp:470` |
| 9 | 目录约定位置 | ✅ 文档为 `<安装目录>/plugins/`，与组合根一致；缺失目录不报错 | `PLUGIN-ARCHITECTURE.md` §4.1、`packages.md` §8 |
| 10 | 打包脚本处理 | ✅ `plugins/` **不随包分发**：`File /r` 以 `/x` 排除；`make-package.ps1` 打包前清空 `dist` 内残留；卸载做**非递归** `RMDir` 兜底 | `packaging/whalepet.nsi`、`make-package.ps1`、`packages.md` §2 / §8 |
| 11 | 示例插件产物 | ✅ `ext_hello`（合法，注册 `ext.hello.greet`）与 `ext_badabi`（`apiVersion=99` 负例） | `src/plugin/examples/**`、`cmake/PluginExamples.cmake` |
| 12 | 测试覆盖 | ✅ `test_dll_plugin`：装载 / ABI 协商 / 失败降级 / 缺失目录与非法文件不报错 / 能力可见且可调用 | `tests/test_dll_plugin.cpp`、`cmake/Tests.cmake` |

**结论**：P7.3 的**加载器实现完整、降级路径齐备、已接入组合根、有目录扫描、有示例插件、有测试**——
「放入合法 DLL 后 `capabilities.list` 出现其能力」这条验收标准**成立**。

> ⚠️ **用户可见行为**：把合法 DLL 放进 `<安装目录>/plugins/` 后**即生效**；
> 不兼容 / 非法 DLL 被**跳过并记日志**，主程序照常启动（见 `packages.md` §8）。

---

## 4. 配置面交叉核查

### 4.1 CMake 目标与依赖

| 检查项 | 结果 |
|---|---|
| `whalepet_plugin` 是否编入 DLL 加载器 | ✅ `src/plugin/dll/IPluginFactory.h`、`DllPluginLoader.{h,cpp}` 在源列表内 |
| 命名管道所需 Qt 模块 | `Qt6::Network` 已随 `whalepet_contextapi` 链接（`QLocalServer` 在其中），**无需新增模块** |
| 桥接 exe 目标 | ✅ `qt_add_executable(whalepet-mcp …)`（`cmake/Executables.cmake`），链 `whalepet_contextapi` |
| 既有测试目标回归 | ✅ 无既有目标受影响；CTest **22 → 24** |

### 4.2 设置项（`settings.json_ext`）

`src/model/SettingsRepo.cpp` 实际键位与 `SETTINGS.md` §2/§7 一致（`context_api_enabled` /
`context_api_port` / `context_api_token` / `work_aware_enabled` / `acp_*`）。

**结论**：P7.2 / P7.3 **均不需要新设置项**——管道名与桥接 exe 属打包/约定范畴；
`plugins/` 为约定路径；外部进程插件配置走 `<数据目录>/plugins.json`（不是 `json_ext` 键）。
→ **设置项层：无改动**。

### 4.3 打包与分发

| 检查项 | 结果 |
|---|---|
| `engine/` 落点 | ✅ 免安装版由 `make-package.ps1` 建；安装版由 `whalepet.nsi` 安装 Section 建 + `icacls` 授权 + 卸载兜底 |
| `plugins/` 落点 | ✅ **不创建、不分发**；`File /r` 以 `/x` 排除，`make-package.ps1` 清空 `dist` 内残留；卸载非递归 `RMDir` 兜底（保护用户自装插件） |
| 桥接 exe | ✅ 与 `WhalePet.exe` 同目录随包；安装 `File /r` 落入、卸载 `Delete "$INSTDIR\whalepet-mcp.exe"` + 卸载开头 `taskkill`（客户端不关 stdin 时桥接会存活并占用映像） |
| 四处清单一致性 | ✅ `packages.md` §2 对应表 / §5 维护流程 / §8 插件约定 / `whalepet.nsi` 与 `make-package.ps1` 已同步（新增产物 → 安装 + 卸载 + 排除 + 授权四处对应） |

### 4.4 组合根（`PetWindow` 装配顺序）

`setupMiniGames → **setupDllPlugins（P7.3）** → setupContextMenu → setupTray → setupController →
setupGrowth → setupContent → setupStomach → setupChat → setupHotword → setupWorkState →
setupContextApi → setupAcp → setupSettings → setupRecallEntry`，随后 `setupProcessPlugins()`。

- ✅ 内置层（`BuiltinPluginLoader`）、**DLL 层（`setupDllPlugins`，P7.3）**、
  外部进程层（`setupProcessPlugins`，P7.4）三层装载器**均已装配**；
- ✅ DLL 层置于「构建菜单之前」，与内置层一致，保证插件贡献的菜单项可见。

---

## 5. 交付改动清单

### 5.1 P7.2（代码）

| 文件 | 改动 |
|---|---|
| `src/contextapi/transport/LocalPipeTransport.{h,cpp}` | **新增**：`QLocalServer` 监听、每连接复用 `StdioTransport`、`removeServer` 清理残留名、`connectionCount()` |
| `src/contextapi/transport/LocalPipeTransport.h` | 管道名唯一约定源 `kDefaultContextPipeName` |
| `src/contextapi/ContextApiService.{h,cpp}` | `m_pipe` 成员、`setPipeName` / `pipeListening` / `pipeName`；`start()` 一并启停两通道且**失败回滚**、`stop()` 一并停、`running()` 取两通道或 |
| `src/app/mcp_bridge_main.cpp` | **新增**：控制台桥接主程序（`takeFrame` / `makeFrame` / `readStdinChunk`（用 `_read`，非 `fread`）/ `injectTokenIntoInitialize` / `--pipe` / `--token` / `--help`） |
| `cmake/Executables.cmake` | 源列表加入 `LocalPipeTransport.*`；新增 `whalepet-mcp` 目标与产物目录 |
| `src/view/PetWindow.cpp` | `started(port)` 日志标注端口；`ContextApiService` 装配不变（管道随开关自动启停） |

### 5.2 P7.3（代码）

| 文件 | 改动 |
|---|---|
| `src/view/PetWindow.{h,cpp}` | 新增 `setupDllPlugins()` 与 `m_dllPlugins` 成员；装配链加入该调用 |
| `src/plugin/examples/hello/HelloPlugin.{h,cpp}` | **新增**：合法示例插件（`apiVersion=1`，注册 `ext.hello.greet`） |
| `src/plugin/examples/badabi/BadAbiPlugin.{h,cpp}` | **新增**：ABI 负例（`apiVersion=99`，应被跳过） |
| `cmake/PluginExamples.cmake` 与 `cmake/Tests.cmake` | `whalepet_ext_hello` / `whalepet_ext_badabi`（`MODULE`，`OUTPUT_NAME=ext_hello/ext_badabi`）与 `test_dll_plugin` 目标；`WIN32` 条件 |

### 5.3 打包

| 文件 | 改动 |
|---|---|
| `packaging/whalepet.nsi` | 新增 `APP_MCP_EXE` 定义；安装 Section `File /r` 增 `/x "plugins"`；卸载开头 `taskkill /IM whalepet-mcp.exe`；卸载 `Delete "$INSTDIR\whalepet-mcp.exe"`；`RMDir "$INSTDIR\plugins"`（非递归兜底） |
| `packaging/make-package.ps1` | 校验 `dist/WhalePet/whalepet-mcp.exe` 存在；`windeployqt` 对两个 exe 一并部署；打包前清空 `dist/WhalePet/plugins/` |

### 5.4 测试

| 文件 | 改动 |
|---|---|
| `tests/test_context_pipe.cpp` | **新增**（8 用例）：命名管道完整 MCP 会话 / token 门控 / 总开关同时启停 / **真实桥接进程端到端**（`WHALEPET_MCP_EXE` 宏指向构建产物） |
| `tests/test_dll_plugin.cpp` | **新增**：真实 DLL 装载 / ABI 协商 / 失败降级 / 缺失目录与非插件文件不报错 / 能力可见可调用 |
| `cmake/Tests.cmake` | 注册两目标并 `add_test`（TIMEOUT 120 / 60） |

### 5.5 文档

| 文件 | 改动 |
|---|---|
| `docs/ROADMAP-P7.md → ROADMAP-P7-Fin.md` | 由 P7.2 / P7.3 「待实施」改为「✅ 已完成」，补交付物 / 验收 / 踩坑 / 验证记录（24/24），并**按约定改名**为 `-Fin` |
| 本文件 | 由「剩余接口核查」改为「交付核查」（已交付证据 + 改动清单） |
| `docs/CONTEXT-API.md` | P7.2 命名管道 / 桥接 exe 交付状态、命名管道命名约定与门控 |
| `docs/packages.md` | §1 步骤与产物、§2 对应表（桥接 exe 行 / `plugins/` 行）、§2.1 命名管道命名约定、§5 排除项、§6.4 桥接人工验收、§7 已知限制、§8 插件接线 |
| `docs/PLUGIN-ARCHITECTURE.md` | DLL 层「规划 / 未接线」→「P7.3 已接线 + 示例插件 + 测试」 |
| `docs/SETTINGS.md` / `docs/ARCHITECTURE.md` | 命名管道由「规划中」改为「P7.2 已落地」 |
| `docs/README.md` / `docs/TESTING.md` / 根 `README.md` | 测试目标总数 22 → **24**；补 `test_context_pipe` / `test_dll_plugin` 两目标；索引改指 `ROADMAP-P7-Fin.md` |
| `docs/traps-P7.md` | 补 TRAP-P7-010（桥接双侧分帧）/ 011（`fread` 读管道阻塞）/ 012（单测同步等连接空等） |
| 源码注释（顶层 `CMakeLists.txt` 与 `cmake/*.cmake` / `src/**` / `tests/**`） | 指向路线图的引用统一改为 `ROADMAP-P7-Fin.md`；`packages.md` 章节号引用改为 §2.1 |

---

## 6. 后续动作

原报告 §6 的三项待办（P7.3 接线 / P7.2 命名管道 / P7.2 桥接 exe）与验收补齐**均已执行完毕**，见 §5。
仍属**人工目视项**（自动化无法替代，见 `CONTEXT-API.md` §8 / `packages.md` §6.4）：

1. 实机勾选「本地 Context API」后，确认日志 `[LocalPipeTransport] 本地 Context API 已监听命名管道`
   与实际管道名；
2. 以真实 MCP 客户端经 `whalepet-mcp.exe` 完成一次 `initialize` / `tools/list` / `tools/call`；
3. 在 `<安装目录>/plugins/` 放入第三方 DLL，确认 `capabilities.list` 出现其能力且退出后被卸载。

---

## 7. 未验证项（如实标注）

- **未做安装包实机安装 / 卸载**：`packaging/*` 的改动经静态核对与文档契约比对，
  但**本轮未实际跑 `makensis` 并安装**，故「卸载后无 `whalepet-mcp.exe` 残留」等属**契约级结论**，
  仍是 `packages.md` §6 的人工验收项。
- **未用第三方 MCP 客户端联调**：桥接端到端由 `test_context_pipe` 以真实进程守卫，
  但完整第三方客户端（如 IDE 内置 MCP 客户端）联调仍属人工目视项。
- **`plugins/` 安装到 `C:\Program Files` 时的写入权限**：文档已提示需管理员或改安装到用户可写目录，
  未实机验证。

---

## 8. 附带发现：文档与仓库卫生（历史记录，部分已处理）

| # | 发现 | 处理 |
|---|---|---|
| 1 | 仓库根目录存在被 git 跟踪的测试日志 `result.txt`（内容为一次 `test_growth` FAIL 输出） | **未改动**（属仓库卫生，登记待办） |
| 2 | `dist/WhalePet/README.md` 为旧副本 | `dist/` 已被 `.gitignore` 忽略；每次打包自动刷新 |
| 3 | `dist/WhalePet/` 下有 `data/`、`stomach/` 且缺 `engine/` | 本地陈旧产物，未改动 |
| 4 | `docs/ROADMAP-P1.md` / `docs/ROADMAP-P4.md` 规划期复选框未勾选 | 已于早前在文首加状态说明与落地证据表 |
