# P-103 · 界面回归用例以「跨难度的格数」为基准 → 地图尺寸一变即误报

| 字段 | 取值 |
|---|---|
| 原始编号 | —（EX5 新编号） |
| 阶段 | EX5 · 小游戏体验优化（国际象棋难度梯度与棋盘朝向 / 找小猫地图与文案） |
| 模块 | `tests/test_smoke.cpp` |
| 报错分层 | test |
| 严重度 | minor |
| 状态 | 已解决（并已反向验证守卫仍然有效） |

## 1. 现象（可复现步骤 / 报错原文）

按需求把找小猫地图各方向翻倍（如浅滩 11×8 → 22×16）后，
`test_smoke::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty` 失败：

```text
FAIL!  : SmokeTest::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty() Compared values are not the same
   Actual   (cellsAfterUp): 308      // 深海遗迹场景 1（22x14，当时的设计）
   Expected (cellsBefore) : 352      // 浅滩场景 1（22x16）
F:\develop\desktoppet\tests\test_smoke.cpp(284) : failure location
```

该用例的本意是「方向键**不得**切换难度、**不得**重载地图」，与地图尺寸无关。

## 2. 根因

用例把基准 `cellsBefore` 取在**切换难度之前**（= 浅滩首场景的格数），
却拿它去比对「切到最后一档（深海遗迹）并按上下键之后」的格数：

- 旧版地图恰好 **浅滩 11×8 = 88 == 深海遗迹场景 1 的 11×8 = 88**，这条断言才成立；
- 这一「跨难度尺寸相等」是**隐含且无文档**的耦合，地图尺寸一变，断言就误报为回归。

另外还发现一处**守卫强度**问题：用例把方向键投递给 `view.focusWidget()`，
而 offscreen 环境下焦点并不总落在难度下拉框上 —— 实测把「下拉框截获方向键」的实现临时去掉，
该用例仍会 PASS（守卫并未真正咬到它要防的路径）。

## 3. 解决或规避

1. **基准改为同一张地图**：切到最后一档后再取 `cellsAtLast = cellCount()`，
   方向键前后都拿它与 `cellsAtLast` 比对（同一地图内移动 ⇒ 格数必须不变），
   语义更强且不再依赖跨难度尺寸相等；`afterUp/afterDown == atLastIndex` 继续守住「难度不得被改」。
2. **方向键直接投递给下拉框本体**（`QWidget *target = box;`），
   保证无论运行环境里焦点落在谁身上，都真实走一遍「下拉框收到上下键」的路径
   （窗口本体的按键路径由同一用例后面的 `QTest::keyClick(&view, Qt::Key_Right)` 覆盖）。
3. **反向验证**：临时把 `KittenView::eventFilter` 的方向键截获改成不生效 → 用例立即 FAIL：

   ```text
   FAIL!  : SmokeTest::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty() Compared values are not the same
      Actual   (afterUp)    : 1        // 下拉框把上键当成「切换难度」
      Expected (atLastIndex): 2
   ```

   还原后 PASS ⇒ 改造后的守卫确实会咬（非永真）。

## 4. 影响与关联文档

- 关联守卫：`TRAP-P6-005`（找小猫三连 Bug：方向键被下拉框吃掉）对应的界面回归用例即本用例；
  本次改造后它同时覆盖「难度不被方向键改动」与「地图不被方向键重载」两条。
- 影响范围：仅测试代码（`tests/test_smoke.cpp`）；产品代码未因此改动。
- 关联文档：`docs/MINIGAME-INTERFACE.md` §10.4、`docs/mapinit.md` §6。
