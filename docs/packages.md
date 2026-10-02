# WhalePet · 打包与分发（免安装版 + NSIS 安装包）

> 适用脚本：`packaging/make-package.ps1`（一键打包）、`packaging/whalepet.nsi`（NSIS 安装程序）。
> 相关文档：`BUILD.md` §10（构建流程）、`DATA-MODEL.md` §1（存档位置与降级）、`README.md` §六（崩溃/调试约定）、`traps-P6.md`。
>
> **本文档是打包脚本的唯一维护指南**：凡改动安装内容、卸载内容、注册表、快捷方式或运行期可写路径，
> 都必须按 §5 的同步清单更新脚本与本文档，二者不得脱节。

---

## 1. 打包链与产物

```powershell
powershell -ExecutionPolicy Bypass -File packaging/make-package.ps1
```

| 步骤 | 动作 | 关键点 |
|---|---|---|
| 1 | `cmake -B build-package -DWHALEPET_PACKAGE=ON` | 独立构建目录；Release 产物直接落在 `dist/WhalePet`，**不生成调试符号** |
| 2 | `cmake --build build-package --config Release` | |
| 3 | `windeployqt --release --no-translations --compiler-runtime --dir dist/WhalePet` | 补齐 Qt 运行库与插件 |
| 4 | 清理 `*.pdb / *.ilk / *.exp / *.lib`，复制 `README.md`、`LICENSE` | 运行期数据（`data/`、`stomach/`）**不打包** |
| 5 | `makensis /INPUTCHARSET UTF8 packaging/whalepet.nsi` | 产出 `dist/WhalePet-Setup-<版本>.exe` |

| 产物 | 说明 |
|---|---|
| `dist/WhalePet/` | 免安装版（整个目录 zip 后即分发；**不含** `data/`、`stomach/`） |
| `dist/WhalePet-Setup-<版本>.exe` | NSIS 安装包（程序 + Qt 运行库 + 快捷方式 + 卸载程序） |

- 安装器的版本号有两个来源，**必须同步**：`make-package.ps1 -Version`（文件名与注册表 `DisplayVersion`）
  与 `whalepet.nsi` 里的 `VIProductVersion`（PE 版本资源，必须是 **4 段数字**）。
- NSIS 脚本为 UTF-8，必须 `/INPUTCHARSET UTF8`（中文界面）；`make-package.ps1` 刻意保持纯 ASCII
  （PowerShell 5.1 会把无 BOM 的 UTF-8 脚本按 ANSI 解析，见 `traps-P6.md` `TRAP-P6-004`）。

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
| `Qt6*.dll` / `D3Dcompiler_47.dll` | windeployqt | `File /r` | `Delete "$INSTDIR\*.dll"` | 通配删除，新增 DLL 无需改脚本 |
| `LICENSE`、`README.md` | make-package.ps1 复制 | `File /r` | `Delete "$INSTDIR\LICENSE"` / `...\README.md` | **本次修复补齐**（此前缺失 → 残留） |
| `generic/` `iconengines/` `imageformats/` `networkinformation/` `platforms/` `sqldrivers/` `styles/` `tls/` | windeployqt 插件 | `File /r` | 逐一 `RMDir /r` | 新增插件目录必须同时加到卸载清单 |
| `stomach/`（空目录，运行期写入） | 安装期 `CreateDirectory` | `CreateDirectory` + `icacls` 授权 | `RMDir /r`（选「是」）/ `RMDir`（兜底） | 见 §3；**本次修复补齐** |
| `plugins/`（动态插件目录，P7 预留） | 打包时由用户/第三方放入 | 首版**不安装**（目录不存在 = 无第三方插件，程序只记 info） | 若将来随包安装需补 `RMDir /r` | 见 §8：**约定先落文档**，P7.3 有 DLL 产物时再同步脚本 |
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
的根因，详见 `traps-extend0.md` `TRAP-EXT0-001`。

```nsis
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION WhalePetLaunchAsUser
Function WhalePetLaunchAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APP_EXE}"'
FunctionEnd
```

> 注意：`asInvoker` 清单只表示「不主动请求提权」，**不能阻止继承提权父进程的令牌**；
> 凡需要与资源管理器等普通进程交互（拖放/剪贴板/全局钩子）的程序，都不能由提权进程直接拉起。

---

## 3. 运行期写权限：`stomach/`（拖拽投喂的落点）

`src/viewmodel/StomachService.cpp` 把拖拽投喂的文件移动到 **`<安装目录>/stomach`**，且**不做用户目录降级**
（需求固定安装目录为唯一落点）。而安装目录由**提权**安装程序创建，其 ACL 取决于目标卷/目录的继承权限——
`C:\Program Files` 下 `BUILTIN\Users` 只有「读取和执行」，任何非提权进程都无法写入。

因此安装期必须显式授权（`whalepet.nsi` 安装 Section）：

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
   - 静态资源（立绘、语料、地图、QSS）优先走 `qrc` 编译进产物（`CMakeLists.txt` 已有 `assets.qrc`、`qt-ui.qrc`），
     **不进安装清单**，无需改打包脚本。
   - 只有「运行期按文件路径读取」的资源才需要落到 `dist/WhalePet/`；此时在 `make-package.ps1` 里复制，
     并在 `whalepet.nsi` 安装 Section 的 `File /r` 覆盖范围（`${APP_SRC}\*.*`）内确认。
