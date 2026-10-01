# traps · P4 — 内容层（真实踩坑记录）

> 对应 `ROADMAP-P4.md`。按 `README.md` §二.5 约定，**仅记录 P4 实施过程中真实复现**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P4-001` | `windeployqt` 后构建目录自带 `platforms/` → 缺 `qoffscreend.dll`，`test_smoke` 异常退出 `0x80000003` | 环境/崩溃 | 已解决（用户定位根因） |
| `TRAP-P4-002` | `slots` 与 Qt 关键字宏同名 → 引入 `QObject` 的头里 `slots()` / `for (x : slots)` 全部编译失败 | 编译 | 已解决 |
| `TRAP-P4-003` | `QuestRepo::markClaimed` 仅按 `done = 1` 判定 → SQLite 重复领取仍返回 `true` | 逻辑/幂等 | 已解决 |
| `TRAP-P4-004` | 成长日记时间戳以 1970 起算（`elapsed` 时钟经 `interactionOccurred` 泄漏到内容层） | 时间基准/数据 | 已解决 |
| `TRAP-P4-005` | 「状态 / 日常 / 设置」三处签到状态不同步 | 状态同步/UI | 已解决 |
| `TRAP-P4-006` | 每日固定任务「今日签到」永不完成：签到从未上报 `Interaction::Signin` | 接线缺失/内容层 | 已解决 |

---

## TRAP-P4-001 — 构建目录缺 `qoffscreend.dll`，`test_smoke` 异常退出

**类别**：环境 / 崩溃 ｜ **影响**：`ctest -C Debug` 里 `test_smoke` 判失败，
且**没有任何测试输出**（连 `-o` 指定的日志文件都不生成），极易误判为「P4 改动引入的崩溃」

### 现象

`ctest` 输出原文：

```
1/6 Test #1: test_smoke .......................***Failed   53.01 sec
...
50% tests passed, 3 tests failed out of 6
```

（同批 `test_growth` / `test_content` 另有失败，属独立问题，见 TRAP-P4-003 与「附」）

直接运行可拿到退出码，但**拿不到任何日志**：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
& .\build\Debug\test_smoke.exe -o smoke_out.txt,txt
"exit=$LASTEXITCODE"          # → exit=-2147483645
Test-Path .\smoke_out.txt     # → False（日志文件根本没生成）
```

`-2147483645` = `0x80000003`（`STATUS_BREAKPOINT`），即进程**异常终止**。

### 根因

**由用户在 Qt Creator 中调试确认**，两条环境原因叠加：

1. 系统环境变量里配置的 Qt 目录指向**已被删除的 Qt**（已改回正确目录）；
2. `build/Debug/platforms/` 下**缺 `qoffscreend.dll`**：

```powershell
Get-ChildItem .\build\Debug\platforms | Select-Object Name
# qwindowsd.dll        ← 只有这一个
```

机制（与 `TRAP-P3-005` 同源）：`windeployqt` **只部署 `platforms/qwindowsd.dll`**，
不会带上 offscreen 插件；而一旦 exe 同级出现 `platforms/` 目录，Qt 就把**该目录**当作插件目录，
**不再回退**到 Qt 前缀 `D:/Qt-debug/plugins/platforms/` 去找。
于是 `QT_QPA_PLATFORM=offscreen` 无插件可用，`QApplication` 构造阶段即终止——

这正好解释了「日志文件为空」：`test_smoke` 的 `-o` 日志、`qWarning()`
都发生在 `QApplication` 之后，而进程在**进入 `main()` 有效逻辑之前**就死了。

> `windeployqt` 是本次 P4 验证时**新执行**的动作（P1~P3 未在 `build/` 下跑过），
> 所以这个坑此前没暴露：以前 exe 同级没有 `platforms/`，Qt 按前缀解析能命中 `D:/Qt-debug/plugins`。

### 解决 / 规避

补齐 debug 版 offscreen 插件（**验证辅助，不随正式发布**）：

```powershell
Copy-Item "D:/Qt-debug/plugins/platforms/qoffscreend.dll" "build/Debug/platforms/" -Force
```

