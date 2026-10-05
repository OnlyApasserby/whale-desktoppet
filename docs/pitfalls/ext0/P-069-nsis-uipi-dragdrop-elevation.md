# 安装版拖拽显示「禁止投放」，根源是安装器以提权身份拉起程序（UIPI 跨完整性级别拦截）

> **原编号**：`TRAP-EXT0-001`　**阶段**：EXT0　**来源**：原按阶段聚合的 `traps-extend0.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：打包 / 权限（进程完整性级别） ｜ **影响**：NSIS 安装版中，把文件拖到立绘上时光标
**直接显示「禁止投放」**（`dragEnterEvent` 根本不被触发），拖拽投喂完全不可用；
而**免安装版（绿色版）在完全相同的位置、完全相同的投放文件路径下一切正常**。

### 现象（用户实测）

- 安装版卸载无残留（`TRAP-P6-007` 已修复），但拖拽投喂不可用。
- 与**投放文件所在位置无关**：无论文件放在**任意盘符的根目录**，还是其**下一级子目录**，
  都是「禁止投放」。
- 免安装版（同一份产物解压运行）在**相同的文件位置**下均能正常投放并触发预期行为。

### 根因

**拖动源（资源管理器）与放置目标（桌宠）处于不同的「完整性级别」，被 Windows UIPI 拦截。**

链条如下（每步均已核对/取证）：

1. **程序自身不会自提权（已验证）**：从 `dist/WhalePet/WhalePet.exe` 中提取内嵌清单，
   内容为 `<requestedExecutionLevel level='asInvoker' uiAccess='false' />`——
   双击启动时**请求的是调用者（普通用户）令牌**，不会弹 UAC。
2. **安装器是提权的**：`scripts/installer.nsi` 声明 `RequestExecutionLevel admin`。
3. **完成页「启动 WhalePet」直接 `Exec` 了程序**（修复前）：
   ```
   !define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
   ```
   NSIS 在此路径下是 `Exec`（`CreateProcess`）——而 `asInvoker` 的语义是
   **「用调用者的令牌运行」**，所以子进程**继承安装器的高完整性级别（High IL）**。
   （`asInvoker` 只决定「不主动请求提权」，并不阻止继承提权父进程的令牌。）
4. **资源管理器是 Medium IL**：免安装版由资源管理器双击启动 → 桌宠是 Medium IL，
   与拖动源同级 → 拖放正常。
5. **UIPI 拦截**：Windows 自 Vista 起，低完整性进程**不能**向高完整性进程窗口投递绝大多数
   窗口消息（`WM_DROPFILES` / `WM_COPYDATA` / `WM_COPYGLOBALDATA` 等）。
   Qt 的拖放目标（OLE `IDropTarget` / `RegisterDragDrop`）因此收不到 `DragEnter`，
   `dragEnterEvent` / `dragMoveEvent` / `dropEvent` 全部不触发，Windows 依据目标的返回
   画出**「禁止投放」**光标。

**这解释了两个「看起来奇怪」的现象**：

- **为什么只在安装版出现、免安装版正常**：两者是同一份二进制，差别**只在启动方式**——
  安装版那次是**被提权的安装器拉起的**，免安装版是**资源管理器拉起的**。
- **为什么与投放文件所在盘符 / 目录层级无关**：该拦截发生在「进程↔进程」的完整性级别边界上，
  与文件路径完全无关；因此「任意盘符根目录 / 一级子目录都失败」正好是它的典型表现，
  而不是路径解析问题（对照：代码侧的投放判定只依赖 `QUrl::isLocalFile()`）。

### 已排除项（已验证）

- **不是路径解析 / 工作目录问题**：`PetWindow::dragEnterEvent` 只依据 `mimeData()->hasUrls()`
  与 `QUrl::isLocalFile()` 判定，不涉及工作目录或路径归一化。新增回归用例
  `test_smoke::dropAcceptsLocalFilesFromAnyDriveRootOrFirstLevelDir` 断言
  「任意盘符根目录 / 一级子目录 / 更深路径的本地文件都被接受，非本地 URL 被忽略」——
  修复前后该用例均通过，说明**判定逻辑本身与来源路径无关**。
- **不是 `setAcceptDrops` / 事件接受与否的写法问题**：`setAcceptDrops(true)` 已在
  `setupWindowFlags()` 中设置，`dragEnterEvent` / `dragMoveEvent` 都已 `acceptProposedAction()`，
  `dropEvent` 亦已实现；同一套代码在免安装版下可用。
- **不是安装目录写权限问题**：写权限只影响 `dropEvent` 之后的入胃落盘（`TRAP-P6-007`），
  不会改变拖拽光标；且本次现象发生在 `dragEnterEvent` 之前。
- **不是文件系统虚拟化（UAC VirtualStore）**：该虚拟化只作用于 32 位进程写
  `Program Files` / `Windows` 的场景，本程序是 **x64**（`-A x64`），不适用。

### 待复核项（未在开发机复现安装过程）

- 用户机器上「完成页启动 → 桌宠为 High IL」这一步**未在开发机实测**（本机无该安装环境，
  且拖放动作需人工执行）。判定方法（人工，30 秒）：
  1. 按 §验证 安装并从完成页启动；
  2. **任务管理器 → 详细信息 → 右键表头 → 勾选「完整性级别」**（或用 Process Explorer 的
     `View → Select Columns → Process Image → Integrity Level`）查看 `WhalePet.exe`；
  3. 「高」= 命中本根因；「中」= 需另查（见下「排查顺序」）。
- **另一种会持续复现的入口**（同样指向 UIPI，供用户确认）：若快捷方式被勾选
  「以管理员身份运行」，或系统在
  `HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers`
  里给该 exe 写了 `RUNASADMIN`（程序兼容性助手可能写入），则**每次启动都是 High IL**，
  拖放会持续不可用。检查/清理方式见「验证」§3。

### 解决

分两层：**根治**（不再以提权身份运行）+ **兜底**（即使用户坚持以管理员运行，也尽力放行消息）。

| 层次 | 修复 | 位置 |
|---|---|---|
| 根治 | 完成页改为**经 `explorer.exe` 转发启动**，由资源管理器以 **Medium IL** 拉起，与免安装版完全一致：`Function WhalePetLaunchAsUser` + `Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APP_EXE}"'`，并定义 `MUI_FINISHPAGE_RUN_FUNCTION WhalePetLaunchAsUser`（`MUI_FINISHPAGE_RUN` 留空以显示勾选项） | `scripts/installer.nsi` |
| 兜底 | 窗口创建后对**本窗口**放行三条跨完整性级别消息：`ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES / WM_COPYDATA / WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr)`（`WM_COPYGLOBALDATA = 0x0049` 为未公开常量，需自行定义）；非提权运行时无副作用 | `src/view/PetWindow.cpp`（`allowDragDropFromLowerIntegrity()`） |
| 可观测 | 提权运行时 `qWarning` 明确告警「Windows 会拦截资源管理器的拖放，拖拽投喂可能不可用，请改用普通用户身份启动」（`isRunningElevated()` 用令牌完整性级别判定，比 `IsUserAnAdmin` 在 UAC 下更准确） | `src/view/PetWindow.cpp`（`showPet()`） |

