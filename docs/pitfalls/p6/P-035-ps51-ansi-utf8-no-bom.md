# PowerShell 5.1 按 ANSI 解析无 BOM 的 UTF-8 脚本，中文乱码致语法错误

> **原编号**：`TRAP-P6-004`　**阶段**：P6　**来源**：原按阶段聚合的 `traps-P6.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：脚本 / 文件编码 ｜ **影响**：打包脚本 `scripts/make-package.ps1` 首次执行即失败，
`powershell -File` 直接报解析错误，脚本**一行都没执行**（非运行期问题，是词法阶段）。

### 现象

可复现步骤：

```powershell
cd f:/develop/desktoppet
& powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\make-package.ps1
```

报错原文（节选）：

```
At F:\develop\desktoppet\scripts\make-package.ps1:51 char:1
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

对照：NSIS 脚本 `installer.nsi` **保留中文**，但走另一条路——`makensis /INPUTCHARSET UTF8` 显式声明
源文件编码，故不受系统 ANSI 影响。

### 影响与关联文档

- 关联：`scripts/make-package.ps1`、`docs/BUILD.md` §10、`docs/README.md` §六。
- 教训：**交给 Windows PowerShell 5.1 的脚本，要么纯 ASCII，要么显式写 UTF-8 BOM**；
  需要中文输出时优先 BOM，且不得依赖「运行机器的默认代码页」。

---
