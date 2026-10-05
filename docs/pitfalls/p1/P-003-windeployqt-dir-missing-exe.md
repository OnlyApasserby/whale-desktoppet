# `windeployqt --dir` 不复制 exe 本体，部署目录缺主程序

> **原编号**：`TRAP-P1-003`　**阶段**：P1　**来源**：原按阶段聚合的 `traps-P1.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：部署 ｜ **影响**：clean PATH 下「双击运行」验证失败

### 现象

按 `BUILD.md` §3 执行部署：

```powershell
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime `
    --dir .\deploy-release .\build\Release\WhalePet.exe
```

部署日志正常（拷贝了 `Qt6Widgets.dll`、`platforms/qwindows.dll`、`imageformats/qwebp.dll` 等），
但随后启动报：

```
Start-Process : 由于出现以下错误，无法运行此命令: The system cannot find the file specified。
```

检查发现 `deploy-release\WhalePet.exe` **不存在**。

### 根因

`windeployqt` 的语义是「为**指定路径的 exe** 部署它依赖的 Qt 运行时」；`--dir` 只决定
**依赖 DLL 的落地目录**，**不会把 exe 本身复制过去**。因此 `--dir` 指向一个非 exe 所在目录时，
该目录里有全套 DLL，却没有主程序。

### 解决

先把 exe 复制到部署目录，再在其上部署（或直接把 `--dir` 指到 exe 所在目录）：

```powershell
Copy-Item .\build\Release\WhalePet.exe .\deploy-release\ -Force
& 'D:\Qt-debug\bin\windeployqt.exe' --release --no-translations --compiler-runtime `
    --dir .\deploy-release .\deploy-release\WhalePet.exe
```

修复后部署目录可独立启动，`WhalePet.exe` 运行正常。

### 影响与关联文档

- 关联：`BUILD.md` §3（部署命令）、`BUILD.md` §8（「运行缺 `Qt6Core.dll` → 用 windeployqt 配当前配置」）。
- 建议后续把「复制 exe → windeployqt」固化为一条部署脚本 / CMake `POST_BUILD`，避免每次手动补拷贝。

> **现状更新（P3 收尾，2026-09-30）**：上述「手动补拷贝」已彻底取消。
> `CMakeLists.txt` 用 `WHALEPET_DEPLOY_DIR` 设置 `WhalePet` 的
> `RUNTIME_OUTPUT_DIRECTORY_RELEASE`，**Release 产物直接生成在 `deploy-release/`**，
> `windeployqt` 只需对 `.\deploy-release\WhalePet.exe` 原地部署。
> 本条记录保留作历史，不再需要 `Copy-Item`。详见 `BUILD.md` §3。

---
