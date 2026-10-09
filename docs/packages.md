# WhalePet · 打包与分发（免安装版 + NSIS 安装包）

> 适用脚本：`scripts/package-release.ps1`（一键打包）、`scripts/installer.nsi`（NSIS 安装程序）。
> 相关文档：`BUILD.md` §10（构建流程）、`DATA-MODEL.md` §1（存档位置与降级）、`README.md` §六（崩溃/调试约定）、`docs/pitfalls/`。
>
> **本文档是打包脚本的唯一维护指南**：凡改动安装内容、卸载内容、注册表、快捷方式或运行期可写路径，
> 都必须按 §5 的同步清单更新脚本与本文档，二者不得脱节。

---

## 1. 打包链与产物

```powershell
# 前置（只做一次 / 每次改代码后重做）：build-package 必须已存在并已构建
#   cmake -S . -B build-package -G "Visual Studio 18 2026" -A x64 `
#         -DCMAKE_PREFIX_PATH="D:/Qt-debug" -DWHALEPET_PACKAGE=ON
#   cmake --build build-package --config Release
powershell -ExecutionPolicy Bypass -File scripts/package-release.ps1 `
    -AppName WhalePet -Version 0.3.0 -BuildDir build-package
```

| 步骤 | 动作 | 关键点 |
|---|---|---|
| 1 | 校验并**复用** `build-package`（`-DWHALEPET_PACKAGE=ON`） | 目录**必须已存在**；拒绝 `CMAKE_BUILD_TYPE=Debug` 的目录；脚本**从不新建 / 清理 / 切换**构建目录。Release 产物（`WhalePet.exe` 与桥接进程 `whalepet-mcp.exe`）落在 `dist/WhalePet`，**不生成调试符号** |
| 2 | 暂存到 `dist/WhalePet-<版本>-portable/` | 主程序按 `<构建目录>/<Config>/<App>.exe` → `dist/WhalePet/WhalePet.exe` 顺序自动定位；`-ExtraExe`（默认 `whalepet-mcp.exe`）随包暂存 |
| 3 | `windeployqt --release --no-translations --compiler-runtime --dir <暂存目录>` | 补齐 Qt 运行库与插件；对 `WhalePet.exe` **与** `whalepet-mcp.exe` 一并部署 |
| 4 | 清理 `*.pdb / *.ilk / *.exp / *.lib`，清空并重建空目录 `engine/`，清空（不重建）`plugins/`，复制 `README.md`、`LICENSE` | 运行期数据（`data/`、`stomach/`）、用户自备引擎与第三方插件**不打包** |
| 5 | `Compress-Archive` | 产出 `dist/WhalePet-<版本>-portable.zip` |
| 6 | `makensis /V2 /INPUTCHARSET UTF8`（传 `/DAPP_NAME` `/DAPP_VERSION` `/DAPP_VERSION4` `/DSRC_DIR` `/DOUT_FILE`） | 产出 `dist/WhalePet-<版本>-setup.exe` |

| 产物 | 说明 |
|---|---|
| `dist/WhalePet-<版本>-portable/` | 免安装版目录（**不含** `data/`、`stomach/` 内容；含**空的** `engine/` 目录，**不含** `plugins/`） |
| `dist/WhalePet-<版本>-portable.zip` | 免安装版压缩包（直接分发） |
| `dist/WhalePet-<版本>-setup.exe` | NSIS 安装包（`WhalePet.exe` + `whalepet-mcp.exe` + Qt 运行库 + 快捷方式 + 卸载程序） |
| `dist/WhalePet/` | CMake 在 `WHALEPET_PACKAGE=ON` 时的 exe 落点，属**构建中间产物**，**不是**发布产物 |

- **版本号只需一处输入**：`-Version`（如 `0.3.0`）同时决定产物文件名、注册表 `DisplayVersion` 与
  `installer.nsi` 的 `VIProductVersion`（4 段值由脚本补零推导后以 `/DAPP_VERSION4` 传入，不再手工维护）；
  另需与顶层 `CMakeLists.txt` 的 `project(... VERSION ...)` 保持一致。
