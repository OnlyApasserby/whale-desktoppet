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
| `TRAP-P6-003` | `test_growth` 全量运行时一次性失败、**无任何断言输出**，随后不可复现 | 测试/时序 | 未定位（观察项） |
| `TRAP-P6-004` | PowerShell 5.1 把**无 BOM 的 UTF-8** `.ps1` 按 ANSI 解析，中文字面量乱码 → 脚本语法错误 | 脚本/编码 | 已解决 |

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

---

## TRAP-P6-003 — `test_growth` 全量运行时一次性失败、无断言输出，随后不可复现

**类别**：测试 / 时序 ｜ **影响**：P6 首次 Debug 全量 `ctest` 时 `test_growth` 判定 Failed，
但**无任何 QTest 断言文本**；随后同二进制单独运行、以及连续 3 次全量运行**均通过**。

### 现象

```powershell
cd f:/develop/desktoppet
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Debug --output-on-failure --timeout 120
```

- 首次：`5 - test_growth ... ***Failed 0.45 sec`，`1 tests failed out of 8`；
  尽管启用了 `-o -,txt`，CTest 仍**未打印任何断言文本**（该失败不具备可诊断输出）。
- `ctest -R test_growth`（单独）→ **Passed**。
- 随后连续 **3 次**全量 `ctest -C Debug` → **每次 100% passed（8/8）**。
- 加入新增 `test_settings` 后，Debug / Release 全量（9 项）→ **均 100% passed**。

### 状态

**未定位 / 暂缓（不可复现）**。按 `README.md` §六，异常一律交回用户调试、AI 不自行插桩排查；
本项非崩溃（进程正常退出，仅测试判 Failed），且无输出，无法据此定位。

**已排除项（已验证）**：

- 与本次 P6 改动无关：本次未触碰 `GrowthService` / `core::GrowthRules` / `PetStateRepo`；
  改动集中在 `SettingsRepo`（`json_ext` 扩展键）、`PetStateMachine`（`night_quiet` 开关）、
  视图层（`PoseView` / `SpeechBubble` / `ContentPanel` / 新增 `SettingsDialog`）与 `PetWindow`。
- 非插件问题（已验证）：`build/Debug` 未执行过 `windeployqt`，Qt 回退到前缀
  `D:/Qt-debug/plugins`，`qoffscreend.dll` 可用——同一批次其余 GUI / 逻辑测试均正常。

**推测（未验证，仅供参考）**：可能与该用例内部的时间/临时目录时序有关，但**未取得证据**，不作结论。

### 影响与关联文档

- 关联：`docs/TESTING.md`（`test_growth`）、`docs/BUILD.md` §9。
- 与 `TRAP-P2-007` 同类：**未复现的长期观察项**，不阻塞 P6 验收。若再次出现且**带断言输出**，
  按 `traps-Pn.md` 规范补记根因。

---

## TRAP-P6-004 — PowerShell 5.1 按 ANSI 解析无 BOM 的 UTF-8 脚本，中文乱码致语法错误

**类别**：脚本 / 文件编码 ｜ **影响**：打包脚本 `packaging/make-package.ps1` 首次执行即失败，
`powershell -File` 直接报解析错误，脚本**一行都没执行**（非运行期问题，是词法阶段）。

### 现象

可复现步骤：

```powershell
cd f:/develop/desktoppet
& powershell -NoProfile -ExecutionPolicy Bypass -File .\packaging\make-package.ps1
```

报错原文（节选）：

```
At F:\develop\desktoppet\packaging\make-package.ps1:51 char:1
+ } else {
+ ~
Unexpected token '}' in expression or statement.
At ...:70 char:85
+ ... '    娓呯悊璋冭瘯鏂囦欢锛? + (($junk | ForEach-Object { $_.Name }) -join ', '))
The string is missing the terminator: '.
```

关键观察：报错正文里的中文字面量呈**乱码**（`娓呯悊璋冭瘯鏂囦欢` = UTF-8 字节被按 GBK 解码的结果）；
失配的是 `{ }` 与成对引号，即**解析**层面。

### 根因

**Windows PowerShell 5.1 对没有 BOM 的 `.ps1` 文件按系统 ANSI 代码页（本机 GBK）解码**，而非 UTF-8。
脚本以 UTF-8（无 BOM）保存，其中的中文（注释与字符串字面量）被逐字节按 GBK 解释产生乱码；
乱码序列中出现引号 / 特殊字符，破坏了字符串与语句块的配对，于是解析失败。

要点：**与脚本逻辑无关**——同一文本以「UTF-8 带 BOM」或「ANSI(GBK)」保存可正常解析，
但两种编码在代码页不同的机器上又会各自出问题。

### 解决

把 `make-package.ps1` 改为**纯 ASCII**（英文注释与输出），不依赖任何代码页，5.1 与 7.x 均可解析。

```powershell
Write-Host '==> [1/4] Configure release build (WHALEPET_PACKAGE=ON, output dist/WhalePet)'
```

对照：NSIS 脚本 `whalepet.nsi` **保留中文**，但走另一条路——`makensis /INPUTCHARSET UTF8` 显式声明
源文件编码，故不受系统 ANSI 影响。

### 影响与关联文档

- 关联：`packaging/make-package.ps1`、`docs/BUILD.md` §10、`docs/README.md` §六。
- 教训：**交给 Windows PowerShell 5.1 的脚本，要么纯 ASCII，要么显式写 UTF-8 BOM**；
  需要中文输出时优先 BOM，且不得依赖「运行机器的默认代码页」。
