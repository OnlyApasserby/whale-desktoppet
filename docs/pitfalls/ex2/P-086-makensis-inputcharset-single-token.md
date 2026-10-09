# P-086：makensis 参数非法（`/INPUTCHARSET` 的值必须为独立 token）

- **原编号**：—（EX2 阶段新增，序号接续 `P-085`）
- **模块**：scripts / NSIS 打包（`scripts/package-release.ps1` → `makensis`）
- **报错分层**：脚本运行期（外部工具退出码 1）
- **严重度**：中（阻断安装包产出）

## 现象（可复现步骤 / 报错原文）

修复 `P-085` 后重跑发布脚本，`windeployqt` 与 zip 均通过，`makensis` 步骤失败并**打印自身 Usage**：

```
Usage:
  makensis [ option | script.nsi | - ] [...]
Options:
  /Vx verbosity where x is 4=all,3=no script,2=no info,1=no warnings,0=none
  /INPUTCHARSET <ACP|OEM|CP#|UTF8|UTF16<LE|BE>>
  ...
FAILED (exit 1): makensis
At F:\develop\desktoppet\scripts\package-release.ps1:127 char:9
```

## 根因

脚本把选项与取值拼成了**一个含空格的参数**：

```powershell
('/INPUTCHARSET ' + $NsisInputCharset)   # → '/INPUTCHARSET UTF8'
```

makensis 的命令行契约是 `/INPUTCHARSET <value>` ——**两个独立 token**。
拼成单参数后（且含空格会被引号包裹），makensis 无法识别该选项，于是打印 Usage 并以退出码 1 结束。

> 该缺陷此前被 `P-085` 的误报掩盖：流程从未走到 makensis 步骤，故长期未被发现。

## 解决或规避

拆成两个数组元素：

```powershell
'/INPUTCHARSET', $NsisInputCharset,
```

（`Quote-Argument` 对不含空白 / 引号的 token 不加引号，两者原样传入。）
重跑后 `[release] installer: WhalePet-0.2.0-setup.exe` 产出成功（13.3 MB）。

## 影响与关联文档

- 影响：安装包无法产出；修复后三个产物齐备（portable / zip / setup）。
- 关联：`scripts/package-release.ps1`、`scripts/installer.nsi`、`docs/release.md`、
  `docs/pitfalls/ex2/P-085-powershell-start-process-exitcode-null.md`。
