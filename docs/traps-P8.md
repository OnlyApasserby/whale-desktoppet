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
| TRAP-P8-008 | 满值常驻位于时段态之前 → 深夜 / 傍晚立绘永不显示 | 行为 / 优先级 | 已解决 |
| TRAP-P8-009 | 沙箱时钟落后于构建产物 → MSBuild 静默跳过重编译，测试跑的是旧二进制 | 环境 / 验证 | 已解决 |
| TRAP-P8-010 | 深夜「唤醒窗口」跨时段泄漏 + 跨时段立绘延迟最多约 60s | 行为 / 优先级 | 已解决（需求二次修订） |

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

---

## TRAP-P8-008 — 满值常驻位于时段态之前 → 深夜 / 傍晚立绘永不显示

**类别**：行为 / 优先级（代码审查 + 运行时数据发现） ｜ **影响**：需求 R2「深夜 23:00–06:59 空闲常驻 `daily-pajama`」在养成满值时**完全失效**（傍晚 `night` 同样失效）。

### 现象

用户报告「23:00–7:00 的立绘变更未生效」。运行时表现为：无论几点，立绘恒为 `tail-swing`（摇尾巴）。

### 根因

新增的「满值特殊常驻」（2026-10-04 立绘激活批次）被插在 `PetStateMachine::contextPose()` 的**时段态之前**：

```cpp
// 1.5) 满值特殊常驻
if (m_mood >= kMoodMax && m_satiety >= kSatietyMax) {
    return kVitalsFullPose;   // 无条件 return，且没有像 1.6 睡眠循环那样排除深夜
}
...
// 2) 时段态（傍晚 night / 深夜 daily-pajama）—— 永远到不了
```

同一批次插入的 `1.6` 睡眠循环写了 `idleSlot != DaySlot::LateNight` 主动给深夜让路，
`1.5` 漏了同样的判断，于是「心情 & 饱腹同时满值」时时段态被整体遮蔽。
本机 `whalepet.db` 佐证：4 个库中 3 个 `satiety = 100`，其中 `deploy-release/data` 为
`mood = 100 & satiety = 100`；且 `m_satietyAccumMs` 不落库、每次重启清零，
开发期反复启停会让 satiety 长期不掉点，满值窗口被拉长。

**为何测试没拦住**：`test_state_machine::vitalsFullShowsTailSwing` 只在默认小时（12，日间）验证；
`daySlotsAndLateNightPersistent`（原名 `daySlotsAndLateNightWake`，见 TRAP-P8-010）从不调用
`setVitals`（默认 0/0）。两个特性的**交叉处零覆盖**，故 35/35 全绿。

### 解决

按方案 A 把「满值常驻」整块**下移到时段态之后**（`contextPose()` 第 4 档）：

```
工作态 > 睡眠循环 > 时段态（傍晚/深夜）> 满值常驻 > 挂机态 > 游戏陪玩态 > 静息态
```

同时把深夜做成**独立阶段**（不参与任何随机立绘池：待机小剧场 / 睡眠循环 / 逗弄 / 满值），
只保留点击反馈，并新增「深夜点击累计 ≥ 10 次 → `meme-smile-pain` 虚弱 20s + `click.latenight.weak` 台词」。

回归用例（修复前必然失败）：

- `test_state_machine::vitalsFullYieldsToTimeSlots`：满值 + 12 点 → `tail-swing`；18 点 → `night`；23 点 → `daily-pajama`；回到日间 → `tail-swing`。
- `test_state_machine::lateNightIsIndependentStage`：深夜连续 tick 不出现 `teasing`、待机 20min+ 不出 `sleep`、满值不出 `tail-swing`；离开深夜后睡眠循环立即恢复。
- `test_state_machine::lateNightClicksTriggerWeakPose`：第 10 次深夜点击 → `meme-smile-pain`（ttl 20s + 虚弱台词）；计数清零后需再满 10 次。
  （**注**：该用例随 TRAP-P8-010 的需求二次修订一并更新 —— 前 9 次点击立绘**不再切换**、虚弱 20s 到期**直接**回 `daily-pajama`；原文描述的「20s 后回 `night`、唤醒窗口到期回 `daily-pajama`」已随唤醒态移除而作废。）

### 影响与关联文档

`src/core/PetStateMachine.{h,cpp}`、`src/core/IdleRules.h`、`assets/lines/lines.txt`、
`tests/test_state_machine.cpp`；`docs/STATE-MACHINE.md` §1 / §1.2 / §2 / §3 / §3.1 / §4、
`docs/POSE-ASSETS.md` 附录 A（2026-10-04 增量修订）。

---

## TRAP-P8-009 — 沙箱时钟落后于构建产物 → MSBuild 增量构建**静默跳过重编译**

**类别**：环境 / 验证（**非代码缺陷**） ｜ **影响**：改了源码、`cmake --build` 成功、`ctest` 仍复现
**改动前**的行为，极易把环境假象误判为逻辑缺陷（本次为此多轮排查）。

### 现象

修改 `src/core/PetStateMachine.cpp` 后重新构建并测试，断言仍按**旧语义**失败；
构建输出只有 `xxx.vcxproj -> xxx.lib`，**看不到 `PetStateMachine.cpp` 编译行**。

### 根因

本机 shell 时钟落后于仓库已有产物的 mtime（sandbox 时钟 ≈ 00:2x，而 `build/Debug/*` 为 20:5x，
即约 20 小时"未来"）；工具写出的源文件 mtime 又比产物更旧：

```
src/core/PetStateMachine.cpp                        2026/10/4 00:22:22   ← 源比产物旧
build/Debug/whalepet_core.lib                       2026/10/4 20:52:22   ← 产物"未来"
build/whalepet_core.dir/Debug/PetStateMachine.obj   2026/10/4 00:27:48
```

