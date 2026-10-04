# traps · extend0 — 扩展功能踩坑记录（打包分发后的拖拽投喂可用性）

> 对应产品功能的**扩展/后续**问题（不属于 P0–P6 任一阶段的原始交付范围），
> 按 `README.md` §二.5 约定，**仅记录真实复现**的问题。
> 记录格式：现象（含可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。
> 本文件中的「已验证 / 待复核」标注即按该约定区分事实与推断。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2** + NSIS **3.12**，
详见 `BUILD.md`；打包方案见 `packages.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-EXT0-001` | 安装版拖拽投喂显示「禁止投放」、免安装版正常：安装器完成页以**提权（High IL）**身份拉起程序，资源管理器（Medium IL）的拖放被 Windows **UIPI** 拦截 | 打包/权限（完整性级别） | 已解决（根治 + 兜底） |
| `TRAP-EXT0-002` | 屏幕分辨率改变 / 监视器变更后鲸鱼娘立绘「看不见」：启动沿用历史坐标，未按当前屏幕布局重算 | 窗口 / 多显示器 | 已解决 |
| `TRAP-EXT0-003` | 新增贴边立绘 `home-bottom` 只更新了 `kPoses`、漏了 `assets.qrc` → 运行期静默缺图；同处暴露 `kPoseCount`(92) 与清单条数(93) 不一致，尾项 `workbench-peek` 被排除在预载队列外 | 数据/资产（清单三处对应） | 已解决 |

---

## TRAP-EXT0-001 — 安装版拖拽显示「禁止投放」，根源是安装器以提权身份拉起程序（UIPI 跨完整性级别拦截）

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
2. **安装器是提权的**：`packaging/whalepet.nsi` 声明 `RequestExecutionLevel admin`。
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
| 根治 | 完成页改为**经 `explorer.exe` 转发启动**，由资源管理器以 **Medium IL** 拉起，与免安装版完全一致：`Function WhalePetLaunchAsUser` + `Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APP_EXE}"'`，并定义 `MUI_FINISHPAGE_RUN_FUNCTION WhalePetLaunchAsUser`（`MUI_FINISHPAGE_RUN` 留空以显示勾选项） | `packaging/whalepet.nsi` |
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
2. **脚本编译（已验证）**：`makensis 3.12 /INPUTCHARSET UTF8 packaging/whalepet.nsi` → 退出码 0。
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

- 关联：`packaging/whalepet.nsi`（完成页启动）、`packages.md`（打包维护规范，§2 已补该约束）、
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

## TRAP-EXT0-002 — 分辨率调整 / 显示器增删后立绘「看不见」：启动沿用历史坐标，未按当前屏幕重算

**类别**：窗口 / 多显示器 ｜ **影响**：调整屏幕分辨率或增删显示器后，鲸鱼娘立绘不在可视区域内
（用户描述为「立绘无法显示」）。

### 现象（用户报告）

- 屏幕分辨率改变或监视器变更后，鲸鱼娘的立绘无法显示。

### 根因（代码走查）

修复前 `PetWindow::showPet()` 启动时先 `restorePosition()`，把窗口移到 SQLite 中保存的**历史坐标**；
`defaultPosition()` 亦按「主屏右下角」计算兜底位置——两者都以**旧屏幕布局**为前提。分辨率缩小、
显示器移除或布局变化后，历史坐标会落到可视区域之外，立绘因此看不见。

> 说明：本次未在真机上逐一复现所有触发路径；上述为代码走查结论。所采用的解决方案不依赖该推断
> 是否完备（「启动即按当前屏幕居中」+「运行期夹回可见区域」对任意屏幕布局都成立）。

### 解决

| 层次 | 修复 | 位置 |
|---|---|---|
| 启动定位 | 每次启动都把窗口放到当前主屏 `availableGeometry()` 的几何中心；删除 `restorePosition()` / `savePosition()` / `importLegacyPositionIfNeeded()`（位置不再跨会话持久化） | `src/view/PetWindow.cpp`（`defaultPosition()` / `showPet()`） |
| 尺寸对齐 | `applySettings()` 应用 `pose_size` 后，按**最终**窗口尺寸再居中一次 | `src/view/PetWindow.cpp`（`showPet()`） |
| 运行期 | 监听各屏 `geometryChanged` / `availableGeometryChanged` 与 `QGuiApplication::screenAdded` / `screenRemoved`，变化时 `clampToVisibleArea()` 把窗口夹回可见区域 | `src/view/PetWindow.cpp`（`watchScreenChanges()`） |