- NSIS 脚本为 UTF-8，必须 `/INPUTCHARSET UTF8`（中文界面）；`scripts/*.ps1` 刻意保持纯 ASCII
  （PowerShell 5.1 会把无 BOM 的 UTF-8 脚本按 ANSI 解析，见 `docs/pitfalls/p6/P-035-ps51-ansi-utf8-no-bom.md`）。
- **改动本节任何脚本 / 安装↔卸载对应表 / 随包清单后，全量回归必须包含发布脚本回归**：
  触发条件、11 项核验清单与发布级冒烟口径见 [`TESTING.md`](TESTING.md) **§8**，
  台账登记见 [`release.md`](release.md)。本轮实测固化的两条口径：
  1. 发布产物 `platforms/` **只有 `qwindows.dll`，不含 `qoffscreen.dll`** —— 发布级冒烟**不能**用
     offscreen，必须走「干净 PATH + 真实平台 + 有界存活」（`P-097`）；
  2. 安装包 `RequestExecutionLevel admin` —— **静默安装 / 静默卸载往返需提权**，自动化会话无法完成时
     **如实登记为待人工验收，不得记作通过**（`P-098`）。
- 发布命名与台账见 [`release.md`](release.md)。

---

## 2. 安装清单：安装 / 卸载 / 权限「三处对应表」

> 安装目录默认 `$PROGRAMFILES64\WhalePet`（64 位、按机器安装，`RequestExecutionLevel admin`；
> 用户可在目录页改到任意位置，如 `D:\WhalePet`）。
>
> **任何一条「安装 Section 写入的东西」，都必须在下表里有对应的卸载条目**；否则卸载会残留，
> 并连带 `RMDir "$INSTDIR"`（仅在目录为空时才生效）失败，把整个安装目录留在磁盘上。