MSBuild 按「输入是否比输出新」做增量 → 判定一切最新 → 只打印目标输出行，
**不重新编译 / 不重新归档 / 不重新链接** → `ctest` 继续运行旧 `.exe`（旧核心已静态链进旧 exe）。

### 解决（可复现）

任选其一，并**确认构建日志出现对应 `.cpp` 的编译行**：

1. 把修改过的源文件 mtime 设到产物之后（如 `(Get-Date).AddDays(1)`）再构建；
2. 或删除受影响产物强制重建：`build/Debug/{whalepet_core.lib, whalepet_view.lib, test_*.exe}`。

> 校验口径：只出现 `xxx.vcxproj -> xxx.lib/.exe` 表示该步骤被**跳过**；
> 必须出现 `PetStateMachine.cpp` / `test_xxx.cpp` 之类的**编译行**才算真的重建。
> 修正后 Debug / Release 全量 CTest 各 **35/35 通过**（与"旧二进制"的错误结论完全相反，
> 反证此前失败是环境假象）。

### 影响与关联文档

`docs/BUILD.md`（增量构建）、`docs/TESTING.md`（验收口径）。与代码无关，
但会直接污染「测试结论」的可信度：**先确认二进制是新的，再解释断言失败**。

---

## TRAP-P8-010 — 深夜「唤醒窗口」跨时段泄漏 + 跨时段立绘延迟

**类别**：行为 / 优先级（需求二次修订） ｜ **影响**：需求 R2「深夜常驻 `daily-pajama`」在**跨时段**场景下失效，或延迟最多约 60s 才切换。

### 现象

- 22:5x 与角色交互（点击 / 拖拽 / 喂食 / 关键词命中等**任一** `touchInput` 路径）后跨入 23:00，
  角色仍显 `night`（傍晚立绘）—— 深夜常驻立绘不生效，直到 60s 唤醒窗口到期；
- 反向同样成立：22:5x 的一次性姿态（如 `react-head`）跨入 23:00 后仍继续占位，切换被一并延迟；
- 时段边界（23:00 整）本应立刻切换，实际要等后续 `Tick` 才纠正。

### 根因

原实现把「深夜唤醒」做成 `m_lateNightAwakeUntilMs = nowMs + kLateNightAwakeMs` 的**绝对时刻窗口**，
且该窗口**不随时段变化而失效**：

```cpp
case EventType::Clock: {
    if (event.hour >= 0) { m_hour = event.hour; }
    if (m_oneShotUntilMs == 0 && !m_dragging) { m_current = fallback(event.nowMs); }
    return m_current;      // 跨时段：不清窗口、不清一次性姿态、不立刻重算
}
```

- `lateNightAwake()` 只判「当前时刻 < 窗口绝对时刻」且「当前是深夜」，**不区分窗口是在哪个时段建立的** →
  22:59 建立、覆盖到 23:00 之后的窗口在深夜成立，立绘被判为 `night`；
- `Clock` 分支仅在 `m_oneShotUntilMs == 0` 时重算立绘，跨时段**不清一次性姿态** → 继续沿用上一时段姿态；
- 无「跨时段」显式分支，刷新完全依赖下一次 `Tick`，故最长延迟约一个 `kLateNightAwakeMs`（60s）。

**为何测试没拦住**：既有用例只在**同一时段内**验证唤醒与到期（`daySlotsAndLateNightWake`），
没有任何「在时段 A 交互后**立刻跨到时段 B**」的断言 → 交叉处零覆盖。

### 解决

按需求二次修订**移除深夜唤醒态**（根因消除），并新增跨时段立即刷新：

- 删除 `kLateNightAwakeMs` / `kLateNightAwakePose` / `lateNightAwake()` / `m_lateNightAwakeUntilMs`；
  `touchInput()` 只刷新「最后输入时刻」，不再影响深夜立绘 → **泄漏窗口不复存在**；
- 深夜常驻立绘恒为 `daily-pajama`；点击**不换立绘**（仅回应台词并累计计数，满 `kLateNightWeakClickCount`
  次 → `meme-smile-pain` 虚弱 20s）；
- `Clock` 分支新增跨时段判断：`daySlotOf(m_hour)` 与上一时段不同时**当帧**清残留
  （离开深夜 → 清零计数 / 虚弱窗口；非拖拽时清一次性姿态）并按新时段立即重算立绘。

回归用例（修复前必然失败）：

- `test_state_machine::daySlotsAndLateNightPersistent`：22:59 `click`（生成 `react-head`，旧实现还泄漏出 `night`）后，
  跨 23:00 的 `clock` **当帧**返回 `daily-pajama`，随后 `tick` 仍为 `daily-pajama`；深夜一轮 9 次点击恒为 `daily-pajama`。
- `test_state_machine::lateNightClicksTriggerWeakPose`：前 9 次点击立绘不变（`daily-pajama`）+ 回应 `click.*` 台词；
  第 10 次 → `meme-smile-pain`（ttl 20s）；20s 到期**直接**回 `daily-pajama`。

### 影响与关联文档

`src/core/PetStateMachine.{h,cpp}`、`src/core/DaySlotRules.h`、`src/core/IdleRules.h`、
`src/viewmodel/PetController.cpp`、`tests/test_state_machine.cpp`、`tests/test_preset_dialogue.cpp`；
`docs/STATE-MACHINE.md` §1 / §1.2 / §2 / §3 / §5、`docs/ROADMAP-P8.md` §P8.1 / §2、
`docs/POSE-ASSETS.md` 附录 A、`docs/README.md`。