`settings` 表的 `pos_x/pos_y` 列与 `SettingsRepo::clearPosition()` 保留（数据模型向后兼容），
但程序不再写入 / 读取窗口坐标。

### 验证

- 回归守卫：`tests/test_smoke.cpp::petWindowStartsCenteredOnPrimaryScreen`
  —— `showPet()` 后窗口中心必须等于当前主屏 `availableGeometry()` 中心（1px 取整容差）。
- 与需求一致：不恢复任何历史坐标，分辨率 / 显示器变化后每次启动都会重新居中。

### 影响与关联文档

- 关联：`docs/PRESENTATION.md` §3（窗口行为）、`docs/SETTINGS.md` §4/§6、`tests/test_smoke.cpp`。
- 教训：**凡把窗口位置跨会话持久化的桌宠 / 悬浮窗，回放历史坐标前必须结合「当前屏幕布局」校验，
  或干脆每次启动重算**；否则分辨率 / 多显示器变化后窗口会落到屏幕外，表现为「程序在跑却看不见」。

---

## TRAP-EXT0-003 — 新增贴边立绘只更新了清单的一半：`home-bottom` 未入 `assets.qrc`，且 `kPoseCount` 与清单条数不一致

**类别**：数据 / 资产（清单「三处对应」） ｜ **影响**：新增的贴边立绘
`dsh-whale-home-bottom.webp` 运行期**永远加载不到**（静默缺图）；依赖它的「上边框贴边」表现
（`DesktopEdge::Top` → `home-bottom`）退化为「保持常规显示」并只记一条 `qWarning`。
同一处还暴露：预载队列尾项被漏掉。

### 现象（可复现，已验证）

- `assets/poses/dsh-whale-home-bottom.webp` 存在（61 KB），`src/core/PoseNames.h` 的 `kPoses`
  也已登记 `{"home-bottom", "dsh-whale-home-bottom"}`；
- 但 `assets/assets.qrc` 里**没有**这一条（只有 `home-peek` / `settings-peek` / `workbench-peek`），
  即运行期不存在 `:/poses/dsh-whale-home-bottom.webp`；
- 复现：`rg "home-bottom" assets/assets.qrc` → **无匹配**；`assets.qrc` 的 `poses/` 条目数为 92，
  而 `assets/poses/*.webp` 为 93 个文件、`kPoses` 为 93 条。

### 根因

立绘资产的清单在工程里分散在 **3 处**，本次新增只更新了其中 2 处：

| # | 位置 | 作用 | 漏掉的后果 |
|---|---|---|---|
| 1 | `assets/poses/*.webp` | 实际文件 | — |
| 2 | `src/core/PoseNames.h`（`kPoses` / `kPoseCount`） | pose → 资源文件名映射、预载队列长度 | `poseFile()` 查不到 → 无法拼出资源路径 |
| 3 | `assets/assets.qrc` | 把文件编译进 `:/poses/` | 路径存在但**资源不存在** → 运行期缺图（编译期不报错） |

漏掉第 3 处时**编译通过、链接通过、单测也可能通过**，只在请求该 pose 时表现为缺图，
属于典型的「能跑但错」。同一处还暴露计数不一致：`kPoses` 实为 **93** 条而 `kPoseCount` 仍是 **92**，
`PoseLibrary` 按 `kPoseCount` 建预载队列，导致**尾项 `workbench-peek` 被排除在预载之外**
（首次显示时回落到「按需即时加载」，功能可用但不符合设计意图）。

### 解决

