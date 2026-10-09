# P-085：发布脚本误报工具失败（PowerShell 5.1 `Start-Process -PassThru` 的 `ExitCode` 为 `$null`）

- **原编号**：—（EX2 阶段新增，序号接续 `P-084`）
- **模块**：scripts / 发布打包（`scripts/package-release.ps1`）
- **报错分层**：脚本运行期（PowerShell 5.1）
- **严重度**：中（阻断发布流程；属**误报**，被调工具实际正常）

## 现象（可复现步骤 / 报错原文）

1. 在 PowerShell **5.1**（`5.1.26100.9444`）中运行发布脚本（复用既有 `build-package`）：
   `powershell -ExecutionPolicy Bypass -File scripts\package-release.ps1 -AppName WhalePet -Version 0.2.0 -BuildDir build-package`
2. 流程走到 `windeployqt` 步骤即中止：

```
FAILED (exit ): windeployqt
所在位置 F:\develop\desktoppet\scripts\package-release.ps1:101 字符: 9
+         throw ('FAILED (exit ' + $p.ExitCode + '): ' + $Label)
```

注意错误里的 `exit ` 后**为空**（没有数字）。

最小复现（与脚本原实现同构）：

```powershell
$p = Start-Process -FilePath 'D:\Qt-debug\bin\windeployqt.exe' -ArgumentList '--version' -NoNewWindow -PassThru
$ok = $p.WaitForExit(60000)
"WaitOk=$ok HasExited=$($p.HasExited) ExitCode=[$($p.ExitCode)]"
# 实测：WaitOk=True HasExited=True ExitCode=[]      <-- ExitCode 为 $null
```

## 根因

`Start-Process -PassThru` **不带 `-Wait`** 时，PowerShell 5.1 返回的 `Process` 对象不保留可用于读取退出码的进程句柄，
其 `ExitCode` 属性为 `$null`——`HasExited` 为 `True` 也不能使其可读。

脚本原判断：

```powershell
if ($p.ExitCode -ne 0) { throw ... }   # $null -ne 0 → $true
```

`$null -ne 0` 恒为真，于是**即使 windeployqt 实际以 0 退出也会抛错**，表现为 `FAILED (exit ): windeployqt`。
（`windeployqt --version` 实测输出 `6.8.4`，工具本身正常。）

## 解决或规避

`Invoke-Checked` 改为直接驱动 `System.Diagnostics.Process`（函数接口与超时语义不变）：

```powershell
$psi = New-Object System.Diagnostics.ProcessStartInfo   # UseShellExecute = $false
$proc = New-Object System.Diagnostics.Process; $proc.StartInfo = $psi
if (-not $proc.Start()) { throw ... }
if (-not $proc.WaitForExit($Timeout)) { $proc.Kill(); throw ... }
$code = $proc.ExitCode                                  # 可读
```

因 PS 5.1（.NET Framework）没有 `ProcessStartInfo.ArgumentList`，新增 `Quote-Argument` 显式拼装命令行
（只对含空白 / 引号的参数加引号）。
验证：同一调用在修复后得到 `ExitCode=[0]`；完整脚本 `windeployqt` 步骤通过。

## 影响与关联文档

- 影响：发布流程被**误报**阻断（工具实际成功）；修复后 windeployqt 步骤通过，并暴露出真正的第二个缺陷 `P-086`。
- 关联：`scripts/package-release.ps1`、`docs/release.md`、`docs/pitfalls/ex2/P-086-makensis-inputcharset-single-token.md`。