| 安装内容 | 来源 | 安装 Section | 卸载 Section | 备注 |
|---|---|---|---|---|
| `WhalePet.exe` | 构建产物 | `File /r` | `Delete "$INSTDIR\WhalePet.exe"` | |
| `whalepet-mcp.exe`（P7.2 桥接进程，控制台子系统） | 构建产物（`WHALEPET_PACKAGE=ON` → `dist/WhalePet`） | `File /r` | `Delete "$INSTDIR\whalepet-mcp.exe"`；卸载开头另加 `taskkill /IM whalepet-mcp.exe /F`（客户端不关 stdin 时桥接会存活并占用映像） | 与 `WhalePet.exe` 同目录；`*.dll` 通配删不到 exe，**必须逐条 Delete** |
| `Qt6*.dll` / `D3Dcompiler_47.dll` | windeployqt（对两个 exe 部署） | `File /r` | `Delete "$INSTDIR\*.dll"` | 通配删除，新增 DLL 无需改脚本 |
| `LICENSE`、`README.md` | package-release.ps1 复制 | `File /r` | `Delete "$INSTDIR\LICENSE"` / `...\README.md` | **本次修复补齐**（此前缺失 → 残留） |
| `generic/` `iconengines/` `imageformats/` `networkinformation/` `platforms/` `sqldrivers/` `styles/` `tls/` | windeployqt 插件 | `File /r` | 逐一 `RMDir /r` | 新增插件目录必须同时加到卸载清单 |
| `stomach/`（空目录，运行期写入） | 安装期 `CreateDirectory` | `CreateDirectory` + `icacls` 授权 | `RMDir /r`（选「是」）/ `RMDir`（兜底） | 见 §3；**本次修复补齐** |
| `engine/`（空目录；用户自备象棋引擎的落点） | 安装期 `CreateDirectory` | `CreateDirectory` + `icacls` 授权；`File /r` 以 `/x "engine"` 排除 | `RMDir /r`（选「是」）/ `RMDir`（兜底） | 见 §3.1；**本次新增**。打包时清空重建，绝不随包分发用户引擎 |
| `plugins/`（动态插件目录，P7.3 **已接线**：宿主启动时扫描） | 打包时由用户/第三方放入 | **不安装**（`File /r` 以 `/x "plugins" /x "plugins\*.*"` 排除，不随包分发） | `RMDir "$INSTDIR\plugins"`（**非递归**兜底：为空才删，含第三方 DLL 则保留） | 见 §8；P7.3 已完成接线与产测，卸载策略刻意保护用户自装插件 |
| `data/`（运行期由程序创建） | 程序首次运行 | 不安装（`/x "data"` 排除） | `RMDir /r`（选「是」）/ `RMDir`（兜底） | 存档，卸载时可选择保留 |
| `Uninstall.exe` | `WriteUninstaller` | — | `Delete "$INSTDIR\Uninstall.exe"` | 最后删除 |
| 开始菜单 `WhalePet\` 目录 + 2 个 `.lnk` | `CreateShortCut` | 所有用户上下文 | `RMDir /r "$SMPROGRAMS\WhalePet"` | |
| 桌面 `鲸鱼娘桌宠 WhalePet.lnk` | `CreateShortCut` | 所有用户上下文 | `Delete "$DESKTOP\...lnk"` | |
| 注册表 `HKLM\...\Uninstall\WhalePet`（含 `DisplayIcon` / `UninstallString` / `QuietUninstallString` / `InstallLocation` / `EstimatedSize` 等） | `WriteReg*` | 64 位视图 | `DeleteRegKey HKLM ...` | |

**64 位程序 = 64 位注册表视图 + 所有用户快捷方式上下文**：

- `SetRegView 64`：否则 64 位安装包的卸载项被重定向到 `HKLM\Software\WOW6432Node\...`，与
  `$PROGRAMFILES64` 不一致（"程序和功能" 仍能显示，但升级/查询视图混乱）。
- `SetShellVarContext all`：否则快捷方式只装到**安装者**的桌面/开始菜单，换用户卸载时删不到。
- **两条指令都只能在 Section / Function 内执行**（安装器放在 `.onInit`，安装 Section 再显式设一次）；
  卸载程序**不执行**安装器的 `.onInit`，所以卸载 Section 内必须重设，否则同样残留。

**完成页启动必须经 `explorer.exe` 转发，不得直接 `Exec`**：

安装器是提权的（`RequestExecutionLevel admin`），直接 `Exec "$INSTDIR\WhalePet.exe"` 会让程序
**继承高完整性级别（High IL）**；而资源管理器是中等完整性级别（Medium IL），Windows 的 UIPI
会拦截跨完整性级别的拖放消息，桌宠窗口收不到 `dragEnterEvent`，拖拽时显示**「禁止投放」**，
且与投放文件所在盘符 / 目录层级无关。免安装版由资源管理器启动故正常——这正是「安装版拖放不可用」
的根因，详见 `docs/pitfalls/` `TRAP-EXT0-001`。

```nsis
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION WhalePetLaunchAsUser
Function WhalePetLaunchAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APP_EXE}"'
FunctionEnd
```

> 注意：`asInvoker` 清单只表示「不主动请求提权」，**不能阻止继承提权父进程的令牌**；
> 凡需要与资源管理器等普通进程交互（拖放/剪贴板/全局钩子）的程序，都不能由提权进程直接拉起。

### 2.1 本地通道命名约定（P7.2 桥接，**唯一约定源**）

MCP stdio 桥接进程（`whalepet-mcp.exe`）与主程序之间经**命名管道**通信。管道名是两侧
**唯一需要对齐的常量**，约定源在 `src/contextapi/transport/LocalPipeTransport.h`：

```cpp
inline constexpr const char *kDefaultContextPipeName = "whalepet-context-v1";
```

| 项 | 约定 |
|---|---|
| 默认值 | `whalepet-context-v1`（Windows 下即 `\\.\pipe\whalepet-context-v1`） |
| 主程序侧 | `LocalPipeTransport`（`QLocalServer`）监听；随「本地 Context API」总开关一并启停 |
| 桥接侧 | `whalepet-mcp.exe` 连接之，可用 `--pipe <name>` 覆盖；`--token` 非空时注入 `initialize.params.token` |
| 修改流程 | 改常量 **必须同步** 本表、`LocalPipeTransport.h` 注释与 `whalepet-mcp` 用法文本；两侧不同值将导致桥接报「无法连接命名管道」 |
| 门控 | 管道仅在用户勾选「本地 Context API」时监听；未开启时桥接进程无法连接（退出码 2） |
| 分发/打包 | 管道是**运行期内核对象**，不落文件、**无需**安装/卸载清单处理；其**载体** `whalepet-mcp.exe` 见 §2 对应表与 §5 |

---

## 3. 运行期写权限：`stomach/`（拖拽投喂的落点）

`src/viewmodel/StomachService.cpp` 把拖拽投喂的文件移动到 **`<安装目录>/stomach`**，且**不做用户目录降级**
（需求固定安装目录为唯一落点）。而安装目录由**提权**安装程序创建，其 ACL 取决于目标卷/目录的继承权限——
`C:\Program Files` 下 `BUILTIN\Users` 只有「读取和执行」，任何非提权进程都无法写入。

因此安装期必须显式授权（`installer.nsi` 安装 Section）：

```nsis
CreateDirectory "$INSTDIR\stomach"
nsExec::ExecToLog '"$SYSDIR\icacls.exe" "$INSTDIR\stomach" /grant *S-1-5-32-545:(OI)(CI)M'
```

- 用系统自带 `icacls`（无需第三方 NSIS 插件；本机 NSIS 3.12 未安装 `AccessControl` 插件）。
- 组用 **SID** `S-1-5-32-545`（内置 Users），与系统显示语言无关（中文系统下名字匹配不可靠）。
- `(OI)(CI)M` = 对象/容器继承 + 修改权限，后续在 `stomach/` 内新建的文件自动继承。
- 授权失败**只告警不阻断安装**（`DetailPrint` + 退出码），保证安装流程不被个别 ACL 环境卡死。

对照：存档 `data/` 走的是 `DataPaths` 的**三级降级**（安装目录 → 用户目录 → 内存，见 `DATA-MODEL.md` §1），
写不进去也不会崩；`stomach/` 没有降级通道，所以**权限必须由安装程序保证**。

---

## 3.1 象棋引擎落点：`engine/`（用户自备）

小游戏「国际象棋」的对手是**外部 UCI 引擎**（如 Stockfish），程序**不自带棋力**
（见 `README.md`「国际象棋引擎」与 `MINIGAME-INTERFACE.md` §11）。引擎文件的落点是
**`<安装目录>/engine/`**，由**用户自行放入**：

| 项 | 约定 |
|---|---|
| 是否随包分发 | 目录随包**创建为空**（免安装版由 `package-release.ps1` 建；安装版由 NSIS 安装 Section 建），**引擎文件本身绝不分发** |
| 打包时清理 | `package-release.ps1` 若发现 `dist/WhalePet/engine/` 已有内容，先**整体删除再重建空目录**，避免打包机上残留的引擎被分发 |
| NSIS 打包排除 | `File /r` 追加 `/x "engine" /x "engine\*.*"`（既排内容也排目录本身） |
| 运行期写权限 | 安装版在安装 Section 用 `CreateDirectory` + `icacls /grant *S-1-5-32-545:(OI)(CI)M` 授权（同 `stomach/`），否则普通用户无法放入引擎 |
| 卸载行为 | 归入「是否删除用户数据」询问（与 `data/`、`stomach/` 一并）；选「是」→ `RMDir /r "$INSTDIR\engine"`；选「否」保留；无论哪种都补一条非递归 `RMDir` 兜底清空目录 |
| 缺失时行为 | 目录为空 / 无引擎 → 「国际象棋」提示引擎不可用并给出 README 指引（**不崩溃、不静默**）；菜单与窗口照常可用 |

> 免安装版用户可手动创建 `<解压目录>\engine\` 并放入引擎；也可在游戏窗口内用「浏览…」
> 指定任意路径的引擎，不受本目录约束（路径落库 `settings.json_ext` 的 `chess_engine_path`）。

---

## 4. 本次修复（2026-10 实测 Bug）

### 4.1 现象

| # | 现象 | 类别 |
|---|---|---|
| A | 安装到 `D:\...` 后，**文件投喂功能不可用**（拖拽文件后投喂动画照常，但文件不落盘到 `stomach/`） | 不可用 |
| B | 卸载后 `LICENSE`、`README.md`、`stomach/` **不会被删除**（残留，且整个安装目录删不掉） | 残留 |

### 4.2 根因与修复

**（B）卸载残留 —— 已验证（逐行核对原脚本即可复现）**

原卸载 Section 只删除了 `WhalePet.exe`、`*.dll`、`*.pdb` 与各 Qt 插件目录，**没有**删除
`LICENSE` / `README.md` / `stomach/`。而 `RMDir "$INSTDIR"` 只在目录为空时生效，于是：
`LICENSE`、`README.md` 与（运行期产生的）`stomach/` 必然留下 → 整个安装目录留在磁盘上。

修复：卸载 Section 逐条补齐 `Delete "$INSTDIR\LICENSE"`、`Delete "$INSTDIR\README.md"`、
`RMDir /r "$INSTDIR\stomach"`，并补了两条 `RMDir`（非递归）兜底。

**（A）投喂不可用 —— 高置信推断（修复方案不依赖推断是否成立）**

`StomachService::ingest()` 的第一步是 `ensureStomachDir()`，目录不可写即 **直接返回 0**（只 `qWarning`），
调用方 `PetWindow::dropEvent` 仍会照常播放投喂动画 —— 表现正是「有动画、没落盘」。
安装目录是否可写完全取决于其 ACL：

- **已验证（本机实测 `icacls`）**：`C:\Program Files` → `BUILTIN\Users:(RX)` + `(OI)(CI)(IO)(GR,GE)`，
  **无写权限**；`D:\` 根在该机恰好含 `NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)`（可继承修改权限）。
  → 结论：**是否可写随目标卷/目录而变，安装程序不能假定可写**。
- **未在本机复现安装过程**（需真实安装到目标机器）：用户报告 "装到 D 盘不可用" 最可能的原因，是目标目录
  继承的 ACL 未给普通用户写权限，导致 `stomach/` 创建/写入失败。**此项标注为推断，待用户机器复核**。

修复不再依赖推断：安装期**显式**创建 `stomach/` 并授予内置 Users 修改权限（见 §3），
无论目标卷默认 ACL 如何，普通用户下拖拽投喂都能落盘。

**（附带加固，均为已验证的脚本缺陷）**

| 项 | 原状 | 修复 |
|---|---|---|
| 注册表视图 | 未设 `SetRegView`，64 位程序的卸载项落在 `WOW6432Node` | `.onInit` + 两个 Section 均 `SetRegView 64` |
| 快捷方式上下文 | 未设 `SetShellVarContext`，仅安装者可见、换用户卸载删不到 | `.onInit` + 两个 Section 均 `SetShellVarContext all` |
| 静默卸载 | `MessageBox` 弹窗在 `/S` 下无法正常返回，可能误删存档 | `IfSilent` 直接走「保留」分支 |
| 卸载删不掉运行中的程序 | 程序在运行 → exe/dll 被占用，`RMDir "$INSTDIR"` 失败 | 卸载开头 `taskkill /IM WhalePet.exe /F`（未命中忽略） |
| 静默卸载入口 | 无 | 注册表补 `QuietUninstallString = "...\Uninstall.exe" /S` |
| 打包排除 | 仅 `/x "data\*.*"`；打包机上残留的空目录会被装到用户机器 | 补 `/x "data"`、`/x "stomach"`、`/x "stomach\*.*"` |
| 升级预填安装目录 | 依赖 `InstallDirRegKey`（读注册表时机不受控） | `.onInit` 显式先读 64 位视图、再回退 32 位视图 |

### 4.3 验证

- 脚本编译：`makensis 3.12` 通过（`Install 4 pages / Uninstall 2 pages`，无 warning/abort）。
- 安装/卸载行为验证方法见 §6（需在真机执行，属人工验收项）。

---

## 5. 功能更新时的同步维护清单（必读）

新增一个「随功能变化」的文件、目录、注册表项或快捷方式时，**逐项过一遍本清单**：

1. **是否随包分发？**
   - 静态资源（立绘、语料、地图、QSS）优先走 `qrc` 编译进产物（`cmake/Libraries.cmake` 已有 `assets.qrc`、`qt-ui.qrc`），
     **不进安装清单**，无需改打包脚本。
   - 只有「运行期按文件路径读取」的资源才需要落到 `dist/WhalePet/`；此时在 `package-release.ps1` 里复制，
     并在 `installer.nsi` 安装 Section 的 `File /r` 覆盖范围（`${APP_SRC}\*.*`）内确认。
2. **安装 Section 写入了什么？** → 在卸载 Section 加**逐条对应**的 `Delete` / `RMDir /r`（对照 §2 表）。
3. **是否运行期写入安装目录的新路径？** → 在上表「运行期可写」列登记，并在安装 Section 用
   `CreateDirectory` + `icacls /grant *S-1-5-32-545:(OI)(CI)M` 显式授权（参照 §3 的 `stomach`）。
   若该数据**允许降级**，优先像 `DataPaths` 那样加降级通道，而不是一律要求安装目录可写。
4. **是否新增注册表项？** → 安装 Section 写入 + 卸载 Section 对应 `DeleteRegKey`/`DeleteRegValue`；
   统一 64 位视图（不要漏 `SetRegView 64`）。
5. **是否新增快捷方式 / 开始菜单项？** → 卸载 Section 同步删除，且保持 `SetShellVarContext all`。
6. **是否新增需要排除的打包机残留？**（`data/`、`stomach/`、`engine/`、`plugins/`、`*.pdb`、日志等）→ 在 `File /r` 的 `/x`
   列表里补上（既排内容也排目录本身，避免空目录被装走），并在 `package-release.ps1` 里清理 `dist/WhalePet/` 下的同名残留。
7. **版本号是否更新？** → 只改发布命令的 `-Version` 与顶层 `CMakeLists.txt` 的 `project(... VERSION ...)`；
   `installer.nsi` 的 `VIProductVersion`（4 段数字）由发布脚本补零推导后以 `/DAPP_VERSION4` 传入，
   **不再需要手工同步**。
8. **更新本文档 §2 的对应表**（这是防止「装了删不掉」回归的唯一防线）。
9. **是否新增「安装后自动启动程序」的入口**（完成页勾选 / 首次运行）？→ 必须以**普通用户身份**
   启动（经 `explorer.exe` 转发），**不得由提权进程直接 `Exec`**，否则拖放等跨进程交互会被
   UIPI 拦截（`docs/pitfalls/ext0/P-069-nsis-uipi-dragdrop-elevation.md`，原 `TRAP-EXT0-001`）。

> 快速自查：跑一遍 §6 的「安装 → 卸载 → 目录是否为空」，只要安装目录残留非 `data/` 的内容，就说明清单漏项。

---

## 6. 验证方法（人工验收）

### 6.1 常规安装 / 卸载

```powershell
# 1) 打包（复用已构建的 build-package，产物落 dist/）
powershell -ExecutionPolicy Bypass -File scripts/package-release.ps1 `
    -AppName WhalePet -Version 0.3.0 -BuildDir build-package

# 2) 安装包校验：装到非系统盘（例如 D:\WhalePetDebug），走完向导
#    —— 重点：安装完成后，「普通用户」登录下拖拽一个文件到桌宠上，
#       检查 D:\WhalePetDebug\stomach\ 是否出现该文件（投喂可用性）。
#    3) 卸载后检查安装目录：除（主动保留的）data\ 外不应有任何残留。
```

