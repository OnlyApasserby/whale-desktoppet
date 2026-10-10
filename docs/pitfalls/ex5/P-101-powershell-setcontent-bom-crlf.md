# P-101 · PowerShell 5.1 `Set-Content -Encoding UTF8` 重写源码：引入 BOM/CRLF，编辑工具随即无法匹配

| 字段 | 取值 |
|---|---|
| 原始编号 | —（EX5 新编号） |
| 阶段 | EX5 · 小游戏体验优化（国际象棋难度梯度与棋盘朝向 / 找小猫地图与文案） |
| 模块 | `scripts/`（本地验证工具链，非产品代码） |
| 报错分层 | script |
| 严重度 | minor |
| 状态 | 已解决 |

## 1. 现象（可复现步骤 / 报错原文）

为做「反向验证」，用 PowerShell 一行式批量破坏 4 个文件的守卫点（`Get-Content -Encoding UTF8 -Raw` +
`-replace` + `Set-Content -Encoding UTF8`）：

```powershell
(Get-Content -Encoding UTF8 src/core/Chess.cpp -Raw) -replace '<old>', '<new>' | Set-Content -Encoding UTF8 src/core/Chess.cpp
```

随后再用编辑工具改回时，`replace_in_file` 连续 4 次失败：

```text
There was an error with the search/replace, and it was NOT applied.
The string to replace was not found in the file (even after relaxing whitespace).
```

直接检查字节：

```powershell
[System.IO.File]::ReadAllBytes('src/core/Chess.cpp')[0..2]   # → 239 187 191（EF BB BF = UTF-8 BOM）
```

4 个被 `Set-Content` 重写过的文件**全部**带上 BOM，且行尾由 LF 变成 CRLF。

## 2. 根因

1. **PS 5.1 的 `Set-Content -Encoding UTF8` 会写 BOM**（`-Encoding UTF8` 在 Windows PowerShell 里等价于
   UTF-8 **with BOM**；无 BOM 需用 `[System.IO.File]::WriteAllBytes` 或 `New-Object System.Text.UTF8Encoding($false)`）。
2. 同时按平台默认把整文件行尾统一成 CRLF —— 这是一次**全文件重写**，不是局部替换。
3. 编辑工具是按「工程既有的无 BOM + LF 文本」建立匹配的；BOM 是**内容字节**而非空白，
   「放宽空白」帮不上忙，于是匹配全线失败（这也是本次能立刻察觉的原因）。

与 `P-035`（PS 5.1 按 ANSI 解析无 BOM 的 UTF-8 脚本）属同一族：**Windows PowerShell 的默认编码行为坑**。

## 3. 解决或规避

- 立刻用 Python 以**二进制**方式写回，去掉 BOM 并把 CRLF 归一化为 LF，恢复工程既有风格（与 P-102 同批处理）。
- 流程上改为：**反向验证的破坏点用编辑工具逐个改**（`replace_in_file`），不再用 shell 批量文本替换源码；
  若确需批量处理，用 Python 且显式指定 `encoding='utf-8'`、`newline='\n'`（且不要写 BOM）。
- 校验手段固定为「读首 3 字节 + `read_file` 可正常读取」两条。

## 4. 影响与关联文档

- 影响范围：仅本地验证流程；4 个文件被重写（未提交，`core.autocrlf` 会在提交时归一化行尾，BOM 已清除），
  恢复后重新构建 + 全量回归通过（Debug / Release 各 34/34）。
- 关联：`P-102`（同批的 Python 清空事故）、`P-035`（PS 5.1 编码坑同族）、`docs/README.md` §六（全量回归要求）。