**兜底手段的已知不确定性（如实记录）**：公开资料对「仅放行三条消息是否足以恢复 Qt 的
**OLE** 拖放」结论不一致——有资料认为还需 `RevokeDragDrop()` + `DragAcceptFiles()` 退回到
传统 `WM_DROPFILES` 路径才真正生效，而那样会**替换掉 Qt 的 OLE 拖放通道**（丢失虚拟文件拖放等
能力），属于超出「只修复可用性」的改动，**本次不做**。因此：

> **不依赖兜底：根治手段是「不以管理员身份运行」。** 兜底只是让用户误用管理员启动时
> 尽量可用，不改变「安装版 = 免安装版行为」的结论（两者都由资源管理器以 Medium IL 启动）。

### 验证

1. **回归守卫（已自动化，已验证）**：
   `tests/test_smoke.cpp::dropAcceptsLocalFilesFromAnyDriveRootOrFirstLevelDir`
   —— 断言本地文件（`D:/x.txt`、`D:/一级子目录/y.txt`、`E:/z.md`、`F:/sub/deep/f.bin`）
   的 `dragEnter` / `dragMove` 均被接受（不会出现禁止标识），且 `https://` URL 仍被拒绝。
   `ctest -C Debug -R test_smoke` → **Passed**（同时说明「拒绝」路径也生效，断言非永真）。
2. **脚本编译（已验证）**：`makensis 3.12 /INPUTCHARSET UTF8 scripts/installer.nsi` → 退出码 0。
3. **真机人工验收（待用户执行）**：
   - 安装完成后从**完成页**启动，检查完整性级别应为 **中**（不是「高」）；
   - 拖拽任意盘符根目录 / 一级子目录下的文件到立绘上 → 光标为可投放，文件进入
     `<安装目录>\stomach\`；
   - 若完整性级别仍为「高」：检查快捷方式 `属性 → 兼容性 → 以管理员身份运行` 是否被勾选，
     以及 `HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers` 中是否
     存在指向该 exe 的 `RUNASADMIN` 值；二者清除后重启程序应恢复正常。

**排查顺序（若真机上仍不可用）**：先看完整性级别（本记录）→ 再看 `stomach` 写权限
（`TRAP-P6-007`）→ 再看窗口是否真的注册为拖放目标（`setAcceptDrops` / OLE 初始化）。

### 影响与关联文档

- 关联：`scripts/installer.nsi`（完成页启动）、`packages.md`（打包维护规范，§2 已补该约束）、
  `src/view/PetWindow.cpp`（`showPet()` / `setupWindowFlags()`）、`src/viewmodel/StomachService.cpp`、
  `tests/test_smoke.cpp`、`docs/README.md` §六。
- 教训 1（打包）：**提权进程 `Exec` 出来的子进程会继承高完整性级别**；`asInvoker` 清单
  只说明「不主动提权」，并不能阻止继承。凡「需要与资源管理器/其它普通进程交互（拖放、剪贴板、
  全局钩子、UI 自动化）」的程序，**安装器不得以提权身份直接拉起它**，应经 `explorer.exe`
  或降权令牌启动。
- 教训 2（分工）：**`RequestExecutionLevel admin` 只应约束安装器**，不应通过「继承」泄漏到
  应用进程；安装器要装到受保护位置（`Program Files`）才需要提权，这与应用**运行时**的权限需求
  （这里是「普通用户 + 安装目录可写」，见 `TRAP-P6-007`）是两件事，必须分开处理。
- 教训 3（诊断）：遇到「某功能只在安装版失效、免安装版正常」时，**先比对两者的启动方式与
  进程完整性级别，而不是先怀疑代码**——同一份二进制的行为差异几乎只可能来自运行环境
  （完整性级别 / 权限 / 工作目录 / 环境变量）。

---
