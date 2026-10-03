# 踩坑记录 · EX1（游戏内存陪玩：Cheat Engine 分析 + 运行期只读感知）

> 规范见 `docs/README.md` §二.5：每条记录必须**实际复现并排查过**，格式为
> **现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档**；
> 禁止编造未发生条目，问题解决前不得美化、删除或提前标记完成。
>
> 与 `ROADMAP-ex1.md` 一一对应。崩溃类问题按 `docs/README.md` §六 处理：
> **AI 不得自行排查崩溃**，必须立即停止编译/构建/测试与复现尝试，如实记录并交回用户。
> 本阶段截至建档时**未出现崩溃**（无异常退出 / 访问违例），故无交回调试条目。

---

## 记录清单

| 编号 | 现象摘要 | 类别 | 状态 |
|---|---|---|---|
| TRAP-EX1-001 | CMake 生成失败：`Cannot find source file: .../Win32GameMemoryReader.h`（类声明误并入 `IGameMemoryReader.h`） | 构建/工程 | 已解决 |
| TRAP-EX1-002 | MSVC C2662：`resolve() const` 调用非 const 的 `readPointer()`，`this` 指针无法从 `const` 转换 | 编译/语言 | 已解决 |
| TRAP-EX1-003 | `QRegularExpression` 分段原始字符串把 `(?:` 的首个 `(` 当作 `R"(` 分隔符，正则非法并**静默失配** | 语言/陷阱 | 已解决 |
| TRAP-EX1-004 | 运行期跨进程调用 `mono.dll` 导出函数 == 在目标进程执行代码（远线程/注入），触碰只读红线 | 设计/规范 | 已决策（改为离线名字解析） |
| TRAP-EX1-005 | MV/MZ 未必可用 `--remote-debugging-port`，CDP 通道并非总可用 | 设计/决策 | 已决策（双通道路由 + 桥接回退） |
| TRAP-EX1-006 | 测试出现 `QObject::disconnect: wildcard call … of QTcpSocket::unnamed` 告警 | 噪声/观察 | 无需修复（Qt 内部，非缺陷） |
| TRAP-EX1-007 | 未过期一次性姿态期间到达的游戏里程碑被吞（`busy` 用 `m_oneShotUntilMs > 0` 判定） | 行为/测试 | 已澄清（符合设计，非缺陷） |

---

## TRAP-EX1-001：CMake 生成失败——声明与列名不一致导致源文件缺失

- **现象**：`cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"`
  在 Generate 阶段报错（逐字）：
  ```
  CMake Error at D:/Qt-debug/lib/cmake/Qt6Core/Qt6CoreMacros.cmake:2804 (add_library):
    Cannot find source file:
      F:/develop/desktoppet/build/src/gamestate/Win32GameMemoryReader.h
  Call Stack (most recent call first):
    ...
    CMakeLists.txt:168 (qt_add_library)
  ```
  可复现：`CMakeLists.txt` 第 172 行把 `src/gamestate/Win32GameMemoryReader.h` 列为源文件，
  但磁盘上不存在该头文件（只有 `.cpp`）；`Win32GameMemoryReader` 类声明被写进了
  `IGameMemoryReader.h`，而 `Win32GameMemoryReader.cpp` 却 `#include "gamestate/Win32GameMemoryReader.h"`。
- **根因**：接口抽象与具体实现被合并在同一个头里，导致「CMake 源列表 / `.cpp` 的 include /
  头文件实际位置」三者不一致；CMake 在 Generate 阶段即校验源文件存在性并硬失败。
- **解决或规避**：把 `Win32GameMemoryReader` 类从 `IGameMemoryReader.h` 拆出，新建独立头
  `src/gamestate/Win32GameMemoryReader.h`（`#include "gamestate/IGameMemoryReader.h"`）；
  在 `IGameMemoryReader.h` 末尾 `#include "gamestate/Win32GameMemoryReader.h"`，
  保持既有「仅含 `IGameMemoryReader.h`」的消费者（`test_game_memory*`、`GenericChainAdapter.cpp`）
  仍可见具体类，无需改动。重新 configure 通过。
