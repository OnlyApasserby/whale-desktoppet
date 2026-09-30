# traps · P3 — 养成与数据层（真实踩坑记录）

> 对应 `ROADMAP-P3.md`。按 `README.md` §二.5 约定，**仅记录 P3 实施过程中真实复现**的问题。
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

按 `ROADMAP-P3.md` 的启动冒烟步骤运行后：

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
- 本仓库当前**没有 `.gitignore`**（也尚未 `git init`）。一旦纳入版本管理，必须先忽略
  `build/`、`deploy-release/`、`**/data/`，否则数据库文件会被误提交。

### 验证方法（确认落盘位置正确）

`StatusPanel` 底部会显示实际生效的存储位置与模式（`PetWindow::storageInfo()`），
正常应为「存储：安装目录」+ `<路径>/data/whalepet.db`；若显示「用户目录（已降级）」或
「内存模式（不落盘）」，说明走了 `DATA-MODEL.md` §1 的降级分支。

### 影响与关联文档

- `DATA-MODEL.md` §1（存储位置）、`BUILD.md`（部署产物）、`ROADMAP-P3.md` §5/§实现状态。
- 构建期已加 `sqldrivers/qsqlite` 插件检查（缺插件直接 `FATAL_ERROR`），
  避免「插件缺失 → 静默降级到内存库 → 数据不落盘却不报错」这种「能跑但错」。