### 6.2 静默安装 / 静默卸载

```powershell
# 静默安装到指定目录（/S 静默，/D= 必须是最后一个参数且不加引号）
Start-Process -Wait '.\dist\WhalePet-0.3.0-setup.exe' -ArgumentList '/S','/D=D:\WhalePetSilent'

# 静默卸载：默认「保留」存档与胃袋（IfSilent 分支），不会弹窗、不会误删
Start-Process -Wait 'D:\WhalePetSilent\Uninstall.exe' -ArgumentList '/S'
```

### 6.3 判据

| 检查 | 期望 |
|---|---|
| 拖拽投喂 | 文件出现在 `<安装目录>\stomach\`；安装目录 ACL 中 Users 含「修改」（`icacls "<安装目录>\stomach"`） |
| 象棋引擎 | `engine\` 目录随安装创建且可写（`icacls "<安装目录>\engine"`）；放入引擎后「国际象棋」可正常对弈 |
| 卸载（选「是」删数据） | 快捷方式、注册表项、安装目录**全部清除**，无 `LICENSE` / `README.md` / `whalepet-mcp.exe` / `stomach` / `engine` 残留（用户自放的 `plugins\` 除外，见 §8） |
| 卸载（选「否」保留数据） | 仅保留 `data\`、`stomach\`、`engine\`；程序文件、快捷方式、注册表项照常清除 |
| 静默卸载 | 不弹窗，保留 `data\`、`stomach\`、`engine\` |
| 桥接 exe 随包 | `<安装目录>\whalepet-mcp.exe` 存在且可直接运行（`whalepet-mcp --help` 打印用法） |

### 6.4 MCP 桥接链路（人工验收，P7.2）

```powershell
# 1) 启动主程序，右键菜单勾选「本地 Context API」（日志打印实际端口 / 管道名）
# 2) 用安装目录内的桥接进程联调（桥接自身只做字节转发，需配一个 MCP 客户端；
#    最简单是拿单测的驱动方式，或直接在终端手动喂一帧 Content-Length 报文）
"Content-Length: 58`r`n`r`n{`"jsonrpc`":`"2.0`",`"id`":1,`"method`":`"ping`"}" | & 'D:\WhalePetDebug\whalepet-mcp.exe'
```

| 检查 | 期望 |
|---|---|
| 总开关关闭时运行桥接 | 打印「无法连接命名管道」并**退出码 2**（管道未监听，属预期） |
| 总开关开启时运行桥接 | 返回一帧 `Content-Length` 分帧的 `{"jsonrpc":"2.0","id":1,"result":{"pong":true,...}}` |
| 主程序退出 | 桥接进程随之退出，不留孤儿进程 |

---

## 7. 已知限制与注意事项

- **`stomach/` 内容在「选『是』删除数据」时被永久删除**（不是移入回收站）：胃袋正常由程序每 5 分钟清空到
  回收站，但卸载清理走 NSIS `RMDir /r`。若希望卸载也「可恢复」，需改为程序侧或 `SHFileOperation` 实现。
- **`data/` 卸载时不会自动移入回收站**，与上同源。
- **运行中的程序**：卸载脚本会先 `taskkill /F`，未经保存的瞬时状态会丢失（养成状态为即时落盘，影响面很小）。
- **`icacls` 授权失败**（极少数受限环境）不阻断安装，但会 `DetailPrint` 告警；此环境下拖拽投喂可能仍不可用，
  属已知降级。
- 安装目录若被用户手工选择了**包含其它文件的目录**（如直接选 `D:\`），本安装包只按 §2 清单删除自己的产物，
  不会（也不应）`RMDir /r` 整个安装目录。
- **`plugins/` 若放了第三方 DLL，卸载后可能残留**：卸载只做**非递归** `RMDir`（为空才删），
  刻意保护用户自装插件；此时安装目录不会自动清空，需用户手动删除 `plugins/` 后目录才为空（见 §8）。

---

## 8. 动态插件目录约定（P7.3 **已接线**，不随包分发）

`docs/PLUGIN-ARCHITECTURE.md` §4.1 约定动态插件放在 **`<安装目录>/plugins/`**，
由宿主在启动时以 `QPluginLoader` 扫描。**该扫描已接线**（P7.3，组合根
`PetWindow::setupDllPlugins()` 构造 `DllPluginLoader` 并 `loadAll`）：

| 项 | 当前约定 |
|---|---|
| 目录是否随包安装 | **否**。目录不存在属正常情况，不影响启动（`DllPluginLoader` 对缺失目录不报错） |
| 用户自放插件 | **有效**：手动创建 `<安装目录>\plugins\` 并放入 DLL 即可（无需改脚本）。注意安装到 `C:\Program Files` 时创建/写入需管理员；也可改为安装到用户可写目录 |
| 打包排除 | `installer.nsi` 的 `File /r` 以 `/x "plugins" /x "plugins\*.*"` 排除；`package-release.ps1` 打包前清空 `dist/WhalePet/plugins/`（第三方插件绝不随包分发） |
| 卸载行为 | 卸载脚本**只做非递归** `RMDir "$INSTDIR\plugins"`：目录为空时顺带清除，**含用户 DLL 时保留**（避免误删第三方插件） |
| 何时需要改脚本 | 若将来改为「随包附带官方插件」，必须按 §5 同步清单补齐：安装 Section `File /r`、卸载 Section `RMDir /r`、§2 对应表 |
| 完整性级别要求 | 与拖放同理（`docs/pitfalls/` TRAP-EXT0-001）：插件 DLL 也是跨进程交互方，安装后**不要**以管理员身份启动主程序 |
| ABI 版本 | DLL 的 `Q_PLUGIN_METADATA` 必须含 `apiVersion`；高于宿主支持版本（当前 `kPluginApiVersion = 1`）时**跳过该插件**并记录原因（已实现并单测） |
| 官方示例 | 仓库内 `ext_hello` / `ext_badabi`（`src/plugin/examples/`）**仅供构建与自动化测试**（`test_dll_plugin`），**不随安装包分发** |

### 8.1 外部进程插件配置（P7.4，运行期，不随包分发）

外部进程插件（MCP Client，`docs/PLUGIN-ARCHITECTURE.md` §4.2）由
**`<数据目录>/plugins.json`**（JSON 数组）配置——属运行期用户数据，与 `plugins/` 同理
**不随包分发**，因此**无需改动** `package-release.ps1` / `installer.nsi` 的安装与卸载清单：

```json
[
  { "pluginId": "hello", "program": "C:/tools/hello-mcp.exe", "arguments": [], "timeoutMs": 3000 }
]
```

| 项 | 约定 |
|---|---|
| 配置键 | `pluginId`（能力前缀 `ext.<pluginId>.`）/ `program`（绝对路径或 PATH 名）/ `arguments`（数组）/ `timeoutMs`（默认 2000） |
| 文件不存在 | **不启动任何外部进程**（零开销）；日志记一条 `qInfo`，不告警、不影响启动 |
| 非法配置 | 空 `pluginId` / 空 `program` / `timeoutMs <= 0` / 重复 `pluginId` → 跳过该条并 `qWarning`（其余照常） |
| 崩溃隔离 | 子进程退出只把该来源能力标记为不可用（`-32002`），主进程与其它能力不受影响 |
| 卸载保留 | 该文件位于 `data/`（§2，卸载时可选择保留），随用户数据一并保留 |