验证结果：`test_smoke` **2.02 sec Passed**，`ctest -C Debug` **6/6 通过**。

### 与 `TRAP-P3-005` 的差异

| | 目录 | 插件文件名 |
|---|---|---|
| `TRAP-P3-005` | 部署目录 `deploy-release/`（Release） | `qoffscreen.dll` |
| `TRAP-P4-001` | 构建目录 `build/Debug/`（Debug） | `qoffscreend.dll` |

**共同规律**：插件名要按构建配置带/不带 `d` 后缀；且**只要 exe 同级有 `platforms/`，
就必须把该目录当成完整插件目录来补齐**，不能指望它回退到 Qt 前缀。

### 影响与关联文档

- `traps-P3.md` `TRAP-P3-005`（同源问题的 Release 版本）、`BUILD.md`、`TESTING.md`。
- **教训**：`ctest` 报某个测试「失败且耗时异常（几十秒）」又拿不到输出时，
  第一件事是**直接跑该 exe 并取退出码**——`0x80000003` / 无日志文件 = 初始化阶段就死了，
  与业务代码无关。

---

## TRAP-P4-002 — `slots` 与 Qt 关键字宏同名，`QObject` 相关头全部编译失败

**类别**：编译错误 ｜ **影响**：`whalepet_view` 编译中断，代码「看起来完全正确」

### 现象

`cmake --build build --config Debug` 报错原文（节选）：

```
src/model/QuestRepo.cpp(55,27): error C2530: "s": 必须初始化引用
src/model/QuestRepo.cpp(55,29): error C2143: 语法错误: 缺少";"(在":"的前面)
src/model/QuestRepo.cpp(55,36): error C2143: 语法错误: 缺少";"(在")"的前面)
```

对应的源码是**完全合法**的 range-for：

```cpp
for (const QuestSlot &s : slots) {
```

### 根因

`QuestRepo.cpp` 经由 `model/Database.h` 间接引入了 `<QObject>`，而 Qt 在
`qobjectdefs.h` 中把 `slots` 定义成了**空宏**（不启用 `QT_NO_KEYWORDS` 时）：

```cpp
# define slots      // → 展开为空
```

于是预处理后这一行变成 `for (const QuestSlot &s : ) {`——
编译器看到的是一个「声明了引用却没初始化」的语句，才报出 C2530/C2143 这种**指向含义完全无关的**错误。

函数参数名 `slots` 在**声明处**（`const QList<QuestSlot> &slots`）不会报错
（未命名形参合法），所以错误只在 `.cpp` 的 `for` 处炸开，更增迷惑性。

### 解决

模块内不再使用裸标识符 `slots`（`slot`、`m_slots` 不是宏，可继续用）：

- `QuestRepo::replaceAll(const QList<QuestSlot> &newSlots)`
- `QuestService::slotList()`（原 `slots()`；成员仍是 `m_slots`）
- `ContentPanel::refreshDaily()` 内局部变量 → `questSlots`

并在 `QuestService.h` 的访问器上方写明原因，避免后续「顺手改回 `slots()`」。

### 影响与关联文档

- `ARCHITECTURE.md` 命名约定；同类的 Qt 宏还有 `signals` / `emit` / `foreach`，**不得**用作标识符。
- 排查技巧：报错位置语义明显不通（如「引用未初始化」出现在 `for (x : y)`）时，
  优先怀疑**标识符被宏替换**，可用 `cl /P` 或 IDE 的「预处理后」视图确认。

---

## TRAP-P4-003 — `markClaimed` 只判 `done`，SQLite 重复领取仍返回成功

**类别**：逻辑错误 / 幂等 ｜ **影响**：`test_content::questRepoConditionalUpdate` 失败；
若上层不加内存态兜底，**会重复发放任务奖励**

### 现象

```
FAIL!  : TestContent::questRepoConditionalUpdate() '!repo.markClaimed(QStringLiteral("pat-3"))' returned FALSE. ()
tests/test_content.cpp(441) : failure location
```

即第二次调用 `markClaimed()` 仍返回 `true`。

### 根因

原实现把「幂等」寄托在 `done = 1` 这个条件上：

