# `qs()` 缺 `const char*` 重载，`nullptr` 经隐式转换进 `strlen` 崩溃

> **原编号**：`TRAP-P6-001`　**阶段**：P6　**来源**：原按阶段聚合的 `traps-P6.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

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