- **影响与关联文档**：`src/gamestate/IGameMemoryReader.h`、`src/gamestate/Win32GameMemoryReader.h`、
  `CMakeLists.txt`（`whalepet_gamestate`）；关联 `docs/ROADMAP-ex1.md` EX1.1、§六 6.2。

### TRAP-EX1-002：`resolve()` 为 const 却调用非 const 辅助方法（MSVC C2662）

- **现象**：编译 `whalepet_gamestate` 时报错（逐字）：
  ```
  PointerChainResolver.cpp(81,14): error C2662: “bool whalepet::gamestate::PointerChainResolver::readPointer(uint64_t,uint64_t *,QString *)”:
    不能将“this”指针从“const whalepet::gamestate::PointerChainResolver”转换为“whalepet::gamestate::PointerChainResolver &”
  ```
  可复现：`cmake --build build --config Debug --target whalepet_gamestate`。
- **根因**：`PointerChainResolver::resolve()` 按接口契约（§六 6.2）声明为 `const`，其内部经
  `readPointer()` → `readRaw()` 累加「本轮字节预算」`m_bytesThisRound`，故辅助方法只能是非 const；
  const 成员函数不能把 `this` 传给需要非 const 的方法 → C2662。
- **解决或规避**：将「本轮字节预算」`m_bytesThisRound` 声明为 `mutable`，并把私有辅助
  `readRaw()` / `readPointer()` 改为 `const`。因该计数是「读取开销预算」这一逻辑上的观察量，
  不影响对象可观察语义，故用 `mutable` 是恰当的最小改动（未放宽接口的 const 契约）。
- **影响与关联文档**：`src/gamestate/PointerChainResolver.h` / `.cpp`；关联 `docs/ROADMAP-ex1.md`
  §2.6.2.1、§4.3、§六 6.2。

### TRAP-EX1-003：`QRegularExpression` 分段原始字符串——`(?:` 首字符被当作分隔符吞掉

- **现象**：`test_unity_adapters` 4 个用例失败，报「dump.cs 中未识别到任何带偏移（// 0x…）的字段」；
  逐字：
  ```
  FAIL!  : UnityAdaptersTest::parseFieldOffsetsExtractsByClassAndName()
    ...returned FALSE. (dump.cs 中未识别到任何带偏移（// 0x…）的字段)
  ```
  可复现：`.\build\Debug\test_unity_adapters.exe -o result.txt,txt`。
- **根因**：`UnityDumpConverter.cpp` 的 `classRe()` 把正则拆成多段 `QStringLiteral(R"(…)" R"(…)" R"(…))`。
  C++ 原始字符串 `R"(…)"` 的**首个 `(` 是分隔符的一部分**、不属于内容；当我写
  `R"(?:class|…)"` 想表达内容 `(?:class|…` 时，实际内容变成 `?:class|…`，**丢了一个 `(`**。
  多段拼起来后正则括号不平衡 → `QRegularExpression` 无效 → `match()` 恒不命中，
  而 `haveClass` 始终为 false，字段被整体跳过，最终只报「未识别到字段」（掩盖了真因）。
- **解决或规避**：
  1) 把 `classRe()` 合并为**单一**原始字符串（内容以 `^\s*` 开头，不触碰分隔符歧义）；
  2) 在 `parseFieldOffsets()` 入口加 `namespaceRe()/classRe()/fieldRe().isValid()` 防御检查，
     正则无效时直接返回「内部解析正则无效（实现缺陷）」，把静默失配变成显式错误。
- **影响与关联文档**：`src/gamestate/UnityDumpConverter.cpp`；关联 `docs/ROADMAP-ex1.md` EX1.2、
  `docs/UNITY-SOP.md` §2。

### TRAP-EX1-004：运行期调用 `mono.dll` 导出函数 == 在目标进程执行代码（红线决策）

- **现象/命题**：ROADMAP §2.6.1 原表述为「经 `mono.dll` 导出按类名/字段名解析」。
  但 Mono 的这些导出函数（`mono_class_from_name` / `mono_field_get_offset` / …）只能在
  **目标进程内**执行：跨进程实现必须「在目标进程创建远线程调用该函数」或注入一个模块，
  这属于**在目标进程执行代码**，等价于注入/hook。