```cpp
q.prepare(QStringLiteral("UPDATE quests SET claimed = 1 WHERE quest_id = :id AND done = 1"));
...
return q.numRowsAffected() > 0;
```

但 SQLite 的 `numRowsAffected()`（即 `changes()`）统计的是 **WHERE 匹配到的行数**，
**不比较 SET 后的值是否真的变化**。已领取的行仍然匹配 `done = 1`，所以第二次执行
依旧「影响 1 行」→ 返回 `true`。

（`numRowsAffected()` 返回 `> 0` 只是「语句执行成功且匹配到行」的判据，
不能当作「状态发生了迁移」的判据。）

### 解决

把目标状态写进 WHERE，让**已达成目标态的行不再匹配**：

```cpp
q.prepare(QStringLiteral(
    "UPDATE quests SET claimed = 1 WHERE quest_id = :id AND done = 1 AND claimed = 0"));
```

修复后：未 `done` → `false`；首次领取 → `true`；重复领取 → `false`。

### 影响与关联文档

- 同类的 `SigninRepo::markReward` 用 `(reward_claimed & bit) = 0` 做条件，**本来就是对的**，
  可作对照写法。
- 通用原则：**幂等的条件更新 = 「目标状态」写进 WHERE**，不要用 `numRowsAffected()` 代替状态判断。
- `QuestService::claim()` 另有内存态 `claimed` 兜底，所以服务层用例此前是通过的——
  这次是仓储层的单测把它兜出来了（见 `tests/test_content.cpp::questRepoConditionalUpdate`）。

---

## 附：验证顺带修掉的「与运行时刻耦合」的测试

非产品缺陷，但会导致**偶发红**，已一并加固：

1. `tests/test_growth.cpp::signInIsIdempotentPerDay`
   原用 `baseMs()`（当前时刻）作基准，断言 `signIn(base + 3600000)` 仍属同一天；
   若恰在午夜前 1 小时内跑，`+1h` 就跨了自然日 → 随机失败。
   新增 `noonMs()`（当天 12:00）作基准，与运行时刻解耦。
2. `tests/test_content.cpp::calendarWeekAndNightWindow`
   原断言 `weekKey(now) == weekKey(now + 7天)`，**把「一周后」当成了「同一周」**；
   正确关系是 `dayIndex` 相同、`weekKey` 前进一周。已改为断言二者不等，
   并各自校验 `weekKey(now) == dayKeyOffset(now, -dayIndexMondayFirst(now))`。
3. （后续轮次发现）`tests/test_growth.cpp::satietyDecayUsesIntegerPoints` 与
   `companionTimeAccumulates`：
   `GrowthService::load()` 以「载入当时墙钟」为结算基准 `m_lastSettleMs`，而测试的
   `base = baseMs()` 在其**之后**才取，两者相差 `d` 毫秒（通常 0，偶发 ≥1）。
   - `settle(base + kMsPerSatietyPoint - 1)` 的实际 elapsed 变成 `k-1+d`，
     `d≥1` 时误掉 1 点 → 断言失败（本次复现到的就是它）；
   - `settle(base + kGrowthTickMs)` 的 elapsed 变成 `k+d` → `companionMs` 多出 `d`。
   修法：在 `load()` 后用 `growth.resetToDefaults(base)` 把结算基准与衰减余量**显式锁定**
   到 `base`（不是放宽阈值），断言即完全确定。`ctest -R test_growth --repeat until-fail:60`
   全绿验证。

---

## TRAP-P4-004 — 成长日记时间戳以 1970 起算（elapsed 时钟泄漏到内容层）

**类别**：时间基准 / 数据 ｜ **影响**：成长日记里所有**由交互触发**的条目（任务完成 / 成就解锁）
显示为 `1970-01-01`；同时污染每日任务与周签到的「今天」判定

### 现象（用户复现）

成长日记中的条目时间从 **Unix 时间戳 0**（1970-01-01）起算，而不是系统当前时间。
`ContentPanel::formatRelative()` 对 `now - ts`（≈1.7e12ms）落入 `>30 天` 分支，
直接打印 `1970-01-01`。

