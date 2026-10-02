# P7 剩余接口 · 配置核查报告

> **范围**：P7 中**尚未交付**的两组接口——**P7.2**（MCP Server 侧通道启用与 `whalepet-mcp.exe`
> 控制台桥接）与 **P7.3**（`plugins/` 动态 DLL 插件装载）。
> **方法**：**静态核对**（读代码 / CMake / 打包脚本 / 设置项，逐条比对文档声明）。
> **本次未做**：未运行构建、未运行 CTest、未实机验证——故本文不含任何运行期结论。
> **关联**：`ROADMAP-P7.md`（阶段验收）、`CONTEXT-API.md` §4、`PLUGIN-ARCHITECTURE.md` §4.1、
> `packages.md` §8、`SETTINGS.md` §7。

---

## 1. 结论摘要

| 接口 | 交付物 | 代码实现 | 配置接线 | 打包/分发 | 测试 | 运行期可用 |
|---|---|---|---|---|---|---|
| **P7.2** | 本地回环 HTTP 通道 | ✅ 已实现 | ✅ 已接线 | —（不落文件） | ✅ `test_context_dispatch` | ✅ 勾选即启动（实机行为仍属人工目视项） |
| **P7.2** | `context_api_enabled` / `_port` / `_token` 设置项 | ✅ | ✅ | — | ✅ | ✅ |
| **P7.2** | **命名管道通道**（`QLocalServer`） | ❌ **未实现** | ❌ | — | ❌ | ❌ |
| **P7.2** | **`whalepet-mcp.exe` 桥接 exe** | ❌ **未实现**（无 CMake 目标） | ❌ | ❌ | ❌ | ❌ |
| **P7.3** | `DllPluginLoader`（元数据 / ABI 协商 / 降级） | ✅ 已实现 | ❌ **未接线** | ❌ | ❌ | ❌ |
| **P7.3** | `plugins/` 目录扫描 | ❌ | ❌ | ⚠️ 仅文档约定 | ❌ | ❌ |
| **P7.3** | 示例插件（`ext.hello`） | ❌ | — | ❌ | ❌ | ❌ |

**总体判断**：两组剩余接口**都不影响默认运行行为**——默认配置（`context_api_enabled = false`、
无 `plugins.json`、无 `plugins/` 目录）下不监听端口、不扫描目录、无新增进程。
**但文档中「放 DLL 进 `plugins/` 即可」「由宿主启动时扫描」等表述此前与实际不符**，
本轮已按事实修正（见 §5）。

---

## 2. P7.2 逐项核查

`ROADMAP-P7.md` 对 P7.2 的验收要求：
> 设置项真实生效（HTTP 回环监听 + 命名管道）；`whalepet-mcp.exe` 控制台桥接（stdio ↔ 本地通道）；
> `initialize` / `tools/list` / `tools/call` 全链路。

| # | 项 | 事实 | 证据 |
|---|---|---|---|
| 1 | HTTP 回环监听 | ✅ 已实现，只绑 `QHostAddress::LocalHost` | `src/contextapi/transport/LocalHttpTransport.cpp:58` |
| 2 | 端口 0 = 系统分配 | ✅ 生效 | `LocalHttpTransport.cpp:70`（打印实际端口）、`PetWindow.cpp:975` |
| 3 | token 校验（`X-WhalePet-Token`） | ✅ HTTP 401 + `kRpcErrorUnauthorized` | `LocalHttpTransport.cpp:192` |
| 4 | token 校验（MCP `initialize.params.token`） | ✅ 已实现 | `src/contextapi/transport/StdioTransport.cpp:174` |
| 5 | `StdioTransport`（MCP Server 侧） | ✅ 已实现并可单测（绑定任意 `QIODevice` 对） | `tests/test_context_dispatch.cpp:488` |
| 6 | **`StdioTransport` 的运行期载体** | ❌ **无**：`ContextApiService` 只持有 `LocalHttpTransport`，`start()` 不创建 stdio 通道 | `src/contextapi/ContextApiService.h:64-72`、`ContextApiService.cpp:77-90` |
| 7 | **命名管道（`QLocalServer` / `QLocalSocket`）** | ❌ **全仓库零使用点**（唯一出现处是 `CMakeLists.txt:16` 的注释提到 `QLocalServer`） | 全库检索（`*.cpp/*.h/*.txt/*.cmake/*.ps1/*.nsi`，排除参考项目与构建目录）；`ARCHITECTURE.md` §2 已注明「规划中」 |
| 8 | **`whalepet-mcp.exe` 桥接目标** | ❌ **CMake 中不存在**（仅 `WhalePet`、测试目标与两个测试桩 exe） | `CMakeLists.txt`（全文无 `whalepet-mcp`） |
| 9 | 桥接入口预留 | ✅ 有：`ContextApiService::handleRequest()` 供「MCP stdio 桥接进程（P7.2）」直调 | `ContextApiService.h:48-50` |
| 10 | 打包/分发处理 | 无需处理（exe 不存在；HTTP 不落文件） | `packaging/make-package.ps1`、`packaging/whalepet.nsi` |
| 11 | 测试覆盖 | 通道与分发已覆盖；**桥接 exe / 命名管道无测试目标** | `CMakeLists.txt:515-518`（仅 `test_context_dispatch`） |

