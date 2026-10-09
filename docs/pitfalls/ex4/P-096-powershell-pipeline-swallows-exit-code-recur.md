# P-096 · PowerShell 管道吞掉构建退出码，编译失败被误判为成功（**重复命中 P-004**）

- **模块**：构建命令（PowerShell 调用 `cmake --build`）
- **报错分层**：script（构建编排）
- **严重度**：major（会把「构建失败」显示成「成功」，属结果误判）
- **原编号**：—（EX4 新增；**重复命中 `P-004`**）

## 现象（可复现步骤 / 报错原文）

EX4 首次构建用了这样的命令（把构建输出接到筛选 cmdlet 上取尾部）：

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build f:/develop/desktoppet/build --config Debug --parallel 2>&1 | Select-Object -Last 60
```

命令**成功返回**（工具侧 `exitCode` 与 `$LASTEXITCODE` 均为 0），但展开的输出里其实已经有：

```
src\viewmodel\MiniGameCompanionSource.cpp(66,18): error C2039: "gameSampleFromSnapshot":
不是 "whalepet::core" 的成员
```

即：**构建确实失败，却被判成成功**。若不是逐行看输出，会直接进入下一步。

## 根因

PowerShell 管道的退出状态取自**管道最后一个命令**（这里是 `Select-Object`），
`$LASTEXITCODE` 也不代表管道整体结果；`cmake.exe` 的非零退出码被管道末端吞掉。
本条与既有 **`P-004`** 是同一机制（上次发生在「`ctest` 输出被管道过滤」场景），
本次在「`cmake --build` 输出被管道过滤」场景再次命中。

## 解决或规避

- 修复（本轮的可靠写法）：**先把输出落盘，再显式读取并打印退出码，最后用带 `-Encoding` 的
  `Select-String` 单独筛错**，构建命令本身不接筛选 cmdlet：

  ```powershell
  & 'C:\Program Files\CMake\bin\cmake.exe' --build f:/develop/desktoppet/build --config Debug --parallel 2>&1 |
      Out-File -Encoding utf8 f:\develop\desktoppet\build\ex4-build.log
  $c = $LASTEXITCODE
  Write-Output "BUILD_EXIT=$c"
  Select-String -Path f:\develop\desktoppet\build\ex4-build.log -Pattern ': error ' -Encoding utf8 |
      Select-Object -First 20 | ForEach-Object { $_.Line }
  ```

  实际结果：`BUILD_EXIT=1` + 两行 C2039/C3861 —— 失败被如实暴露。
- 规避：**判据只认原生命令的退出码**；要展示 / 过滤输出时，先落盘再读，
  不要让 `cmake` / `ctest` / 测试可执行文件成为「被管道末端 cmdlet 决定状态」的前段。

## 影响与关联文档

- 关联：`docs/pitfalls/p1/P-004-powershell-pipeline-swallows-exit-code.md`（原条目）、
  `qt-msvc-cmake` 第 5.1 节（超时与退出码要求）。
- 结果：改用「落盘 + 显式退出码」后，Debug / Release 构建与 CTest 均以 `BUILD_EXIT` / `CTEST_EXIT`
  显式判定；Debug / Release CTest 各 **33/33**。
