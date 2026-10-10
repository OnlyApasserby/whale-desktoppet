# P-099 · 测试把 UTF-8 中文文案按 `fromLatin1` 解码，断言乱码失败

- **模块**：`tests/test_tokencatch.cpp`（接 Token 纯逻辑用例）
- **报错分层**：test（QTest 断言）
- **严重度**：minor（用例自身写错，产品代码无缺陷）
- **原编号**：—（EX4 追加：接 Token 插件接入陪玩通用聚合的验证轮）

## 现象（可复现步骤 / 报错原文）

`ctest --test-dir build -C Debug` 首轮：`test_tokencatch` 2 个用例失败，报错原文（`-o <file>,txt` 取回）：

```
FAIL!  : TokenCatchTest::presetsAndConstantsAreConsistent() Compared values are not the same
   Actual   (QString::fromLatin1(tokenCatchPresetName(TokenCatchPreset::Easy))): "\u00E5\u0088\u009D\u00E7\u00BA\u00A7"
   Expected (QStringLiteral("初级"))                                         : "\u521D\u7EA7"
```

复现路径：断言 `QString::fromLatin1(core 层 const char* 中文名) == QStringLiteral("初级")`。

## 根因

`core::TokenCatchPresetDef::name` 是源码中的 **UTF-8 字节串**（`"初级"` = `E5 88 9D E7 BA A7`）。
测试用 `QString::fromLatin1()` 解析，等于把每个 UTF-8 字节各自当成一个 Unicode 码位，
于是得到 `åçº§` 这样的乱码（正是报错里的 `\u00E5\u0088\u009D\u00E7\u00BA\u00A7`），与 `QStringLiteral("初级")` 必然不等。

仓库既有视图一直是正确的口径，本条目只是「测试侧写错」：`MinesweeperView.cpp` 对同一类 preset 名用的是
`QString::fromUtf8(p.name)`；同理 `viewmodel::PosePresenter` / 台词表走 `std::string`（UTF-8 字节）
再用 `QString::fromStdString`（Qt 6 等价 `fromUtf8`）。

## 解决或规避

- 修复：断言改用 `QString::fromUtf8(...)`，并在用例里写明口径注释：

  ```cpp
  // 具名文案是 UTF-8 字节串（与扫雷 / 找小猫的 preset 名同一口径），
  // 一律用 fromUtf8 解析，不得用 fromLatin1（否则中文变乱码）。
  QCOMPARE(QString::fromUtf8(tokenCatchPresetName(TokenCatchPreset::Easy)),
           QStringLiteral("初级"));
  ```

- 规避（回灌为规则）：**`const char*` → `QString` 的转换按内容分档**——
  中文 / 非 ASCII 文案一律 `fromUtf8`（或经 `std::string` + `fromStdString`）；
  `fromLatin1` / `fromLocal8Bit` 只允许用于**纯 ASCII** 的稳定标识（难度 id、台词场景 key 等）。

## 影响与关联文档

- 关联规则：`docs/MINIGAME-INTERFACE.md` §12（接 Token）「难度文案解码」；同类既有实现见
  `src/minigame/minesweeper/MinesweeperView.cpp:225`（`fromUtf8(p.name)`）。
- 结果：修正后 `test_tokencatch` 13 例全绿；Debug / Release CTest 各 **34/34**。
- 未放宽任何断言：失败原因是用例写错，产品代码 `core::TokenCatch` 未做任何改动。
