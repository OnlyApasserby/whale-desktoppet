# P-106 · 在 `[CmdletBinding()]` 之前插入赋值语句，PowerShell 脚本直接无法解析

| 字段 | 取值 |
|---|---|
| 阶段 | P10（工作能力插件；第一阶段：构建与部署目录收敛） |
| 原编号 | —（P10 新增） |
| 模块 | `scripts/package-release.ps1` |
| 报错分层 | script（脚本解析期，语法自检即捕获） |
| 严重度 | minor（未进入构建/发布流程，自检阶段即发现并修正） |

## 一、现象（可复现步骤 / 报错原文）

为把构建目录固定为 `build`，曾把常量赋值写到参数块之前：

```powershell
# ...（脚本头部注释块）...
#>
$BuildDir = 'build'

[CmdletBinding()]
param(
    ...
)
```

不执行脚本、只用解析器即可复现：

```powershell
$tokens=$null; $errs=$null
$null=[System.Management.Automation.Language.Parser]::ParseFile('scripts\package-release.ps1',[ref]$tokens,[ref]$errs)
$errs | ForEach-Object { $_.Message }
```

报错原文：

```
意外的属性"CmdletBinding"。
表达式或语句中包含意外的标记"param"。
赋值表达式无效。赋值运算符输入必须是能够接受赋值的对象，例如变量或属性。
（「赋值表达式无效」同类共 12 条）
```

## 二、根因

PowerShell 要求 `[CmdletBinding()]` / `param(...)` 必须是脚本（或函数体）的**首部语句**：
属性与 `param` 之间不允许插入任何可执行语句。属性出现在非首部位置时，解析器把
`[CmdletBinding()]` 报成「意外的属性」、把其后的 `param` 报成「意外的标记」，随后整段参数块
被当作普通表达式继续解析，于是每个参数绑定各报一条「赋值表达式无效」。

## 三、解决或规避

固定构建目录改为**普通赋值，放在 `param` 之后**（本次落在 `$ErrorActionPreference = 'Stop'` 之后）：

```powershell
[CmdletBinding()]
param(
    ...
)

$ErrorActionPreference = 'Stop'

# Fixed build directory (contract at the top): ...
$BuildDir = 'build'
```

规避口径：

- **脚本首部只放属性与 `param`**，任何常量 / 默认值初始化一律放到 `param` 之后；
- 改动 `scripts/*.ps1` 后，先用 `Parser::ParseFile` 做一次**语法自检**（只读、不执行、不写回文件），
  再进构建 / 发布流程；本轮的语法自检正是捕获点。

## 四、影响与关联文档

- 影响面：仅限脚本自身；本轮未影响构建与发布（自检阶段即修正）。
- 关联：`docs/pitfalls/ex6/P-104-build-redirect-pipe-deadlock.md`（同类「脚本层先自检、再入流程」的口径）、
  `docs/pitfalls/p6/P-035-ps51-ansi-utf8-no-bom.md`（`scripts/*.ps1` 纯 ASCII 约定，本次改动未破坏）、
  `scripts/package-release.ps1` 头部契约注释、`docs/BUILD.md` §10。