### 根因

`PetController` 用 `QElapsedTimer m_clock` 做时钟，`nowMs()` 返回的是**进程启动起算**的毫秒数：

```cpp
qint64 nowMs() const { return m_clock.elapsed(); }   // ← 不是 Unix 墙钟
```

该值经 `interactionOccurred(type, nowMs())` 广播，组合根 `PetWindow` 原样喂给三个内容层 Service
（`AchievementService` / `QuestService` / `SigninService`）。这三个 Service 的
`nowOrCurrent()` 只判断 `nowMs > 0` 就采信，于是把「启动后几百毫秒」当成时间戳写入
`bond_diary.ts_ms`，并据此算出 `dayKey == "1970-1-1"`。

**连带影响**：`QuestService::refreshForToday` / `SigninService::syncWeek` 也拿到该 elapsed 值，
与启动时用墙钟建立的 `m_dayKey` 不一致 → 每次交互都误判「跨天」，重建任务槽位、重置周签到板。

对照：凡是以默认参数 `nowMs = 0` 调用的路径（签到 `markToday()`、账目 `settle()` 等）
拿到的是正确墙钟，所以**同一条日记里时间戳是混用的**（签到类正确、交互类为 1970）。

### 解决

把 `PetController` 的时间基准统一为**系统墙钟**（Unix 毫秒）：

```cpp
qint64 PetController::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}
```

`PetStateMachine` 内部只使用事件的**时间差**（`now - lastInput`、`now + ttl`），
与绝对基准无关，故切换后表现逻辑不受影响（已由 `test_state_machine` / `test_smoke` 回归确认）。

### 影响与关联文档

- `GAMEPLAY.md §6`（成长日记）、`DATA-MODEL.md`（`bond_diary.ts_ms` 语义 = Unix 毫秒）。
- 教训：**跨层传递的「时间」必须显式约定基准**（墙钟 ms vs 单调 elapsed ms）；
  `nowOrCurrent()` 这类「>0 就用」的兜底无法识别基准错误。状态机/动效可用单调时钟，
  但只要要落库/展示，就必须是墙钟。

---

## TRAP-P4-005 — 「状态 / 日常 / 设置」三处签到状态不同步

**类别**：状态同步 / UI ｜ **影响**：在任一处签到后，其余面板仍显示「今日签到（可点）」，状态各说各话

### 现象（用户复现）

「状态」面板、「日常」面板（右键菜单打开）、「设置 → 日常」内嵌页三处都有「今日签到」按钮，
但在一处签到后，另外两处**不刷新**，仍显示未签到。

### 根因

1. **设置内嵌页是另一个实例**：`PetWindow::syncContentPanel()` 只刷新独立窗口的
   `m_contentPanel`；`SettingsDialog` 内嵌的 `ContentPanel`（`m_content`）是**另一个对象**，
   未被刷新。
2. **状态面板按钮从不更新**：`StatusPanel::updateFrom()` 只写标签，从不改 `m_signInButton`
   的文案 / `enabled`，因此签到后仍显示「今日签到」。

### 解决

- `SettingsDialog` 暴露 `refreshContent()`（转调内嵌 `ContentPanel::refreshAll()`）；
  `PetWindow::syncContentPanel()` 同时刷新独立面板与设置内嵌面板。
- `StatusPanel` 新增 `setTodaySigned(bool)`（置灰 + 文案切换）；
  `PetWindow::syncStatusPanel()` 以 `SigninService::isTodaySigned()` 为统一口径喂入。
- 追加接线：`SigninService::boardChanged → PetWindow::syncStatusPanel`，
  使周签到板变化能实时驱动状态面板按钮。

### 验证状态

- **已验证**：Debug / Release 均可构建；`ctest` 各 **9/9 通过**（`test_content` / `test_settings` 覆盖相关链路）。
- 状态面板按钮文案/置灰、三处实时联动属人工目视项。

### 影响与关联文档