**结论**：P7.2 的**主通道（HTTP 回环）已可用且门控正确**；缺的是
**命名管道**与**桥接 exe**——即 MCP Server 在运行期**没有真实进程中转**，
所以「任一 MCP 客户端可列举并调用 `context.snapshot`」这条 P7.2 验收标准**不成立**。

---

## 3. P7.3 逐项核查

`ROADMAP-P7.md` 对 P7.3 的验收要求：
> `plugins/` 目录扫描装载、IID/`apiVersion` 协商、失败降级；示例插件（如 `ext.hello`）。
> 放入合法 DLL 后 `capabilities.list` 出现其能力；版本不匹配的 DLL 被跳过且主程序正常启动。

| # | 项 | 事实 | 证据 |
|---|---|---|---|
| 1 | `IPluginFactory` ABI 边界 | ✅ 已定义：IID `ai.whalepet.PluginFactory/1.0`、`kPluginApiVersion = 1` | `src/plugin/dll/IPluginFactory.h:22,38` |
| 2 | 元数据 `apiVersion` 校验 | ✅ 缺失 / 高于宿主 → 跳过并记原因 | `src/plugin/dll/DllPluginLoader.cpp:72-80` |
| 3 | IID 不匹配 / 实例化失败降级 | ✅ `qobject_cast` 失败 → `unload()` + 跳过 | `DllPluginLoader.cpp:87-92` |
| 4 | `factory->apiVersion()` 二次协商 | ✅ | `DllPluginLoader.cpp:93-99` |
| 5 | 注册冲突处理（不静默） | ✅ 注册被拒 → `unload()` + 返回原因 | `DllPluginLoader.cpp:108-113` |
| 6 | `QPluginLoader` 保活（不析构卸载） | ✅ 存入 `m_loaders` | `DllPluginLoader.cpp:115` |
| 7 | **组合根接线** | ❌ **无调用点**：`PetWindow` 只 include 并使用了 `BuiltinPluginLoader` 与 `ProcessPluginLoader`；全库检索 `DllPluginLoader` 仅命中其自身 `.{h,cpp}` 与 `CMakeLists.txt` 的源列表 | `src/view/PetWindow.cpp:20-21`；`src/plugin/dll/DllPluginLoader.{h,cpp}` |
| 8 | **`plugins/` 目录扫描** | ❌ 未发生（因第 7 项），目录名常量也未在任何组合根出现 | 同上 |
| 9 | 目录约定的位置 | ⚠️ 文档约定为 `<安装目录>/plugins/`；**加载器构造函数收的是任意目录字符串**，接线时才需定死 | `DllPluginLoader.cpp:16-19`、`PLUGIN-ARCHITECTURE.md` §4.1 |
| 10 | 打包脚本处理 | ❌ 两个脚本都没有创建 / 安装 / 卸载 `plugins/`（当前**刻意不装**） | `make-package.ps1:94-101`（只建 `engine/`）；`whalepet.nsi:50,139,160-164,214` |
| 11 | 示例插件产物 | ❌ 仓库内**无**任何 DLL / `metadata.json` / 示例插件目录 | 全库检索无匹配 |
| 12 | 测试覆盖 | ❌ 无 `DllPluginLoader` 测试目标（`test_plugin_registry` 只用替身覆盖 `PluginOrigin::Dll` 的**优先级仲裁**，不加载 DLL） | `CMakeLists.txt:485-488`、`tests/test_plugin_registry.cpp:258-261` |

