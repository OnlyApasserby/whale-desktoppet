# P-097 · 发布产物随包不含 `qoffscreen.dll`，发布级冒烟不能用 offscreen

- **模块**：`scripts/package-release.ps1`（windeployqt 部署口径）/ `docs/TESTING.md` §8.3
- **报错分层**：script（发布编排）
- **严重度**：minor（易被误判为「程序崩溃 / 环境缺库」）
- **原编号**：—（EX4 新增）

## 现象（可复现步骤 / 报错原文）

EX4 打包回归时，按既有自动化用例的口径准备对**发布产物**做 offscreen 冒烟：

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
& 'dist\WhalePet-0.3.0-portable\WhalePet.exe'
```

但产物根本起不来。核验暂存目录后发现：

```
== platforms ==
qwindows.dll
```

`platforms/` 里**只有 `qwindows.dll`，没有 `qoffscreen.dll`**。

## 根因

`windeployqt` 按**部署目标**（Windows GUI 程序）挑选平台插件，`offscreen` 平台既不是发布目标，
也不被 `qwhale` 主程序依赖，因此**不会**被写进发布目录。这与既有两条陷阱同源但成因不同：

- `P-022` / `P-023`：部署目录**应该**有 `qoffscreen.dll` 却漏了（用于自动化冒烟）；
- 本条：发布目录**本就不该**有 `qoffscreen.dll`（它不属于分发物）。

也就是说「`qoffscreen.dll` 缺失」在**构建目录**里是缺陷，在**发布产物**里是正常。
两者不能混用同一套冒烟口径。

## 解决或规避

- 解决：本轮改用**干净 PATH + 真实平台 + 有界存活**做发布级冒烟，实测存活 12s →
  运行库齐备（缺 DLL 会立即以 `0xC0000135` 退出）；命令见 `docs/TESTING.md` §8.3。
- 规避：
  1. **发布级冒烟不用 offscreen**，用干净 PATH（仅 `C:\Windows\System32;C:\Windows`）确保不依赖开发机 Qt；
  2. **自动化用例仍用 offscreen**（跑在 `build/` 上，可显式注入 `qoffscreen.dll`），两条口径分开写；
  3. 退出码若为 `0xC0000135` / `0xC0000409` 等，**一律不自行调试**，按 `README.md` §六交回用户。

## 影响与关联文档

- 关联：`scripts/package-release.ps1`、`docs/TESTING.md` §8.3、`docs/packages.md` §1、
  `docs/release.md` §1.3；同类陷阱 `P-022` / `P-023`。
- 结果：本轮免安装版在干净 PATH 下存活 12s 被判定通过；发布脚本退出码 0。