- `SETTINGS.md §2`（设置面板内嵌日常页）、`GAMEPLAY.md §4`（签到）、`PRESENTATION.md §3`（状态面板）。
- 教训：**同一逻辑面板被复用到多个宿主时，每个宿主实例都要纳入刷新链路**；
  只读展示控件若承载「状态按钮」，也必须随状态变化更新，不能只在构造时定型。

---

## TRAP-P4-006 — 每日固定任务「今日签到」永不完成（签到从未上报 `Interaction::Signin`）

**类别**：接线缺失 / 内容层 ｜ **影响**：签到记录（周签到板 / 连续天数）与每日任务不同步——
每天固定占 slot 0 的任务 `signin-1「今日签到」` 永远停在 **0/1「进行中」**，无法领取

### 现象（用户复现）

完成签到后：「状态 / 日常 / 设置」都能看到签到成功（周签到板点亮、连续天数 +1），
但每日任务列表里的「今日签到（0/1）」不推进、始终不能领取。

### 根因

任务池里有一条 `always` 任务：

```cpp
{"signin-1", "今日签到", "完成今天的签到", QuestMetric::Signin, 1, 5, 6, true}
```

`QuestService::interactionMatches()` 也已把 `core::Interaction::Signin → QuestMetric::Signin` 映射好，
但**生产代码里没有任何地方上报过 `Interaction::Signin`**：

- `PetController::applyGrowthForZone/applyGrowthForEvent` 只映射 摸头/肚子/尾巴/戳/投喂/夸夸/三连击；
- 签到走的是 `PetWindow::handleSignIn()` → `GrowthService::signIn()` + `SigninService::markToday()`
  这条**独立链路**，完全不经过 `PetController::interactionOccurred` 交互总线。

`tests/test_content.cpp::questProgressAndClaimIsIdempotent` 是**直接**调用
`quest.reportInteraction(Interaction::Signin, base)` 才通过的，因而掩盖了「组合根未接线」这一缺口——
服务层有单测、端到端却没人喂数据。

### 解决

把签到接入交互总线（保持「唯一上报口径」）：新增 `PetController::reportSignIn()`，
**只广播不施加养成增量**（签到的心情/好感已由 `GrowthService::signIn()` 落定，重复施加会双倍加心情）：

```cpp
void PetController::reportSignIn() { emit interactionOccurred(core::Interaction::Signin, nowMs()); }
```

`PetWindow::handleSignIn()` 在 `m_growth->signIn()` 成功（= 今天真的签到了）后调用一次。
随后既有接线自动完成其余动作：`QuestService` 推进 `signin-1` → 发 `questDone` →
`AchievementService::reportQuestCompleted()` → `slotsChanged` → 内容面板刷新。

**连带去重**：`signin-1` 一经上报即「完成」，`QuestService` 原本会按任务再写一条日记
（`kind = "quest"`），而签到本身已写了一条（`kind = "signin"`）；日记 UI 只展示 `detail`，
于是同一次签到会出现两条「今日签到」。因「签到任务的完成 = 签到本身」，已让
`QuestService` 对 `QuestMetric::Signin` 不再重复记日记（其余任务照旧）。

### 验证状态

- **已验证**：`tests/test_smoke.cpp` 新增 `signInInteractionReportsWallClock`——断言
  `reportSignIn()` 广播一次 `Interaction::Signin`，且时间戳为墙钟（同时守住 `TRAP-P4-004`）；
  `tests/test_content.cpp::questProgressAndClaimIsIdempotent` 增断言：完成 `signin-1` 后
  日记条数为 **0**（签到任务不重复记日记）。Debug / Release 各 **9/9 通过**。
- 端到端（签到后任务变「可领取」）建议在真实桌面点一次签到目视确认。

### 影响与关联文档

- `GAMEPLAY.md §5`（每日任务含固定「今日签到」）、`Quests.h`（`always` 槽）、`QuestService`。
- 教训：**服务层单测通过 ≠ 端到端接线完成**。凡是「某交互 → 某服务」的映射，
  都要在组合根（`PetWindow`）确认该交互确实被广播；新增交互枚举值时尤其要检查上报点。
  另：签到这类「有独立服务链路」的交互，勿让 `applyGrowthForEvent` 再走一遍养成增量。
