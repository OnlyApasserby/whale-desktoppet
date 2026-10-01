# traps · P6 — 设置 / 打磨 / 测试 / 打包（真实踩坑记录）

> 对应 `ROADMAP-P6.md`。按 `README.md` §二.5 约定，**仅记录 P6 实施过程中真实复现**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P6-001` | 测试辅助 `qs()` 只有 `std::string` 重载，接不住 `matchKeyword()` 返回的 `nullptr` → `strlen(nullptr)` 崩溃 | 测试/UB | 已解决 |
| `TRAP-P6-002` | `HotwordRepo::upsert` 用「先删后插」，重绑关键词会给旧词分配新 id → 热词优先级顺序被打乱 | 逻辑/持久化 | 已解决 |

---

## TRAP-P6-001 — `qs()` 缺 `const char*` 重载，`nullptr` 经隐式转换进 `strlen` 崩溃

**类别**：测试代码 / 未定义行为 ｜ **影响**：`test_hotword` 命中「自定义热词全为脏数据 → 无命中」
分支时进程访问违例（ctest 判定 `SEGFAULT`），P6 热词用例无法跑绿。

### 现象

可复现步骤：

```powershell
cd f:/develop/desktoppet
cmake --build build --config Debug --target test_hotword
ctest --test-dir build -C Debug -R test_hotword --output-on-failure
```

- 构建配置：Visual Studio（多配置）生成器，Config = `Debug`。
- 二进制：`f:\develop\desktoppet\build\Debug\test_hotword.exe`（同目录 `test_hotword.pdb`，带符号）。
- 结果：`1 tests failed out of 2 … 8 - test_hotword (SEGFAULT)`。

用户用 WinDbg 取 `kb`（输出留档 `tests/dump.txt`，不入库），关键帧：

```
00 ucrtbased!strlen+0x31
01 test_hotword!std::_Narrow_char_traits<char,int>::length+0x14   [__msvc_string_view.hpp @559]  ← 首参 = 0x0
02 test_hotword!std::basic_string<char,...>::basic_string<char const*>+0x33 [xstring @819]        ← 第 2 参 = 0x0
03 test_hotword!TestHotword::dirtyHotwordIsSkipped+0x5c4 [F:\develop\desktoppet\tests\test_hotword.cpp @ 120]
04 test_hotword!TestHotword::qt_static_metacall  [test_hotword.moc @136]
```

即：`basic_string(const char* = nullptr)` → `char_traits::length(nullptr)` → `strlen(nullptr)`。
帧 `03` 行号 `@120`，与堆栈上「空指针」一致；`qs` 因内联没有独立帧。

### 根因

`tests/test_hotword.cpp` 的辅助函数只有一个重载：

```cpp
QString qs(const std::string &s) { return QString::fromStdString(s); }
```

而第 120 行传入的是 `core::matchKeyword()` 的返回值，类型为 `const char*`，其 API **约定无命中时返回
`nullptr`**：

```cpp
const std::vector<CustomHotword> allDirty = { { "", "omg" }, { "开服", "nope" } };
QCOMPARE(qs(matchKeyword(QStringLiteral("今晚开服").toStdString(), allDirty)), QString());
```

两条热词全非法（空词 / 非法 id）→ `matchKeyword` 返回 `nullptr`。由于缺少 `const char*` 重载，
编译器改选 `qs(const std::string&)`，需要一次用户自定义转换 `const char* → std::string`
（`basic_string(const char*)`）；该构造函数的前提是**合法的非空 C 字符串**，内部直接
`char_traits::length(ptr)` → `strlen(ptr)`，传 `nullptr` 即 UB → 读地址 `0x0` → 访问违例。

要点：崩溃发生在 `qs` **函数体之前**（`QString::fromStdString` 从未执行），所以在既有重载体内判空无效；
`std::string` 本身不可能为「空指针」，既有重载对它的契约是安全的，缺的只是承接裸指针的那条路。

### 解决

为裸指针单独提供重载并显式判空（其余调用点不变）：

```cpp
QString qs(const char *s)          { return s ? QString::fromUtf8(s) : QString(); }
QString qs(const std::string &s)   { return QString::fromStdString(s); }
```

重载决议：`const char*` 实参精确匹配新重载（不再走 `std::string` 的用户自定义转换）；
`std::string` 实参仍走原重载。`nullptr` 被显式映射为 `QString()`，与断言里期望的空串一致。

### 影响与关联文档

- 关联：`docs/CHAT.md` §4（自定义热词）、`docs/DATA-MODEL.md` §3.9（`hotwords` 表）、`docs/TESTING.md`。
- 教训：测试辅助函数承接「可能返回 `nullptr` 的 C 接口」时，必须提供裸指针重载（或改用返回
  `std::string` 的接口），不能依赖 `const char* → std::string` 的隐式转换。

---

## TRAP-P6-002 — `upsert` 先删后插，重绑关键词会重排热词优先级

**类别**：逻辑 / 持久化 ｜ **影响**：用户修改某条热词绑定的关键词后，该热词的**优先级位置**
被无声挪到列表末尾，与「按录入顺序（`id` 升序）决定优先级」的产品语义不符；表现为「改完关键词后
这条热词不生效了」（被后面的热词抢先匹配）。

### 现象

`test_hotword` 用例 `repoKeepsInsertOrderAndRemoves()` 断言失败（Debug）：

```
FAIL!  : TestHotword::repoKeepsInsertOrderAndRemoves() Compared values are not the same
   Actual   (items[0].word)        : "\u4E59"      ← 乙
   Expected (QStringLiteral("甲")): "\u7532"       ← 甲
F:\develop\desktoppet\tests\test_hotword.cpp(197) : failure location
Totals: 11 passed, 1 failed
```

用例：依次录入 甲 / 乙 / 丙，再把「甲」重绑为 `sike`（第 194 行），
期望 `loadAll()[0]` 仍是「甲」（覆盖不改变位置），实际变成「乙」。

### 根因

原实现为规避 `ON CONFLICT` 兼容问题，采用**先删后插**：

```cpp
DELETE FROM hotwords WHERE word = :w;   // 删掉旧「甲」（id=1）
INSERT INTO hotwords(...) VALUES(...);  // 重新插入「甲」→ AUTOINCREMENT 分到新 id=4
```

`hotwords.id` 是 `INTEGER PRIMARY KEY AUTOINCREMENT`，删除后再插入必然拿到**更大的新 id**；
而 `loadAll()` 以 `ORDER BY id ASC` 表达优先级，于是「甲」被排到「乙」「丙」之后。
即：把「更新」实现成了「删除 + 追加」，主键身份没保住。

### 解决

`upsert` 改为**先 UPDATE、命中即返回**，未命中才 INSERT，从而保住原 `id`：

```cpp
UPDATE hotwords SET keyword_id = :k, created_ms = :ms WHERE word = :w;
if (upd.numRowsAffected() > 0) { return true; }   // 既有记录：id 不变
// 否则 INSERT ...
```

同样不使用 `UPSERT` / `ON CONFLICT`，对老 SQLite 驱动仍兼容；归一化与 `keywordIdValid`
校验逻辑不变。

### 影响与关联文档

- 关联：`docs/CHAT.md` §4（自定义热词，优先级=录入顺序）、`docs/DATA-MODEL.md` §3.9（`hotwords` 表）。
- 教训：凡用「自增主键顺序」承载业务次序（优先级 / 排序）的表，**更新**语义必须原地 `UPDATE`，
  不得用「删 + 插」代替，否则主键（= 次序）会被悄悄改写。
