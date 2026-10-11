# P-112 · 笔触计数断言漏算「`begin` 自动收笔」语义（期望 4 实得 3）

| 字段 | 取值 |
|---|---|
| 阶段 | P11（绘画模块；core/CanvasBoard 画板笔触模型） |
| 原编号 | —（P11 新增） |
| 模块 | `tests/test_drawing.cpp`（`core::CanvasModel` 的用例） |
| 报错分层 | test（用例侧口径问题，模型语义正确） |
| 严重度 | minor（假失败；修正断言后 37/37 通过） |

## 一、现象（可复现步骤 / 报错原文）

`test_drawing` 首次运行时失败一条：

```text
FAIL!  : TestDrawing::canvasModelStrokeLifecycle() Compared values are not the same
   Actual   (model.strokes().size()): 3
   Expected (std::size_t(4))        : 4
F:\develop\desktoppet\tests\test_drawing.cpp(339) : failure location
```

用例最后一段的意图是「在已有进行中的笔时再 `begin`，实现会先自动收笔、不丢数据」。

## 二、根因

`CanvasModel::begin()` 的既定语义是：**若已有一笔正在进行，先自动 `end()`**（该笔进入历史），
随后开始新的一笔。断言时把「新开的这一笔」也算进了 `strokes()`：

- `begin()` 之后：历史 = 前 3 笔（含刚被自动收笔的那一笔），**新笔仍在进行中**（`drawing() == true`）；
- 只有 `end()` 之后，新笔才进入历史。

所以正确计数是「`begin` 后 3、`end` 后 4」，原断言的 4 是把未提交状态当成了已提交。

## 三、解决或规避

按真实语义改写断言，并顺带把「自动收笔没丢数据」这一真正要验证的点显式断言出来：

```cpp
model.begin({ 7.0f, 7.0f }, RgbColor{ 0, 255, 0, 255 }, 6, false);
QCOMPARE(model.strokes().size(), std::size_t(3));               // 上一笔被自动收笔，进入历史
QCOMPARE(model.strokes().back().points.size(), std::size_t(2)); // 自动收笔保留全部点
QVERIFY(model.drawing());                                       // 新的一笔正在进行
model.end();
QCOMPARE(model.strokes().size(), std::size_t(4));
QVERIFY(!model.drawing());
```

**规避口径**：对「集合 / 历史」类计数断言，先确认**哪些状态已提交**（本项目里 `begin/end`
是显式的提交边界），再换算期望值；断言失败时先按语义复核期望，不要把模型改得去迎合断言。

## 四、影响与关联文档

- 影响面：仅测试；产品逻辑未改一行。
- 关联：`docs/DRAWING.md` §6（画板笔触模型语义）、`core/CanvasBoard.h`（语义注释）、
  `docs/pitfalls/p11/P-111-*`（同日另一条用例侧口径问题）。