**结论**：P7.3 的**加载器实现完整、降级路径齐备**，但**没有接线、没有目录扫描、没有示例插件、
没有测试**——因此「放入合法 DLL 后 `capabilities.list` 出现其能力」这条验收标准**不成立**。

> ⚠️ **用户可见影响**：当前把 DLL 放进 `<安装目录>/plugins/` **不会生效**（也不会报错）。
> 该行为此前在 `packages.md` §8 被描述为「手动放入即可」，已在本轮修正。

---

## 4. 配置面交叉核查

### 4.1 CMake 目标与依赖

| 检查项 | 结果 |
|---|---|
| `whalepet_plugin` 是否编入 DLL 加载器 | ✅ `src/plugin/dll/IPluginFactory.h`、`DllPluginLoader.{h,cpp}` 在源列表内（`CMakeLists.txt:171-173`） |
| 是否需要为命名管道新增 Qt 模块 | 不需要——`Qt6::Network` 已随 `whalepet_contextapi` 链接（`CMakeLists.txt:222-224`），`QLocalServer` 在其中 |
| 是否需要新增 exe 目标（桥接） | 需要（P7.2），当前无 |
| 是否会影响既有 22 个测试目标 | 否（无既有目标依赖上述两项） |

### 4.2 设置项（`settings.json_ext`）

`src/model/SettingsRepo.cpp:55-65` 实际键位与 `SETTINGS.md` §2/§7 一致：

| 键 | 默认 | 用途 | 出现在 |
|---|---|---|---|
| `context_api_enabled` | `false` | HTTP 通道总开关 | `SettingsRepo.cpp:114,149` |
| `context_api_port` | `0` | 回环端口（0 = 系统分配） | `SettingsRepo.cpp:115,150` |
| `context_api_token` | 空 | HTTP / MCP token 校验 | `SettingsRepo.cpp:116,151` |
| `work_aware_enabled` | `false` | 感知采样开关 | `SettingsRepo.cpp:113,148` |
| `acp_enabled` / `acp_signal_path` | `false` / 空 | ACP 显式信号 | `SettingsRepo.cpp:118-119,152-153` |
| `acp_dsh_path` / `acp_profile` / `acp_workspace` | 空 | ACP 客户端 | `SettingsRepo.cpp:121-123,154-156` |

**缺口**：P7.2 / P7.3 **均不需要新设置项**——
命名管道名与桥接 exe 属打包/约定范畴；`plugins/` 目录为约定路径；
外部进程插件配置走 `<数据目录>/plugins.json`（**不是** `json_ext` 键，`packages.md` §8.1）。
→ **设置项层：无需改动**。

### 4.3 打包与分发

| 检查项 | 结果 |
|---|---|
| `engine/` 落点 | ✅ 免安装版 `make-package.ps1:94-101`；安装版 `whalepet.nsi:160-164` + 卸载 `:214` + `icacls` 授权 |
| `plugins/` 落点 | ⚠️ **未创建、未安装、未卸载**——与 `packages.md` §8「不随包安装、卸载不删」的约定一致，**无需立即改脚本** |
| 桥接 exe | ❌ 无产物，脚本无需处理 |

### 4.4 组合根（`PetWindow` 装配顺序）

`src/view/PetWindow.cpp:216-229` 的装配链：
`setupMiniGames → setupContextMenu → setupTray → setupController → setupGrowth → setupContent →
setupStomach → setupChat → setupHotword → setupWorkState → setupContextApi → setupAcp →
setupSettings → setupRecallEntry`，随后 `:1602` 调 `setupProcessPlugins()`。

