# `expSpanForLevel()` 的等级夹取退化

> **原编号**：`TRAP-P3-001`　**阶段**：P3　**来源**：原按阶段聚合的 `traps-P3.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：逻辑错误 ｜ **影响**：状态面板经验进度条的分母会变成 `0`（`QProgressBar` 退化为空/异常），
纯逻辑单测直接失败

### 现象

首次跑 `test_growth` 即失败（`ctest -C Debug` 报 `test_growth ***Failed`），单测输出原文：

```
FAIL!  : TestGrowth::levelCurveDerivedFromExp() Compared values are not the same
   Actual   (expSpanForLevel(1)): 0
   Expected (kLevelStep)        : 500
tests/test_growth.cpp(72) : failure location
```

完整复现命令（Windows PowerShell）：

```powershell
$env:PATH="D:\Qt-debug\bin;$env:PATH"; $env:QT_QPA_PLATFORM='offscreen'
& .\build\Debug\test_growth.exe -o result.txt,txt ; Get-Content result.txt
```

### 根因

`core/GrowthRules.h` 最初写成：

```cpp
inline int expSpanForLevel(int level)
{
    const int lv = level < 1 ? 1 : level;
    return expNeeded(lv) - expNeeded(lv - 1);   // ← 问题在这里
}
```

而 `expNeeded()` 自身也做等级夹取：

```cpp
inline int expNeeded(int level)
{
    const int lv = level < 1 ? 1 : level;   // lv == 0 被夹到 1
    return kLevelStep * lv;
}
```

于是 `expSpanForLevel(1)` = `expNeeded(1) - expNeeded(0)` = `500 - 500` = **0**。
`level <= 1` 时区间长度恒为 0，恰好是「新号第一级」这个最常走到的分支。

> 这是**两个都自带夹取的函数相减**导致的退化：夹取在单个函数内是正确的，组合起来却引入了
> 「非单调」的隐式钳位。属于典型的「防御性代码叠加后语义被吃掉」。

### 解决

不再做「两次 `expNeeded` 相减」，直接按定义展开：

```cpp
inline int expSpanForLevel(int level)
{
    const int lv = level < 1 ? 1 : level;
    return expNeeded(lv) - kLevelStep * (lv - 1);   // 恒等于 kLevelStep（当前曲线）
}
```

并在函数上方写明**禁止**再改回相减写法的原因，避免后续维护者「顺手简化」把它改坏。

### 影响与关联文档

- `GAMEPLAY.md` §1 的曲线表述不变；若将来把曲线换成递增阶梯，只需同时改
  `expNeeded()` 与 `expSpanForLevel()` 两处，并让 `test_growth::expCurveIsMonotonic`
  与 `levelCurveDerivedFromExp` 继续兜住。
- 已由 `tests/test_growth.cpp` 的 `levelCurveDerivedFromExp()` 固化
  （`QCOMPARE(expSpanForLevel(1), kLevelStep)`），不会再静默回归。

---
