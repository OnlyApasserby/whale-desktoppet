# P-100 · 把「越界点击必然产生位移」当作断言，与 `move*` 返回「是否发生变化」的契约冲突

- **模块**：`tests/test_tokencatch.cpp` ↔ `core::TokenCatch::moveCatcher / moveCatcherTo`
- **报错分层**：test（QTest 断言）
- **严重度**：minor（用例期望写错，产品代码无缺陷）
- **原编号**：—（EX4 追加：接 Token 插件接入陪玩通用聚合的验证轮）

## 现象（可复现步骤 / 报错原文）

`test_tokencatch` 首轮第二个失败：

```
FAIL!  : TokenCatchTest::catcherMovementClampsAtEdges() 'game.moveCatcherTo(999)' returned FALSE. ()
F:\develop\desktoppet\tests\test_tokencatch.cpp(434) : failure location
```

复现路径：先用 `moveCatcherTo(kTokenCatchCols - 1)` 把接取区移到最右（夹取到 `maxColumn = 列数 - 接取区宽度`），
随后断言 `QVERIFY(game.moveCatcherTo(999))` 为真。

## 根因

`moveCatcher()/moveCatcherTo()` 的返回契约是「**是否发生位移**」（`true` = 位置改变），
不是「参数是否越界」。上一步已经把接取区放到 `maxColumn`，越界点击夹取之后**仍是** `maxColumn`，
没有位移 → 返回 `false`。用例把「越界 → 必然返回 true」当成了期望，与契约直接冲突。

## 解决或规避

- 修复：把断言拆成「**状态断言**」与「**返回值语义断言**」两组，并先离开边界再测越界：

  ```cpp
  QVERIFY(game.moveCatcherTo(0));
  QCOMPARE(game.catcherColumn(), 0);
  QVERIFY2(!game.moveCatcherTo(-999), "越界点击（已夹取到最左）不产生位移");
  QVERIFY(game.moveCatcherTo(999));  // 越界点击同样夹取到最右
  QCOMPARE(game.catcherColumn(), maxColumn);
  QVERIFY2(!game.moveCatcherTo(999), "已是最右：再次越界点击不产生位移");
  ```

- 规避（回灌为规则）：对「夹取 / 幂等 setter」类 API，**最终状态用 `QCOMPARE` 断言，返回值只断言
  「是否发生变化」**；不要用返回值去表达「参数被越界纠正」这类语义。

## 影响与关联文档

- 关联：`docs/MINIGAME-INTERFACE.md` §12（接 Token 的键鼠 / 点击操作口径）；
  同源契约在既有玩法里同样存在（`core::Minesweeper::reveal/toggleFlag` 返回 `MineMove.changed` 表示「棋盘是否变化」）。
- 结果：修正后 `test_tokencatch` 13 例全绿；Debug / Release CTest 各 **34/34**。
- 未放宽任何断言：`core::TokenCatch` 未做任何改动。
