# P-107 · 断言「会话独占期间 `lineKey` 为空」失败 —— 漏算 `PoseResult` 的缓存重放语义

| 字段 | 取值 |
|---|---|
| 阶段 | P10（工作能力插件；② 状态机「菜单互动」通道） |
| 原编号 | —（P10 新增） |
| 模块 | `tests/test_state_machine.cpp`（用例口径；被测行为正确） |
| 报错分层 | test（CTest 用例失败） |
| 严重度 | minor（口径误判，非产品缺陷；修正断言后通过） |

## 一、现象（可复现步骤 / 报错原文）

新增 P10 通道用例 `menuInteractionOutranksWorkAndSlot` 后，Debug CTest 单测失败：

```
2/4 Test #2: test_state_machine ...............***Failed    0.22 sec
```
```text
FAIL!  : StateMachineTest::menuInteractionOutranksWorkAndSlot() 'coding.lineKey.empty()' returned FALSE. ()
F:\develop\desktoppet\tests\test_state_machine.cpp(760) : failure location
Totals: 32 passed, 1 failed, 0 skipped, 0 blacklisted, 7ms
```

复现步骤（当时写法）：`reset` → `menuInteractionChanged(Work)` → `workStateChanged(Coding)`
→ 断言「返回结果的 `lineKey` 为空」。

> 附带现象：`ctest --output-on-failure` 把该用例的输出落进
> `build/Testing/Temporary/LastTest.log` 时只有 `Test Failed.`（Qt Test 的输出不在该日志里）；
> 判定「断言失败 vs 进程异常退出」的可靠做法是**直接取被测进程退出码**
> （本次 `EXIT=1` = 断言失败；若是访问违例则是 `0xC0000005` 一类，须按 `README.md` §六 交回用户）。

## 二、根因

状态机**每个 tick 都会重推同一个缓存结果**（`core::PoseResult` 的注释明确约定：
`fxSerial` / `lineSerial` 只在真正产生新表现时自增，同一结果重放时保持不变 —— 见
`src/core/PetTypes.h:25-30`，Presenter 据此去重、避免台词与特效被按 tick 反复播放）。

P10 的「会话独占」守卫按设计**直接 `return m_current`**（不覆盖立绘、不播报），因此返回的
`lineKey` 仍是**进入会话时**那一批（`menu.work`）的缓存值，而**不是空**。用例把
「不播报」误写成了「`lineKey` 为空」。

## 三、解决或规避

断言改为比「与进入会话时同一批次」——**序号不变 ⇒ Presenter 不会重播**，这才是「不表现」的
可核验判据：

```cpp
QCOMPARE(QString::fromStdString(coding.lineKey), QStringLiteral("menu.work"));
QCOMPARE(coding.lineSerial, entered.lineSerial); // 序号不变 → 不会重播
```

口径沉淀：凡是断言「某事件**不产生新表现**」，都应比对 `fxSerial` / `lineSerial`（批次序号），
而不是断言字段为空 —— 空值断言只在「结果被重新 compose」时才成立。

## 四、影响与关联文档

- 影响面：仅该用例；被测产品行为正确，修正后 `test_state_machine` **33 passed / 0 failed**，
  全量 Debug CTest **35/35**。
- 关联：`src/core/PetTypes.h`（序号注释）、`docs/STATE-MACHINE.md` §1.2（菜单互动口径）、
  `docs/ARCHITECTURE.md` 附录 D.3、`docs/pitfalls/index.md`（阶段表）。
- 同类先例：`P-099` / `P-100`（均为用例侧口径问题，产品无缺陷）。