- ✅ 内置层（`BuiltinPluginLoader`，`PetWindow.cpp:455-459`）与外部进程层（`setupProcessPlugins`）已装配；
- ❌ **没有** DLL 层的等价 `setupDllPlugins()`；即 `DllPluginLoader` 从未被构造。

---

## 5. 本轮改动清单（文档整理 + 据实修正）

**只改文档，未改任何功能与源码。** 全部结论均来自对 `src/`、`tests/`、`CMakeLists.txt`、
`assets/`、`packaging/` 的静态核对；**未构建、未运行 CTest**，故不以运行期结果为由改写任何记录。

### 5.1 剩余接口相关的据实修正

| 文件 | 原表述 | 现状 |
|---|---|---|
| `packages.md` §2/§8 | 「由宿主在启动时扫描（`QPluginLoader`）」「手动创建 `plugins\` 并放入 DLL 即可」 | 明确标注**扫描尚未接线（P7.3 待办）**，当前放入 DLL **不会生效** |
| `PLUGIN-ARCHITECTURE.md` §4.1 | 「`<安装目录>/plugins/`（**运行期按目录扫描**）」 | 改为**规划**并加 ⚠️ 现状说明（加载器已实现但未接入组合根） |
| `PLUGIN-ARCHITECTURE.md` §1 / `ARCHITECTURE.md` §3 | 「ACP / IDE Agent 集成 = 预留接口（仅接口）」 | 改为「P7.5 实现 + P7.6 ACP 客户端」；`contextapi` 行同步 |
| `PLUGIN-ARCHITECTURE.md` §4 表 / §9 | 三层装载器未区分「已接线 / 未接线」 | 补 ⚠️ 说明；§9.2 补 P7.4/P7.5/P7.6 的 CTest 递进与当前 22 目标 |
| `CONTEXT-API.md` §1 / §4 | 「本期交付的是接口与可运行骨架」；命名管道未标注状态 | 改为按交付状态分列，并标注命名管道**尚未实现**（无 `QLocalServer` 使用点） |
| `ARCHITECTURE.md` §2 | `Qt6::Network`（`QTcpServer` / `QLocalServer`） | 标注当前只落地 `QTcpServer`，命名管道待 P7.2 |
| `ROADMAP-P7.md` 总览 / 依赖图 / 完成标记 / 验证记录 | P7.6 缺失；P7.2/P7.3 状态含糊；P7.4/P7.5 的 CTest 递进互相颠倒 | 四处补 P7.6；P7.2/P7.3 标注具体缺口；递进改回 **P7.4 18→19、P7.5 19→20**（与 `docs/README.md` 一致） |

### 5.2 文档整体整理（索引 / 数字 / 交叉引用）

| 范围 | 修正 |
|---|---|
| 测试目标总数 | 根 `README.md`（2 处）、`docs/TESTING.md` 的「18 个」→ **22 个**，并补齐 4 个缺失目标名；`TESTING.md` 目标表补 4 行（小游戏结算 / ACP 显式信号 / MCP Client / ACP 映射与客户端）与 `QCoreApplication` vs `offscreen` 的准确说明 |
| `docs/README.md` | 补 P7.6 状态条目、踩坑合计 **9 条**（含 `TRAP-P7-009`）、当前测试总量、未改签阶段（P1/P4）说明、剩余接口指引；索引表补 `P7-REMAINING-INTERFACES-AUDIT.md` 与 `ROADMAP-P7` |
| `ROADMAP-P6-Fin.md` | 「项目最后一个阶段」→ P0–P6 收尾；「小游戏不实现」结论标注**已被 P6+ 取代**（现 3 个插件） |
| `ROADMAP-P5-Fin.md` / `ROADMAP-P2-Fin.md` | 文首「未完成 / 待改签」与实际 `-Fin` 文件名矛盾 → 改为「已完成并改签」 |
| `ROADMAP-P1.md` / `ROADMAP-P4.md` | 补「状态说明 + 落地证据表」，并注明**窗口位置持久化需求已废止**（`traps-extend0.md` `TRAP-EXT0-002`） |
| `ROADMAP-P3-Fin.md` | 已删除的 `savePosition/restorePosition/importLegacyPositionIfNeeded` 标注为「后已删除」；`whale-moe-core.js:2134-2137` 越界引用改为真实行 `:552` |
| `ROADMAP-P0-Fin.md` | `MINIGAME-INTERFACE.md` 描述由「小游戏预留」更正；交付物表补 P0 之后新增的文档 |
| `ACP-EVAL.md` | 标题改为「P7.6 准入评估（历史文档，已实施）」；§9.1「新增 `test_acp_client`」改为已交付；§9.2 完整链路标注**已于 P7.6 验证**；风险表与事实来源表的「本机无 dsh」标注**已消除/作废**；§4.3 澄清 `thinking/waiting/tool/failure` 非 `WorkState` 取值；§6.2 提案表补「与实际交付的差异」并以代码为唯一真源 |
| `DATA-MODEL.md` | **补缺失的 §3.9 `hotwords` 表**（`Schema.cpp` 与 `traps-P6.md` 均在引用它）；「所有表带 id」改为按表说明主键；`json_ext` / `meta` 键清单补全；`minigame_enabled` 改为三游戏统一门控；仓储清单补 `HotwordRepo`、表数改六张 |
| `STATE-MACHINE.md` | 「`meme-*`（13 种）」→ 关键词表情 **21 项**（其中 `meme-*` 10 项）；删除 `night` 行并说明深夜走 `sleep`；`running/failure/celebrate/greet/wink` 标注**资产已备但未接入**；升级/成就姿态改为 `levelup`/`achievement`；`blush` 触发改为「夸夸」、摸头改为分区立绘；`KeywordHit` 来源改为 `PetController` |
| `PRESENTATION.md` | `meme-*` 数量 13 → **18**；呼吸动效「`QPropertyAnimation`」→ **`QTimer` 帧驱动**（源码中无该 Qt 类）；单击反馈去掉不存在的「特效」；双击 / 悬停标注**设计预留、未实现**；右键菜单项补全 |
| `GAMEPLAY.md` | 小游戏由「两个插件」→ **三个**（补 §11）；夸夸/摸头的立绘与特效**互换纠正**（夸夸=`blush`+爱心，摸头分区=`Fx::None`）；升级曲线补 `kLevelStep = 500` 与 `expNeeded()`；羁绊等级改为「1+（解锁节点 3/5/7）」；小游戏类 7 项成就注明三游戏共用 |
| `CHAT.md` | 语料文件表补 `chess.txt` / `work.txt`；规模改为「whale 原库 530+ / **本项目实际 368 条**」（逐文件计数）；`hug/cute/morning` 澄清为「既无立绘也无台词，直接跳过」；P5 的 7/7 标注为历史值 |
| `MINIGAME-INTERFACE.md` | `test_chess.cpp`「14 类用例」→ **12**；台词语料行补 `chess.txt`/`work.txt`；§8 的 12/12 标注为**当时**实测（现 22 目标） |
| `mapinit.md` | §6.5 的 12/12 标注为当时实测值 |
| `BUILD.md` | §10 打包流程补「清空并重建空 `engine/`」步骤；发布目录排除项补 `engine/`；`stomach/` 授权说明补 `engine/`；§9 基线改为「以 `data/whalepet.db` 生成为权威判据」（线程数非稳定判据） |
| `traps-P7.md` | `test_process_plugin`「8 个用例」→ **6 个用例**（Totals 8 含 init/cleanup） |
| `traps-P5.md` / `traps-P6.md` | 指向已改名文件的 `ROADMAP-P5.md` / `ROADMAP-P6.md` → `-Fin` 版本 |
| `ACP-EVAL.md` 事实来源与 §4 开头 | 歧义写法：把**参考项目**内的契约文档写成裸 `docs/…` 路径 → 补全为 `referances/dsh-whale-musume/docs/…` |

> 复核方式：对全部 `docs/*.md` 与根 `README.md` 做了**引用路径存在性检查**
> （`docs/*.md`、`packaging/*`），并对「文档里出现的测试目标名 / 源文件路径 / 常量」逐条与
> `CMakeLists.txt`、`src/`、`assets/` 对照。**发现但未改动源码**的一处：
> `src/core/GrowthRules.h:38` 的注释仍引用越界的 `whale-moe-core.js:2134-2137`
> （同 §5.2 中 `ROADMAP-P3-Fin.md` 已修正的那处；因属源码注释、本轮不动源码，故仅在此登记）。

---

## 6. 建议的后续动作（按优先级，尚未执行）

均属 **P7.2 / P7.3 待实施**范畴，需另行确认后再动代码：

1. **P7.3 接线（成本最低、收益直接）**：在组合根加一个 `setupDllPlugins()`，
   以 `<applicationDirPath>/plugins` 构造 `DllPluginLoader` 并调用 `loadAll(m_plugins)`；
   同时补一个加载器单测与一个示例插件（`ext.hello`）。同步 `packages.md` §2/§8。
2. **P7.2 命名管道**：新增 `LocalPipeTransport`（`QLocalServer`，复用同一 `JsonRpcDispatcher`），
   由 `ContextApiService::start()` 一并启停；补单测。
3. **P7.2 桥接 exe**：新增控制台目标 `whalepet-mcp`（`add_executable`，非 `WIN32`），
   以 `stdin`/`stdout` 驱动 `StdioTransport`、以命名管道/HTTP 连回主进程；
   打包脚本与 NSIS 三处对应表需同步（`packages.md` §2/§5）。
4. **验收补齐**：P7.2 / P7.3 的验收标准目前**无任何自动化守卫**，
   建议随实现补测试目标（预计 CTest 22 → 24+）。

---

## 7. 未验证项（如实标注）

- **未构建、未运行 CTest**：本文所有「已实现」结论均来自**源码与脚本静态阅读**，
  不代表可编译 / 可通过测试。测试目标总数（22）来自 `CMakeLists.txt` 静态计数，
  与各 ROADMAP 记录的历史实测值一致，但**本轮未复跑验证**。
- **未实机验证**：命名管道缺失、DLL 未接线均可在源码层确证；
  但「勾选 Context API 后 HTTP 端口确实监听」等运行期行为仍属
  `CONTEXT-API.md` §8 的**人工目视项**，本文未替代执行。
- **未核验打包产物**：`dist/` 与 `deploy-release/` 的既有内容未做一致性比对
  （仅发现下述卫生问题，未改动）。

---

## 8. 附带发现：文档与仓库卫生（**仅报告，未改动**）

| # | 发现 | 证据 | 影响 |
|---|---|---|---|
| 1 | 仓库根目录存在**被 git 跟踪的测试日志** `result.txt`（GBK 编码，内容是一次 `test_growth` 的 **FAIL** 运行输出） | `git ls-files result.txt` 命中；`docs/traps-P3.md:47` 的示例命令 `test_growth.exe -o result.txt,txt` 正是它的来源 | 与 `ROADMAP-P3-Fin.md`「5/5 通过」的结论**观感矛盾**，易被误读为当前有失败用例。建议删除或把该命令的输出改到已被忽略的路径 |
| 2 | `dist/WhalePet/README.md` 是**旧的 README 副本**（与根 `README.md` 哈希不同） | `Get-FileHash` 两者不一致；`make-package.ps1:104` 每次打包会重新复制，故属**产物陈旧** | `dist/` 已被 `.gitignore` 忽略，**不影响仓库**；下次打包自动刷新 |
| 3 | `dist/WhalePet/` 下有 `data/` 与 `stomach/`，且**缺少** `engine/` | 目录列举；`make-package.ps1:94-101` 会清空重建 `engine/` | 说明该目录是**早于「国际象棋 / engine」的一次打包**、且之后被运行过。属本地产物，未改动 |
| 4 | `docs/ROADMAP-P1.md` / `docs/ROADMAP-P4.md` 的规划期复选框全部未勾选，与「已实现」现状观感不符 | 文件内容 vs `docs/README.md` §二.3 | 本轮已在这两个文件顶部**加状态说明与落地证据表**（不代判验收通过） |

