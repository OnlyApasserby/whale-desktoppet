# traps · P8 — 时段常驻立绘 / 工作立绘池 / 预设对话（真实踩坑记录）

> 对应 `ROADMAP-P8.md`。环境基线：Qt 6.8.4 + MSVC（VS 18 2026）+ CMake 4.4.2。
> 仅记录实施过程中**真实复现并排查过**的问题；问题解决前不美化、不删除、不提前标记完成。

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| TRAP-P8-001 | `kCodingPose` 定义被误删 → C2065 未声明标识符 | 编译 | 已解决 |
| TRAP-P8-002 | `PresetDialogueTable::clear()` 只声明未定义 → LNK2019 | 链接 | 已解决 |
| TRAP-P8-003 | `SettingsDialog.cpp` 缺 `#include <QLineEdit>` → C2027 | 编译 | 已解决 |
| TRAP-P8-004 | 编程时立绘闪一下 `work-ram`（播报与常驻立绘不同源） | 行为 | 已解决 |
| TRAP-P8-005 | 档位扩容后 `core + warm (53) ≥ kCacheCapacity (36)` → 预载逐出 core | 设计/回归 | 已解决 |
| TRAP-P8-006 | `test_context_http_security` 并行运行时偶发失败 | 测试环境 | 已定性（非回归） |
| TRAP-P8-007 | 成员函数取名 `slots()` 被 Qt 关键字宏展开 → C2059 | 编译 | 已解决 |

---

## TRAP-P8-001 — `kCodingPose` 定义被误删 → C2065

**类别**：编译 ｜ **影响**：`whalepet_core` 编译失败，全部下游目标阻塞。

### 现象

```
src/core/PetStateMachine.cpp(87,20): error C2065: “kCodingPose”: 未声明的标识符
```

复现：`cmake --build build --config Debug --target test_preset_dialogue`。

### 根因

`WorkPosePool.h` 里原本同时有 `constexpr const char *kCodingPose = "running";` 与
`bool workStateIsCoding(...)` 声明；在一次局部替换中把常量定义连同注释一起替换掉了，
只剩下函数声明 —— 而 `PetStateMachine.cpp` 仍在引用该常量。

### 解决

把 `kCodingPose` 常量定义放回 `WorkPosePool.h`（与 `workStateIsCoding` 相邻，语义成组）。

### 影响与关联文档

`src/core/WorkPosePool.h`、`src/core/PetStateMachine.cpp`；见 `ROADMAP-P8.md` P8.2。

---

## TRAP-P8-002 — `PresetDialogueTable::clear()` 只声明未定义 → LNK2019

**类别**：链接 ｜ **影响**：`WhalePet.exe` 链接失败（Debug / Release 同）。

### 现象

```
whalepet_view.lib(DialogueService.obj) : error LNK2019: 无法解析的外部符号
  "public: void __cdecl whalepet::core::PresetDialogueTable::clear(void)"
F:\develop\desktoppet\build\Debug\WhalePet.exe : fatal error LNK1120: 1 个无法解析的外部命令
```

### 根因

`PresetDialogue.h` 声明了 `void clear();`，但 `PresetDialogue.cpp` 漏写了实现
（`loadFromText` / `addAnswer` / `find` / `countOf` 都写了，唯独漏掉 `clear`）。
头文件里以 `inline` 方式写的 `empty()` / `size()` 掩盖了同类风险，只有非内联声明会暴露。

### 解决

在 `PresetDialogue.cpp` 补 `void PresetDialogueTable::clear() { m_questions.clear(); }`。

### 影响与关联文档

`src/core/PresetDialogue.{h,cpp}`；见 `DIALOGUE.md` §2。

---

## TRAP-P8-003 — `SettingsDialog.cpp` 缺 `#include <QLineEdit>` → C2027

**类别**：编译 ｜ **影响**：`whalepet_view` 编译失败。

### 现象

```
src/view/SettingsDialog.cpp(100,33): error C2027: 使用了未定义类型“QLineEdit”
src/view/SettingsDialog.cpp(103,15): error C2665: “QFormLayout::addRow”: 没有重载函数可以转换所有参数类型
```

### 根因

新增天气设置项时只在 `SettingsDialog.h` 做了 `class QLineEdit;` 前向声明，
但 `.cpp` 里实际 `new QLineEdit(...)` / 调用 `editingFinished` / `setText`，
需要完整类型；漏加 `#include <QLineEdit>`。连带 `QFormLayout::addRow` 因参数类型不完整而报重载不匹配。

### 解决

在 `SettingsDialog.cpp` 补 `#include <QLineEdit>`。

### 影响与关联文档

`src/view/SettingsDialog.{h,cpp}`；见 `SETTINGS.md` §4。

---

## TRAP-P8-004 — 编程时立绘闪一下 `work-ram`（播报与常驻立绘不同源）

**类别**：行为（测试暴露） ｜ **影响**：需求 R3「编程时常驻 running」在**状态切换的那一帧**被破坏。

### 现象

`test_work_state::machineDrivesPoseAndSilencesProactive` 失败：

```
FAIL!  : 实际 "work-ram" ≠ 期望 "running"   （Event::workStateChanged(Coding) 的返回值）
```

复现：`ctest -C Debug -R test_work_state`。

### 根因

`contextPose()` 已按 P8 规则把编程族映射为 `running`，但 `EventType::WorkStateChanged` 分支
在**播报**时仍调用旧的 `workStatePose(next)`，随后 `applyOneShot()` 把该值写进 `m_current` ——
于是"状态变化的那一次 return"返回 `work-ram`，下一个 tick 才回到 `running`（肉眼即"闪一下"）。

