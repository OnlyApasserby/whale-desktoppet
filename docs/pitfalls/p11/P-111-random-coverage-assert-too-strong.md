# P-111 · 「完全随机 RGB」的覆盖断言按直觉写成三通道同时命中极端区间（概率仅约 32%）

| 字段 | 取值 |
|---|---|
| 阶段 | P11（绘画模块；core/DrawingPalette 随机配色） |
| 原编号 | —（P11 新增） |
| 模块 | `tests/test_drawing.cpp`（`core::randomRgb` 的用例） |
| 报错分层 | test（用例侧口径问题，产品逻辑正确） |
| 严重度 | minor（假失败；修正断言后 37/37 通过） |

## 一、现象（可复现步骤 / 报错原文）

`test_drawing` 首次运行时失败一条：

```text
FAIL!  : TestDrawing::randomRgbCoversFullRangeAndAlphaWindow() 'sawLow' returned FALSE. ()
F:\develop\desktoppet\tests\test_drawing.cpp(80) : failure location
Totals: 24 passed, 2 failed, 0 skipped, 0 blacklisted, 386ms
```

用例原意是验证「**完全随机 RGB**（用户 2026-10-11 口径）确实铺开全区间」，写法是：
在 200 次取色中，期待至少出现一次「三通道同时 < 32」（`sawLow`）与一次「三通道同时 > 224」（`sawHigh`）。

## 二、根因

期望值按「200 次抽样，极端区间总该出现过」的直觉设定，未核算概率：

- 「三通道同时 < 32」单次概率 = (32/256)³ = 0.00195 ⇒ 200 次里至少一次 ≈ **32%**；
- 于是**用固定种子跑出来的结果就是「没出现」** —— 断言失败是**用例期望过强**，
  不是 `randomRgb` 被约束住了（同批次另有逐通道区间断言全部为真）。

## 三、解决或规避

把「覆盖区间」这一意图改成**逐通道**的可核验写法（同时保留 alpha 区间与合法域断言）：

```cpp
int minChannel[3] = { 255, 255, 255 };
int maxChannel[3] = { 0, 0, 0 };
for (int i = 0; i < 200; ++i) {
    const RgbColor color = randomRgb(rng);
    /* …三通道与 alpha 合法域断言… */
    // 逐通道累计极值
}
// 「完全随机 RGB」的可观测性：每个通道都真正铺开了区间（不是被约束在窄带里）
for (int c = 0; c < 3; ++c) {
    QVERIFY(minChannel[c] < 64);
    QVERIFY(maxChannel[c] > 191);
}
```

这是**换一个等价且概率安全的可观测口径**，不是放宽条件：
`min < 64 且 max > 191` 在「被约束在窄带」（例如只取中段）时必然失败，
因此在同样能咬住「完全随机」的前提下把假失败概率降到可忽略。

## 四、影响与关联文档

- 影响面：仅测试；产品逻辑未改一行。
- 关联：`docs/DRAWING.md` §3（随机配色口径）、`docs/pitfalls/p11/P-112-*`（同日另一条用例侧口径问题）、
  `docs/TESTING.md`（自研测试策略）。