| 层 | 修复 |
|---|---|
| 资源 | `assets/assets.qrc` 补登 `poses/dsh-whale-home-bottom.webp`（立绘 92 → 93） |
| 清单 | `src/core/PoseNames.h`：`kPoseCount` 92 → **93**（与该表条数、与目录文件数一致） |
| 单测 | `test_state_machine::catalogCoversAllPoses` 期望值同步 92 → 93 —— 该断言是**清单完整性守卫**，同步后它**真正覆盖全部 93 项**（不是放宽条件） |
| 文字 | 「92 张立绘」→「93 张」同步于 `PoseNames.h` / `PetVisuals.h` / `PoseView.h` / `PoseLibrary.h` / `WorkState.{h,cpp}` / `PRESENTATION.md` / `ARCHITECTURE.md` / `README.md` |

### 验证

- `test_smoke::poseViewAttachesPeekPoseToDesktopEdge`：贴边立绘可加载，且左 / 右命中区互斥（贴合方向正确）；
- `test_smoke::petWindowReportsDesktopEdgeWhenMovedToBorder`：窗口移到边框 → 方向判定与表现联动；
- `test_state_machine::catalogCoversAllPoses`：`kPoseCount == 93`，逐项 `poseExists()` / `poseFile()` 通过；
- Debug / Release CTest 各 **17/17**（新增用例计入 `test_smoke`，测试目标数不变）。

### 影响与关联文档

- 关联：`assets/assets.qrc`、`src/core/PoseNames.h`、`src/view/PoseLibrary.cpp`、
  `docs/PRESENTATION.md` §1/§3.1、`tests/test_state_machine.cpp`、`tests/test_smoke.cpp`。
- 教训：**凡「文件 + 代码清单 + 资源清单」三处对应的资产，新增时三处必须同一次改到位**；
  建议把「`assets/poses/*.webp` 文件数 == `assets.qrc` 的 `poses/` 条目数 == `kPoseCount`」
  作为可自动核验的不变量——本次正是由该不变量的差值发现的问题。
  另外：`kPoses` 是**有序表 + 独立计数常量**，在中间插入一项会让尾项静默落到 `kPoseCount` 之外，
  新增时必须同步计数（或改为 `sizeof(kPoses)/sizeof(kPoses[0])` 直接推导）。

---

## TRAP-EXT0-005 · QTest 用例失败时 CTest 拿不到任何输出（`-o -,txt` 静默失效）

**阶段**：P8 立绘加载路径 A（2026-10-04）· **类型**：测试基础设施 · **影响面**：所有 GUI 测试的失败定位

### 现象

新增 `test_pose_assets` 后 CTest 报 `***Failed`，但**看不到任何用例结果**：

- `ctest --output-on-failure`：无输出；
- `ctest -V`（verbose）：同样无 QTest 用例行，只有汇总；
- 直接跑 `.\build\Debug\test_pose_assets.exe -o -,txt`（管道 / `Start-Process -RedirectStandardOutput`）：stdout 与 stderr 均为 **0 字节**；
- 退出码为 `1`（正常失败，非崩溃）。

同期 `test_smoke` 等既有 GUI 测试通过——它们只校验退出码，故从未暴露该问题。

### 根因

Qt 6.8.4 在 Windows 上，`QTest` 的 plain-text logger 写 stdout 这一路在
「无控制台 / 输出被重定向」的组合下不可靠：`qt_add_executable` 产出的 exe 走
Qt 的默认消息处理器，stdout 句柄在部分宿主下不可写，QTest **静默丢弃**全部用例行
（既不报错也不影响退出码）。这与 `docs/traps-P2.md` 记录的
「QTest 在无控制台时改走 `OutputDebugString`」是同一类问题的不同表现。

### 解决

不要依赖 stdout。**让 QTest 直接写文件**，再读该文件：

```powershell
$p = Start-Process -FilePath ".\build\Debug\test_pose_assets.exe" `
     -ArgumentList '-o','build\pose_result.txt,txt' `
     -WorkingDirectory "f:\develop\desktoppet" -NoNewWindow -PassThru -Wait
Get-Content build\pose_result.txt -Encoding Default   # 注意：QTest 输出非 UTF-8
```

关键点：

