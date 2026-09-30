# traps · P3 — 养成与数据层（真实踩坑记录）

> 对应 `ROADMAP-P3-Fin.md`。按 `README.md` §二.5 约定，**仅记录 P3 实施过程中真实复现**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。
>
> 本轮 P3 未出现崩溃类现象。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P3-001` | `expSpanForLevel()` 复用 `expNeeded()` 的等级夹取 → `expSpanForLevel(1) == 0` | 逻辑/单测 | 已解决 |
| `TRAP-P3-002` | `Schema::migrate(QSqlDatabase&)` 收 `db.db()`（按值临时量）→ MSVC **C2664** | 编译 | 已解决 |
| `TRAP-P3-003` | 养成数据目录落在 exe 同级 `data/`，构建与部署目录都会长出运行期产物 | 环境/产物 | 已记录（预期行为） |
| `TRAP-P3-004` | PATH 中 `D:/Strawberry/c/bin` 的旧 `cmake.exe` 遮蔽 CMake 4.4.2 → 无法创建 `Visual Studio 18 2026` 生成器 | 环境/构建 | 已解决 |
| `TRAP-P3-005` | 部署目录缺 `platforms/qoffscreen.dll`：以 `QT_QPA_PLATFORM=offscreen` 冒烟会「进程存活但不建库」 | 环境/验证 | 已解决（改用默认平台验证） |

---

## TRAP-P3-001 — `expSpanForLevel()` 的等级夹取退化

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

## TRAP-P3-002 — `Schema::migrate()` 无法接收按值返回的 `QSqlDatabase`

**类别**：编译错误 ｜ **影响**：`test_database` 无法编译，构建中断

### 现象

`cmake --build build --config Debug` 报错原文：

```
F:\develop\desktoppet\tests\test_database.cpp(114,9): error C2664: “bool whalepet::model::Schema::migrate(QSqlDatabase
&)”: 无法将参数 1 从“QSqlDatabase”转换为“QSqlDatabase &” [F:\develop\desktoppet\build\test_database.vcxproj]
```

### 根因

`Database::db()` 的签名是**按值返回**（与 Qt 惯例一致，`QSqlDatabase` 是轻量句柄）：

```cpp
QSqlDatabase db() const { return m_db; }
```

而 `Schema::migrate()` 需要非 const 左值引用（内部要执行 `QSqlQuery`）。
`Schema::migrate(db.db())` 传进去的是**临时对象**，无法绑定到非 const 左值引用。

### 解决

在调用点先取出具名副本再传入（也顺带明确了「迁移操作的是一个连接句柄」这一语义）：

```cpp
QSqlDatabase handle = db.db();
QVERIFY(Schema::migrate(handle));
```

`Database::schemaVersion()` 内部早有同样的写法（`QSqlDatabase handle = m_db;`），保持一致。

> 备选方案是把 `Database::db()` 改为返回 `QSqlDatabase&`，但那会暴露内部成员、
> 让调用方有机会在连接被 `close()` 后继续持有一只「半死」句柄，故未采用。

### 影响与关联文档

- 新增测试代码时，凡需要把连接传给 `Schema` / `QSqlQuery` 的地方，一律先取具名副本。
- `Database::close()` 已按「先释放所有 `QSqlQuery` 引用再 `removeDatabase()`」的顺序实现，
  避免 `QSqlDatabase: connection is still in use` 警告（见 `Database.cpp`）。

---

## TRAP-P3-003 — 养成数据目录会出现在 exe 同级 `data/`

**类别**：环境 / 产物 ｜ **影响**：无功能影响；仅影响「哪些文件算运行期产物」的认知

### 现象

按 `ROADMAP-P3-Fin.md` 的启动冒烟步骤运行后：

```powershell
$p = Start-Process -FilePath .\build\Release\WhalePet.exe -PassThru
Start-Sleep -Seconds 6
Stop-Process -Id $p.Id -Force
Get-ChildItem .\build\Release\data
```

输出：

```
whalepet.db      # 53248 字节
```

即**构建目录** `build/Release/` 下长出了一个 `data/` 目录；同理，部署目录
`deploy-release/` 下首次运行也会长出 `data/`。

### 根因

这是 `DATA-MODEL.md` §1 明确的设计：首选位置就是 **`<安装目录同级>/data/whalepet.db`**。
`DataPaths::resolve()` 取 `QCoreApplication::applicationDirPath() + "/data"`，
`Database::openAt()` 会用 `mkpath` 自动建目录——所以「跑一次就多一个 `data/`」是**预期行为**，
不是路径拼接错误。

### 解决 / 规避

- 无需改代码。需要注意的只是产物归类：
  - `build/**/data/` 属构建产物；
  - `deploy-release/data/` 属**用户数据**，覆盖部署（重新解压 / 重跑 `windeployqt`）时不应删除，
    否则养成进度与窗口位置会丢。
- 仓库已纳入版本管理并有 `.gitignore`：`build/`、`deploy-release/` 已整体忽略，
  其下的 `data/` 不会误提交。（若后续把数据目录改到仓库内非忽略位置，需补 `**/data/` 规则。）

### 验证方法（确认落盘位置正确）

`StatusPanel` 底部会显示实际生效的存储位置与模式（`PetWindow::storageInfo()`），
正常应为「存储：安装目录」+ `<路径>/data/whalepet.db`；若显示「用户目录（已降级）」或
「内存模式（不落盘）」，说明走了 `DATA-MODEL.md` §1 的降级分支。

### 影响与关联文档

- `DATA-MODEL.md` §1（存储位置）、`BUILD.md`（部署产物）、`ROADMAP-P3-Fin.md` §5/§实现状态。
- 构建期已加 `sqldrivers/qsqlite` 插件检查（缺插件直接 `FATAL_ERROR`），
  避免「插件缺失 → 静默降级到内存库 → 数据不落盘却不报错」这种「能跑但错」。

---

## TRAP-P3-004 — PATH 里的旧 `cmake.exe` 遮蔽基线 CMake，生成器不可用

**类别**：环境 / 构建 ｜ **影响**：configure 直接失败，且报错指向「生成器不存在」，容易误判为 CMake 版本坏了

### 现象

按 `BUILD.md` §2 初始化 PATH（把 `D:/Strawberry/c/bin` 等一并前置）后执行基线命令：

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt-debug"
```