2. **安装 Section 写入了什么？** → 在卸载 Section 加**逐条对应**的 `Delete` / `RMDir /r`（对照 §2 表）。
3. **是否运行期写入安装目录的新路径？** → 在上表「运行期可写」列登记，并在安装 Section 用
   `CreateDirectory` + `icacls /grant *S-1-5-32-545:(OI)(CI)M` 显式授权（参照 §3 的 `stomach`）。
   若该数据**允许降级**，优先像 `DataPaths` 那样加降级通道，而不是一律要求安装目录可写。
4. **是否新增注册表项？** → 安装 Section 写入 + 卸载 Section 对应 `DeleteRegKey`/`DeleteRegValue`；
   统一 64 位视图（不要漏 `SetRegView 64`）。
5. **是否新增快捷方式 / 开始菜单项？** → 卸载 Section 同步删除，且保持 `SetShellVarContext all`。
6. **是否新增需要排除的打包机残留？**（`data/`、`stomach/`、`*.pdb`、日志等）→ 在 `File /r` 的 `/x`
   列表里补上（既排内容也排目录本身，避免空目录被装走）。
7. **版本号是否更新？** → 同步改 `make-package.ps1 -Version` 与 `whalepet.nsi` 的 `VIProductVersion`
   （4 段数字）以及 `CMakeLists.txt` 的 `project(... VERSION ...)`。
8. **更新本文档 §2 的对应表**（这是防止「装了删不掉」回归的唯一防线）。
9. **是否新增「安装后自动启动程序」的入口**（完成页勾选 / 首次运行）？→ 必须以**普通用户身份**
   启动（经 `explorer.exe` 转发），**不得由提权进程直接 `Exec`**，否则拖放等跨进程交互会被
   UIPI 拦截（`traps-extend0.md` `TRAP-EXT0-001`）。

> 快速自查：跑一遍 §6 的「安装 → 卸载 → 目录是否为空」，只要安装目录残留非 `data/` 的内容，就说明清单漏项。

---

## 6. 验证方法（人工验收）

### 6.1 常规安装 / 卸载

```powershell
# 1) 打包（默认 0.2.0）
powershell -ExecutionPolicy Bypass -File packaging/make-package.ps1

# 2) 安装包校验：装到非系统盘（例如 D:\WhalePetDebug），走完向导
#    —— 重点：安装完成后，「普通用户」登录下拖拽一个文件到桌宠上，
#       检查 D:\WhalePetDebug\stomach\ 是否出现该文件（投喂可用性）。
#    3) 卸载后检查安装目录：除（主动保留的）data\ 外不应有任何残留。
```

### 6.2 静默安装 / 静默卸载

```powershell
# 静默安装到指定目录（/S 静默，/D= 必须是最后一个参数且不加引号）
Start-Process -Wait '.\dist\WhalePet-Setup-0.2.0.exe' -ArgumentList '/S','/D=D:\WhalePetSilent'

# 静默卸载：默认「保留」存档与胃袋（IfSilent 分支），不会弹窗、不会误删
Start-Process -Wait 'D:\WhalePetSilent\Uninstall.exe' -ArgumentList '/S'
```

### 6.3 判据

| 检查 | 期望 |
|---|---|
| 拖拽投喂 | 文件出现在 `<安装目录>\stomach\`；安装目录 ACL 中 Users 含「修改」（`icacls "<安装目录>\stomach"`） |
| 卸载（选「是」删数据） | 快捷方式、注册表项、安装目录**全部清除**，无 `LICENSE` / `README.md` / `stomach` 残留 |
| 卸载（选「否」保留数据） | 仅保留 `data\`、`stomach\`；程序文件、快捷方式、注册表项照常清除 |
| 静默卸载 | 不弹窗，保留 `data\`、`stomach\` |

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

---

## 8. 动态插件目录约定（P7 预留，未随包分发）

`docs/PLUGIN-ARCHITECTURE.md` §4.1 约定动态插件放在 **`<安装目录>/plugins/`**，
由宿主在启动时扫描（`QPluginLoader`）。当前阶段（P7.0）**没有 DLL 产物**，因此：

| 项 | 当前约定 |
|---|---|
| 目录是否随包安装 | **否**。目录不存在属正常情况，宿主只记一条 `qInfo`，不告警、不影响启动 |
| 用户自放插件 | 手动创建 `<安装目录>\plugins\` 并放入 DLL 即可（无需改脚本） |
| 卸载行为 | 卸载脚本**不删除** `plugins/`，避免误删用户自装的第三方插件 |
| 何时需要改脚本 | 若将来改为「随包附带官方插件」，必须按 §5 同步清单补齐：安装 Section `File /r`、卸载 Section `RMDir /r`、§2 对应表 |
| 完整性级别要求 | 与拖放同理（`traps-extend0.md` TRAP-EXT0-001）：插件 DLL 也是跨进程交互方，安装后**不要**以管理员身份启动主程序 |
| ABI 版本 | DLL 的 `Q_PLUGIN_METADATA` 必须含 `apiVersion`；高于宿主支持版本（当前 `kPluginApiVersion = 1`）时**跳过该插件**并记录原因 |