1. 参数用 `-o <文件>,txt`（**文件路径**，不是 `-`），绕开 stdout；
2. 读文件用 `-Encoding Default`（UTF-8 读取会得到空行——QTest 默认按本地代码页输出）；
3. CTest 侧保持既有 `-o -,txt` 不变：**退出码判定是可靠的**，只是失败详情拿不到；
   需要详情时用上面的文件法，不要因为「CTest 没输出」就去删断言或放宽比较。

### 影响与关联文档

- 关联：`cmake/Tests.cmake`（`-o -,txt` 的由来）、`docs/traps-P2.md`。
- 教训：**`ctest` 只报 `***Failed` 而无详情 ≠ 测试没有断言输出**。
  Windows + QtTest 环境下，定位失败必须准备「QTest 写文件」这条备用通道；
  且这条通道要**在写测试时就会用**，而不是等到失败了才临时找。

---

## TRAP-EXT0-006 · `PoseLibrary` 自身不初始化 qrc，静默依赖 `main()` 的调用顺序

**阶段**：P8 立绘加载路径 A（2026-10-04）· **类型**：隐式顺序依赖 · **影响面**：静态库资源初始化

### 现象

为 `PoseLibrary` 写单元测试时，单独构造 `PoseLibrary` 并调用 `startPreload()`，
12 张 core 立绘**全部加载失败**（`load()` 返回 false），只留 12 条
`[PoseLibrary] 立绘加载失败（跳过）` 告警。而同样的代码在应用里跑得好好的。

### 根因

立绘 qrc 挂在**静态库** `whalepet_view` 上，静态库资源不会自动注册，必须显式
`Q_INIT_RESOURCE(assets)`。旧实现里这段初始化被**写了两份**（`PoseView.cpp` 与
`main.cpp` 各一份 static 函数），而 `PoseLibrary::loadOne()` **自己不调**，
只是恰好依赖 `main.cpp` 在构造 `PetWindow` 之前抢先初始化过一次：

```
main() → whalepetInitAssetsResource()   ← 只有这一处真正保证了「先初始化」
      → PetWindow 构造 → PoseView::loadSourcePixmap() 才调 whalepetInitAssetsResource()
      → PoseLibrary::loadOne()          ← 从不自己调，靠上面那条隐式链路
```

于是「库可独立工作」这件事是**假的**。任何绕过 `main()` 的入口（单元测试、
将来的外部资源加载入口、将来的命令行工具）都会踩空，且失败形态是
「93 张全缺 + 只有 stderr 告警、界面停在首帧」——正是 `TRAP-EXT0-003` 的静默缺图家族。

### 解决

把初始化收敛为**全局作用域的单一入口** `src/view/AssetsResource.{h,cpp}`，
由 `PoseView` / `PoseLibrary::loadOne` / `main` 三处共用：

- `AssetsResource.cpp` 里的**定义必须在全局作用域**——`Q_INIT_RESOURCE` 宏不能进任何
  命名空间（含匿名命名空间），否则宏内声明的 `qInitResources_assets` 会被名称修饰成
  带命名空间的符号，与 rcc 在全局作用域生成的符号不匹配，链接期报 LNK2019
  （`docs/traps-P1.md` 已记录，此处是**第二处**需要该约束的地方）；
- `loadOne()` 开头自行调用，库不再依赖任何调用顺序；
- `qInitResources_assets()` 自带幂等保护，三处重复调用无害。

### 验证

- `test_pose_assets::libraryPreloadsCoreTierSynchronously`：裸构造 `PoseLibrary`
  （不经过 `main`、不经过 `PetWindow`）→ core 12 张全部就绪，`failedCount()==0`；
- `test_pose_assets::everyRegisteredPosePassesStrictValidation`：93 张逐张严格校验通过。

### 影响与关联文档

- 关联：`src/view/AssetsResource.{h,cpp}`、`src/view/PoseLibrary.cpp`、
  `src/view/PoseView.cpp`、`src/app/main.cpp`、`docs/traps-P1.md`。
- 教训：**静态库里的资源，「谁用谁初始化」比「在 main 里统一初始化」更可靠**。
  隐式的调用顺序依赖不会编译报错、不会链接报错，只在绕开它时静默失败；
  库级单测正是用来暴露这类「假的独立性」的。
