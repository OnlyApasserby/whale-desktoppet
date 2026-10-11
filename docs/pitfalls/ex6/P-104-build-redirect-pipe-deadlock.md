# P-104：带输出重定向的构建命令必然超时（`RedirectStandardOutput` + 先 `WaitForExit` → 管道写满死锁）

- **原编号**：—（EX6 阶段新增，序号接续 `P-103`）
- **模块**：scripts / 构建自动化（AI 侧与脚本侧调用 `cmake --build` 的方式）
- **报错分层**：script（自动化命令本身；工程配置与构建产物均正常）
- **严重度**：中（阻断 AI 侧「全量回归」；改文件重定向即可规避，不需要改工程）
- **日期 / 版本**：2026-10-10，工作区状态（EX6 未提交改动）
- **环境**：CMake 4.4.2 / VS 18 2026（`Visual Studio 18 2026`，x64）/ Qt 6.8.4 / PowerShell 5.1（.NET Framework）/ 生成器多配置，构建目录 `build/`（`--config Debug`）
- **关联**：`P-004` / `P-096`（PowerShell 管道吞退出码）、`P-085`（`Start-Process -PassThru` 的 `ExitCode` 为 `$null`）、`docs/BUILD.md` §3

## 现象（可复现步骤 / 报错原文）

1. 以「重定向 stdout/stderr + 先 `WaitForExit` 再读」的方式跑 Debug 构建：

```powershell
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = 'C:\Program Files\CMake\bin\cmake.exe'
$psi.Arguments = '--build f:/develop/desktoppet/build --config ""'
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true      # 管道
$psi.RedirectStandardError = $true       # 管道
$psi.CreateNoWindow = $true
$p = [System.Diagnostics.Process]::Start($psi)
if (-not $p.WaitForExit(300000)) { try { $p.Kill() } catch {}; throw '超时' }   # 先等退出
$o = $p.StandardOutput.ReadToEnd()       # 退出之后才读 —— 永远不会走到这里
```

2. 结果：**每次都恰好在 300000 ms 抛「超时」**，且拿不到任何构建输出；
   `build/` 目录在这 5 分钟里只写了「增量判断」类文件，没有任何 `.obj` / `.exe`。
   （另一次由 AI 侧工具直接执行的构建同样超时，原因见「根因 ②」。）

3. 最小复现（与脚本同构，用任意高输出命令代替构建；实测输出 ≈ 1 MB）：

```powershell
# A：重定向到管道 + 先 WaitForExit
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = 'cmd.exe'; $psi.Arguments = '/c dir /s /b f:\develop\desktoppet\build'
$psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
$p = [System.Diagnostics.Process]::Start($psi)
$p.WaitForExit(10000)        # 实测：exited=False（10s 到点仍未退出，只能 Kill）

# B：同一条命令，重定向到文件
$p2 = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','dir /s /b f:\develop\desktoppet\build' `
      -NoNewWindow -PassThru -RedirectStandardOutput $env:TEMP\pipe-demo.log
$p2.WaitForExit(60000)       # 实测：exited=True，日志 10324 行 / 1001841 字节
```

现场时间线（2026-10-10，构建目录写入时间戳）：

| 时间 | 写入数 | 内容 |
|---|---|---|
| 18:54:25 | — | 构建启动，同时创建 23 个 MSBuild 节点进程 |
| 18:55–18:56 | 0 | 无任何写入 |
| 18:57–18:58 | 1122 | 只有 `.tlog` / `.lastbuildstate` / `.recipe`（302 组「增量判断」记录） |
| 18:58:12 之后 | 0 | 完全静默，直到 19:02 复查仍无写入；5 分钟到点抛「超时」 |
| 19:02 复查 | — | `cmake` 已死，**23 个 MSBuild 进程滞留**（各 CPU 仅 2–4 s） |

## 根因

① **`RedirectStandardOutput/Error = $true` 建的是管道，写满即阻塞**（.NET 管道缓冲为 KB 量级），
而脚本是「先 `WaitForExit(300000)`、退出后才 `ReadToEnd()`」——管道一旦写满，
子进程（cmake → MSBuild → 节点）阻塞在 `WriteFile`、父进程阻塞在等退出，**双方互等**，
必然在超时点抛「超时」。全量构建的 stdout（每目标一行 + 告警）是 1 MB 量级，远超缓冲，
所以这不是偶发：**每次都会卡在构建中途或尾部**。
Qt Creator 不触发该问题，因为它**持续流式消费**输出且不设等待上限。

② 我（AI）侧工具执行的构建是**另一条失效路径**：工具会流式读取输出（无 ① 的死锁），
但存在会话级等待上限，而全量 Debug 构建的耗时超过该上限 → 工具侧超时。
两条路径叠加，就是「Qt Creator 能全量构建、AI 侧必超时」的现象。

排除/附带发现：

- `--config ""`（空配置）**不是**卡住的原因：现场写入落在 `build/x64/Debug/`、`*_dir/Debug/`，
  说明它退化成了默认配置 Debug；但空值写法不可依赖，多配置生成器必须显式 `--config Debug`。
- **300 s 预算是全量构建不够用的**（同机 Release 增量已占约 3 分钟）；且 `$p.Kill()` 只杀 `cmake` 本体，
  **不杀 MSBuild 子进程树** → 残留 23 个节点进程，后续构建若走 node reuse 复用它们会更难排查。
- 那 5 分钟里到底是「管道写满」还是「编译/节点卡住」无法只看文件系统判定（没有日志），
  但两种失效都被下面的写法覆盖。

## 解决或规避

按使用场景三选一（**都不改工程配置**）：

```powershell
# (1) 本机控制台、要实时输出（推荐）：不用管道，退出码不会被吞（P-004 / P-096）
& 'C:\Program Files\CMake\bin\cmake.exe' --build f:/develop/desktoppet/build --config Debug --parallel
"exit=$LASTEXITCODE"

# (2) 脚本/自动化、要超时保护：重定向到**文件**（不是管道）+ 杀整棵进程树
$log = 'f:/develop/desktoppet/build/msbuild-debug.log'
$p = Start-Process -FilePath 'C:\Program Files\CMake\bin\cmake.exe' `
      -ArgumentList @('--build','f:/develop/desktoppet/build','--config','Debug','--parallel') `
      -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
if (-not $p.WaitForExit(1800000)) { & taskkill /PID $p.Id /T /F | Out-Null; throw "构建超时，日志：$log" }
# 退出码用 System.Diagnostics.Process 读（PS 5.1 的 -PassThru 取不到 ExitCode，见 P-085）
Get-Content $log -Tail 50

# (3) 边跑边看：另开一个窗口
Get-Content 'f:/develop/desktoppet/build/msbuild-debug.log' -Wait -Tail 30
```

- 构建前清掉上次被强杀留下的节点：`Get-Process MSBuild -ErrorAction SilentlyContinue | Stop-Process -Force`。
- 验证：A/B 对照（同一高输出命令）——A `exited=False`、B `exited=True`（10324 行 / 1001841 字节）；
  **端到端全量 Debug 构建待下一次执行回填**（本条不宣称已跑通全量构建）。
- 降级：无。

## 影响与关联文档

- 影响：AI 侧无法单独完成「全量回归」，需改为「日志文件 + 长超时 + 后台轮询」或交回用户控制台；
  被强杀的构建会遗留 MSBuild 节点进程，进而干扰下一次构建。
- 关联：`P-004`、`P-096`、`P-085`；`docs/BUILD.md` §3；技能 `qt-msvc-cmake` §5.1（命令必须带超时）、
  技能 `debug` §5.1（带超时执行模板）。