### 解决

抽出 `PetStateMachine::contextWorkPose()`（编程族 → `running`；busy 非编程族 → 池当前张；
其余按 `workStatePose`；`Idle`/`Unknown` → `nullptr` 交回下层），
`contextPose()` 与 `WorkStateChanged` 播报**共用同一口径**，消除两处规则漂移。

### 影响与关联文档

`src/core/PetStateMachine.{h,cpp}`；`ROADMAP-P8.md` A4；`STATE-MACHINE.md` §1.3。

---

## TRAP-P8-005 — 档位扩容后 `core + warm ≥ kCacheCapacity` → 预载逐出 core

**类别**：设计 / 回归（测试暴露） ｜ **影响**：core 档（首帧 + 贴边，延迟最敏感）可能被 LRU 逐出，与"零延迟"设计意图冲突。

### 现象

`test_pose_assets` 三处失败：

```
FAIL! : tierKeysAreAllRegisteredPoses  Actual (core.size()): 15  Expected (12): 12
FAIL! : tiersDoNotOverlapAndFitCapacity  'core.size() + warm.size() < library.capacity()' returned FALSE
FAIL! : libraryPreloadsCoreTierSynchronously  Actual (library.warmCount()): 38  Expected (22): 22
```

### 根因

P8 为 R1–R4 新增了 3 张 core（`night` / `daily-pajama` / `running`）与 16 张 warm
（工作池补全 6 + 对话池 5 + 天气 5）。15 + 38 = **53 > kCacheCapacity(36)**，
预载过程会按 LRU 把最早的 core 成员挤出去 —— 而该容量存在的唯一目的正是"预载完成后 core 与 warm 共存不逐出"。

### 解决

两条同时做（不做任何"放宽断言"的掩盖）：

1. **精简档位**：`sleep` 移出 core（P8 起深夜空闲立绘是 `daily-pajama`，`sleep` 已无代码路径输出）；
   `failure` / `celebrate` / `levelup` 与长尾关键词、对话池 5 张、天气 5 张一律**按需加载**
   （问答为 8–15 分钟一次的低频路径，首次解码延迟不可感知）→ core **14** + warm **25** = 39；
2. **容量 36 → 40**：39 < 40 恢复"预载不互逐"；40 × 256² × 4 B = **10.0 MiB**，
   仍满足 `POSE-ASSETS.md` 的 M3 目标（≤ 10 MiB），故未突破既定指标。

同步更新 `test_pose_assets` 的 4 处硬断言（core 14 / warm 25 / capacity 40 / warmCount 25）。

### 影响与关联文档

`src/view/PoseLibrary.{h,cpp}`、`tests/test_pose_assets.cpp`；
`POSE-ASSETS.md`（B1 / B2′ / M2 / M3 / 附录 C）、`ROADMAP-P8.md` A10。

---

## TRAP-P8-006 — `test_context_http_security` 并行运行时偶发失败

**类别**：测试环境（**非 P8 回归**） ｜ **影响**：全套并行执行时该用例偶发失败，易误判为本次改动引入。

### 现象

`ctest -C Debug`（默认并行度）中出现：

```
28 - test_context_http_security (Failed)   9.18 sec
```

而**单独复跑**与**顺序全量复跑**均通过（4.28s / 4.32s，33/33）。

### 根因（判定）

该用例绑定本地 HTTP 端口并做超时/连接清理断言，与其它绑定端口的用例
（`test_context_pipe` / `test_process_plugin`）在并行调度下可能互相抢占或受时序抖动影响。
本次改动未触碰 `src/contextapi/**`，且单跑/顺序跑稳定通过 → 判定为**并行环境偶发**，不是回归。

### 解决

未修改该用例（不掩盖）。验收口径统一为：**顺序执行** `ctest -C Debug` /
`ctest -C Release`，并要求全绿；如后续需要并行 CI，另立专项处理端口隔离。

### 影响与关联文档

`tests/test_context_http_security.cpp`（未改动）；`TESTING.md` §2 执行口径。

---

## TRAP-P8-007 — 成员函数取名 `slots()` 被 Qt 关键字宏展开 → C2059

**类别**：编译 ｜ **影响**：`whalepet_view` 编译失败（改问答系统时引入，立刻被编译拦住）。

### 现象

```
src/core/PresetDialogue.h(68,36): error C2059: 语法错误:“)”
src/core/PresetDialogue.h(68,43): error C2238: 意外的标记位于“;”之前
```

对应源码为：

```cpp
std::vector<std::string> slots() const;   // ← 第 68 行
```

### 根因

Qt 把 `slots` 与 `signals` 定义为**关键字宏**（`qobjectdefs.h` 里的 `#define slots`）。
`PresetDialogue.h` 经 Qt 头文件之后被编译时，`slots` 被展开为空 token，
声明于是变成 `std::vector<std::string> () const;`（缺函数名）→ 语法错误。
同类保留名还有 `signals`、`emit`、`foreach`。

### 解决

改名为 `answerSlots()`（语义也更准确：返回该问题**已登记的回答槽位**），
并在头文件加注释说明「不可叫 `slots`」，避免后续再犯。
`PresetDialogue.cpp` 与 `core/DialogueOptions.cpp` 同步改名。

### 影响与关联文档

`src/core/PresetDialogue.{h,cpp}`、`src/core/DialogueOptions.cpp`；
`DIALOGUE.md` §2；这类命名冲突在纯逻辑头文件里尤其隐蔽（core 是「零 Qt 依赖」，
但被 Qt 侧编译单元包含时仍会吃到 Qt 的宏）。
