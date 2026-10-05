# NSIS 打包：卸载残留 + 64 位安装视图不一致 + 安装目录无写权限致拖拽投喂不可用

> **原编号**：`TRAP-P6-007`　**阶段**：P6　**来源**：原按阶段聚合的 `traps-P6.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：打包 / 部署 ｜ **影响**：`dist/WhalePet-Setup-<版本>.exe` 实测两处问题——
（A）安装到非系统盘（如 `D:\`）后**拖拽投喂功能不可用**（投喂动画照常，文件不落盘）；
（B）卸载后 `LICENSE`、`README.md`、`stomach/` **不删除**，且整个安装目录删不掉（`RMDir "$INSTDIR"` 因目录非空而失败）。
维护规范与同步清单见 `docs/packages.md`。

### 现象（用户实测）

- 用 NSIS 安装包安装到 `D:\...` 后，把文件拖到桌宠上：有投喂反馈，但 `stomach/` 里没有文件。
- 卸载完成后，安装目录里仍留有 `LICENSE`、`README.md`、`stomach/`（以及目录本身）。

### 根因

**（B）卸载残留 —— 已验证（逐行核对 `scripts/installer.nsi` 即可复现）**

原卸载 Section 只删了 `WhalePet.exe`、`*.dll`、`*.pdb` 与各 Qt 插件目录，**没有** `LICENSE`、`README.md`、
`stomach/` 的删除条目。而 `RMDir "$INSTDIR"` 仅在目录为空时生效，故这些残留必然留下，并连带整个目录删不掉。

**（A）投喂不可用 —— 高置信推断（修复不依赖推断是否成立）**

`StomachService::ingest()` 第一步 `ensureStomachDir()` 失败即**直接返回 0**（仅 `qWarning`），
而调用方 `PetWindow::dropEvent` 仍照常播放投喂动画 → 表现为「有动画、没落盘」。
`stomach/` 不做用户目录降级（需求固定安装目录为唯一落点），故安装目录对其**必须可写**；
而安装目录由提权安装程序创建，其 ACL 取决于目标卷/目录的继承权限：

- **已验证（本机 `icacls`）**：`C:\Program Files` → `BUILTIN\Users:(RX)`（无写权限）；
  `D:\` 根在该机恰含 `NT AUTHORITY\Authenticated Users:(OI)(CI)(IO)(M)`（可继承修改权限）。
  → **可写性随目标卷/目录而变，安装程序不能假定可写**。
- **未在本机复现安装过程**：用户机器上「装到 D 盘不可用」**最可能**是目标目录未继承写权限，
  导致 `stomach/` 创建/写入失败。**标注为推断，待用户机器复核**。

**（附带缺陷，均已验证）**：脚本未设 `SetRegView`（64 位程序卸载项落到 `WOW6432Node`）、
未设 `SetShellVarContext`（快捷方式只装给安装者，换用户卸载删不到）、
`MessageBox` 在 `/S` 静默卸载下无法正常返回（可能误删存档）、
运行中的程序占用 exe/dll 令卸载失败、`/x` 只排了 `data\*.*` 未排目录本身。

### 解决

`scripts/installer.nsi`（编译基线：NSIS **3.12**，`/INPUTCHARSET UTF8`）：

| 根因/缺陷 | 修复 |
|---|---|
| 卸载残留 | 卸载 Section 补 `Delete "$INSTDIR\LICENSE"`、`Delete "$INSTDIR\README.md"`、`RMDir /r "$INSTDIR\stomach"`，并补非递归 `RMDir` 兜底 |
| 投喂不可用 | 安装期 `CreateDirectory "$INSTDIR\stomach"` + `nsExec::ExecToLog '"$SYSDIR\icacls.exe" ... /grant *S-1-5-32-545:(OI)(CI)M'`（系统自带 icacls，SID 授权、与系统语言无关、失败只告警） |
| 64 位视图 | `.onInit` 与安装/卸载 Section 均 `SetRegView 64`（`SetRegView` **不能**写在 Section/Function 之外，否则 `Error: command SetRegView not valid outside Section or Function`） |
| 快捷方式上下文 | `.onInit` 与安装/卸载 Section 均 `SetShellVarContext all`（同上，卸载器不执行安装器 `.onInit`，必须重设） |
| 静默卸载 | `IfSilent KeepUserData` 直接走「保留」分支，不弹窗 |
| 程序占用 | 卸载开头 `nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM "WhalePet.exe" /F'` |
| 静默卸载入口 | 注册表补 `QuietUninstallString` |
| 打包排除 | `File /r` 补 `/x "data" /x "stomach" /x "stomach\*.*"` |
| 升级预填目录 | `InstallDirRegKey` 改为 `.onInit` 显式读（先 64 位视图，空则回退 32 位视图，兼容历史 WOW6432Node 登记） |

### 验证

- **已验证**：`makensis 3.12` 编译通过（`Install 4 pages, 1 section` / `Uninstall 2 pages, 1 section`，无 warning/abort）；
  期间修正了一处脚本错误——`SetRegView`/`SetShellVarContext` 首次被写在 Section 之外，被编译器直接拒绝。
- **待人工验收**（真机）：安装到非系统盘 → 普通用户拖拽投喂文件出现在 `<安装目录>\stomach\`；
  卸载（选「是」）后安装目录清空、无 `LICENSE`/`README.md`/`stomach` 残留。步骤见 `docs/packages.md` §6。

### 影响与关联文档

- 关联：`scripts/installer.nsi`、`scripts/make-package.ps1`、`docs/packages.md`（新增，打包唯一维护指南）、
  `docs/BUILD.md` §10、`src/viewmodel/StomachService.cpp`、`docs/DATA-MODEL.md` §1。
- 教训 1（卸载）：**安装清单与卸载清单必须逐条对应**，并维护成一张表（`docs/packages.md` §2）；
  `RMDir "$INSTDIR"` 只是「空则删」的兜底，不是清理手段。
- 教训 2（64 位）：**64 位、按机器安装的程序，注册表要显式 64 位视图、快捷方式要显式「所有用户」上下文**；
  这两条指令只能写在 Section/Function 内（卸载器与安装器互不继承）。
- 教训 3（权限）：**装在 Program Files 下的程序，其安装目录对普通用户默认只读**——
  凡是「运行期要写进安装目录」的数据路径，要么有降级通道（如 `data/` 的 `DataPaths` 三级降级），
  要么由安装程序显式授权（如 `stomach/` 的 `icacls`），**不能依赖目标卷的默认 ACL**。
- 教训 4（静默）：`MessageBox` 类交互在 `/S` 静默模式下不可依赖，必须用 `IfSilent` 给非交互默认值。