- **根因**：Mono 无稳定的、可纯 `ReadProcessMemory` 复刻的公开结构（布局随版本变化），
  故「名字解析」的物理实现逃不开在目标进程内调用其 API。
- **解决或规避（决策）**：EX1.2 改为**离线名字解析**——
  `UnityDumpConverter` 解析 Il2CppDumper/Cpp2IL 的 `dump.cs`，按「类名+字段名」定位字段偏移并
  产出 profile；运行期 `UnityMonoAdapter` / `UnityIl2CppAdapter` 仅**消费 profile**（纯只读内存读取）。
  既满足「按名字定位、不硬编码偏移」的目标，又不触碰「不注入、不在目标进程执行代码」的红线。
- **影响与关联文档**：`src/gamestate/UnityDumpConverter.*`、`UnityMonoAdapter.*`、`UnityIl2CppAdapter.*`、
  `UnityAdapterBase.*`、`docs/UNITY-SOP.md`；关联 `docs/ROADMAP-ex1.md` §2.6.1 与 EX1.2 交付物 1 的折衷说明。

### TRAP-EX1-005：MV/MZ 的 CDP 通道并非总可用（双通道路由决策）

- **现象/命题**：ROADMAP §2.6.2 方案 A 要求 MV/MZ 走 CDP（`--remote-debugging-port`）。
  但实测与常识：**并非所有 MV/MZ 用户可传启动参数**——启动器转发参数、加壳、
  或发行版把 NW.js 参数固定，都会导致调试端口未开启；此时若无回退，该游戏永远接不上。
- **根因**：CDP 依赖「用户能给 NW.js 传参 + Chromium 调试端口可绑」两个前提，二者不由本工具控制。
- **解决或规避（决策）**：EX1.3 采用**双通路 + 回退**：
  `createGameStateAdapter` 对 MV/MZ 先判 `RpgMakerCdpAdapter::supports()`（有 `cdpPort`/`wsUrl`）
  → 用 CDP；否则判 `RpgMakerBridgeAdapter::supports()`（有 `bridge`）→ 用桥接；
  两者都缺则返回 `nullptr` + 原因（提示加 `--remote-debugging-port` 或配 `bridge`）。
  RGSS（XP/VX/VX Ace）无 CDP，直接走桥接主路径。桥接输出格式与 CDP 探测结构对齐，
  故 `RpgMakerSpecialSceneDetector` 两通道共用一份判据/滞回实现。
- **影响与关联文档**：`src/gamestate/GameStateAdapterFactory.cpp`、`RpgMakerCdpAdapter.*`、
  `RpgMakerBridgeAdapter.*`、`docs/RPGMAKER-SOP.md` §2/§3/§5；关联 `docs/ROADMAP-ex1.md` EX1.3 交付物 3。

### TRAP-EX1-006：`QWebSocket` 销毁期的 Qt 内部告警（噪声，勿误判为缺陷）

- **现象**：`test_rpgmaker_adapters` 的 CDP 用例执行时打印（逐字）：
  ```
  QWARN  : RpgMakerAdaptersTest::cdpAdapterReadsFieldsAndProbe()
    QObject::disconnect: wildcard call disconnects from destroyed signal of QTcpSocket::unnamed
  ```
  用例本身 `PASS`（8 passed / 0 failed），不影响结果。
- **根因**：该告警来自 Qt `QWebSocket`/`QWebSocketServer` 内部在对象析构路径上的
  `disconnect(…, nullptr, …)`；`QTcpSocket::unnamed` 为 Qt 内部传输套接字，与业务代码无关。
- **解决或规避**：**无需修复**。记录以**避免后续误判为「套接字泄漏/未清理」而做无谓改动**；
  验收以「用例是否 PASS」为准，不以该 QWARN 为准。
- **影响与关联文档**：`src/gamestate/CdpWebSocketClient.*`、`tests/test_rpgmaker_adapters.cpp`；
  关联 `docs/ROADMAP-ex1.md` EX1.3 验收标准「连接失败时优雅降级」。

### TRAP-EX1-007：未过期一次性姿态期间到达的里程碑/陪玩态被让位（符合设计，非缺陷）