报错原文（节选）：

```
CMake Error: Could not create named generator Visual Studio 18 2026
Generators
  Visual Studio 17 2022        = Generates Visual Studio 2022 project files.
  ...
```

即**报错列出的生成器清单里根本没有 `Visual Studio 18 2026`**。但与此同时
`cmake --version` 却是 `cmake version 4.4.2`（基线版本）。

### 根因

`D:/Strawberry/c/bin/cmake.exe` **确实存在**，且在 PATH 中排在
`C:\Program Files\CMake\bin` 之前，于是 `cmake` 解析到了这个随 Strawberry 附带的旧 CMake。
旧版不认识 VS 2026 生成器，所以清单里没有它。

判定依据：

```powershell
(Get-Command cmake).Source        # 未前置 PATH 时 → C:\Program Files\CMake\bin\cmake.exe
Test-Path 'D:/Strawberry/c/bin/cmake.exe'   # True ← 遮蔽源
(& 'C:\Program Files\CMake\bin\cmake.exe' --help) -match 'Visual Studio 18'
# → * Visual Studio 18 2026  = Generates Visual Studio 2026 project files.
```

### 解决 / 规避

- **一律用绝对路径调用基线工具**：
  `& 'C:\Program Files\CMake\bin\cmake.exe' ...`、`& 'C:\Program Files\CMake\bin\ctest.exe' ...`。
- 或把 `C:\Program Files\CMake\bin` 放到 PATH **最前**，不要放在 Strawberry 之后。
- 只按需把 Qt/Perl/NASM 加入 PATH：为跑测试注入 `D:\Qt-debug\bin` 时，**不要**顺手带上 `D:/Strawberry/c/bin`。

### 影响与关联文档

- `BUILD.md` §2/§3/§8（已补「用绝对路径调用 CMake」与排查项）。
- 与 `TRAP-P2-008` 同类：都是「环境注入不当导致命令解析到错的东西」，不是代码问题。

---

## TRAP-P3-005 — 部署目录缺 `qoffscreen.dll`，offscreen 冒烟会「假存活」

**类别**：环境 / 验证方法 ｜ **影响**：冒烟结论**假阳性/假阴性**——进程活着却什么都没跑，极易被当成「通过」

### 现象

把 Release 产物统一到 `deploy-release/` 后，用原有 offscreen 方式冒烟：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$p = Start-Process -FilePath .\deploy-release\WhalePet.exe -PassThru
Start-Sleep -Seconds 6
$p.HasExited      # → False（看起来「启动成功」）
Test-Path .\deploy-release\data    # → False（但数据库根本没建）
```

开启 stderr 后才看到真正原因：

```
qt.qpa.plugin: Could not find the Qt platform plugin "offscreen" in ""
```

`windeployqt` **只部署 `platforms/qwindows.dll`**，不会带上 `qoffscreen.dll`；
于是 `QApplication` 构造阶段就卡住（不进入 `main()` 之后的任何逻辑），
`PetWindow` 从未被构造，`Database` 自然也没打开——进程只是「挂在初始化阶段」。

（对照：`build\Release\WhalePet.exe` 同参数下能跑，因为它的 `Qt6Core.dll` 来自
`D:\Qt-debug\bin`，插件路径按 Qt 前缀解析，能命中 `D:/Qt-debug/plugins/platforms/qoffscreen.dll`。）

### 根因

Qt 的平台插件按「exe 同级 `platforms/`」查找。部署目录里没有 offscreen 插件，
`QT_QPA_PLATFORM=offscreen` 就无解；错误只落在一行 `qWarning` 上，
GUI 子系统（`WIN32`）进程没有控制台，默认看不到，于是表现为「静默假存活」。

### 解决 / 规避

- **验证部署目录时不要用 `offscreen` 平台**，改用默认（`qwindows`）跑真实桌面冒烟：
  ```powershell
  Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue
  $p = Start-Process -FilePath .\deploy-release\WhalePet.exe -PassThru
  Start-Sleep -Seconds 8
  Get-Process -Id $p.Id | Select-Object Threads,WorkingSet64
  Test-Path .\deploy-release\data     # 权威判据：库建出来了才算真跑起来
  Stop-Process -Id $p.Id -Force
  ```
- 判据必须是**副作用**（`data/whalepet.db` 生成、线程数 ≈ 30、WS ≈ 100MB），
  不能只用 `HasExited == $false`。
- 若确实需要无桌面 offscreen 冒烟：把 `D:\Qt-debug\plugins\platforms\qoffscreen.dll`
  一并拷入部署目录 `platforms/`（**属验证辅助，不随正式发布**）。
- 排障时用 `$env:QT_FORCE_STDERR_LOGGING='1'` + `-RedirectStandardError` 才能看到这类 `qWarning`。

### 影响与关联文档

- `BUILD.md` §3/§9（部署后最小化验证）、`ROADMAP-P3-Fin.md` 启动冒烟步骤、`TRAP-P2-008`（同为验证方法类）。
- **教训**：任何「进程还活着」的冒烟结论都要配一条产物断言，否则等于没验证。
