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

## 附：本轮验证顺带修掉的两处「与运行时刻耦合」的测试

非产品缺陷，但会导致**偶发红**，已一并加固：

1. `tests/test_growth.cpp::signInIsIdempotentPerDay`
   原用 `baseMs()`（当前时刻）作基准，断言 `signIn(base + 3600000)` 仍属同一天；
   若恰在午夜前 1 小时内跑，`+1h` 就跨了自然日 → 随机失败。
   新增 `noonMs()`（当天 12:00）作基准，与运行时刻解耦。
2. `tests/test_content.cpp::calendarWeekAndNightWindow`
   原断言 `weekKey(now) == weekKey(now + 7天)`，**把「一周后」当成了「同一周」**；
   正确关系是 `dayIndex` 相同、`weekKey` 前进一周。已改为断言二者不等，
   并各自校验 `weekKey(now) == dayKeyOffset(now, -dayIndexMondayFirst(now))`。