- **现象**：`test_game_companion` 首轮运行，`machineMilestonesBroadcast` 失败（逐字）：
  ```
  FAIL!  : GameCompanionTest::machineMilestonesBroadcast() Compared values are not the same
     Actual   (QString::fromStdString(r2.pose)): "levelup"
     Expected (QStringLiteral("game-win"))     : "game-win"
  ```
  复现步骤：`machine.reset(0)` → `handle(gameStateChanged(Normal,0,levelUp,1000))` →
  **不推进 Tick** → `handle(gameStateChanged(Normal,0,clear,8000))`，期望立绘变 `game-win`，
  实际仍是 `levelup`（上一次升级的一次性姿态）。
- **根因**：`PetStateMachine::handle(EventType::GameStateChanged)` 用
  `busy = (m_oneShotUntilMs > 0) || m_dragging` 判定「不打断一次性姿态」，而
  `m_oneShotUntilMs` 是**绝对到期时刻**（`event.nowMs + ttl`），只有在 `EventType::Tick`
  分支里 `event.nowMs >= m_oneShotUntilMs` 时才被清零（见 `PetStateMachine.cpp` Tick 分支）。
  因此两次游戏事件之间若不经过 Tick，即使`nowMs`已越过到期时刻，`busy` 仍为 true，
  里程碑被整体跳过（既不 fallback 也不播报）。这与工作态（`WorkStateChanged`）行为完全一致。
- **解决或规避（决策）**：**代码无需改动**——真实运行时 `GameCompanionService` 每 200ms 采样、
  主循环同时持续发送 Tick，一次性姿态会在其到期后的第一个 Tick 被回收，里程碑不会丢。
  测试侧修正：在两个游戏事件之间插入 `machine.handle(Event::tick(5000))`，模拟真实 Tick 节奏，
  再验证通关里程碑生效（现 17/17 通过）。
- **影响与关联文档**：`src/core/PetStateMachine.cpp`（`GameStateChanged`/`WorkStateChanged` 的 busy 判定）、
  `tests/test_game_companion.cpp`；关联 `docs/ROADMAP-ex1.md` EX1.4「不打断工作专注/深夜/一次性小剧场」验收标准。

---

## 记录模板（新增条目照此格式）

### TRAP-EX1-000：一句话现象

- **现象**：可复现步骤 + 报错原文（逐字，含文件:行号）。
- **根因**：最小化到具体机制，说明为什么会这样。
- **解决或规避**：实际采取的修复动作（不是「可能」「也许」）。
- **影响与关联文档**：涉及的文件/符号，关联的路线图章节。

---

## 待人工验收项（非踩坑，登记备查）

> 需真实游戏与人工操作，不计入自动化测试；完成后回填结论与日期。

| 编号 | 验收项 | 关联阶段 | 状态 |
|---|---|---|---|
| ACC-EX1-001 | ≥1 个 Unity **Mono** 单机游戏端到端读出约定字段 | EX1.2 | 待验收 |
| ACC-EX1-002 | ≥1 个 Unity **IL2CPP** 单机游戏端到端读出约定字段 | EX1.2 | 待验收 |
| ACC-EX1-003 | ≥1 个 RPG Maker **MV/MZ** 单机游戏经 CDP 只读读出金币/变量/坐标 | EX1.3 | 待验收 |
| ACC-EX1-004 | 特殊场景 CG（图片 / 专用场景 / 影片至少各 1 例）识别并驱动静默陪伴 | EX1.3 | 待验收 |
| ACC-EX1-005 | ≥1 个 RPG Maker **RGSS（XP/VX/VX Ace）** 单机游戏经只读脚本桥接读出字段 | EX1.3 | 待验收 |
| ACC-EX1-006 | 开启「游戏陪玩」后端到端触发立绘/台词（升级/BOSS/通关各 ≥1 例），且进入特殊场景（CG/影片/专用场景/对话）时静默陪伴 | EX1.4 | 待验收 |
| ACC-EX1-007 | 关闭「游戏陪玩」后运行行为与 EX1 前一致（无进程打开、无 game.* 台词、进程退出后自动回到正常陪伴态） | EX1.4 | 待验收 |
