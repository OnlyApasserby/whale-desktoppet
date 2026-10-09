# P-098 · 安装包需提权（UAC），静默安装/卸载往返无法在自动化会话完成

- **模块**：`scripts/installer.nsi`（`RequestExecutionLevel admin`）
- **报错分层**：script（发布编排）
- **严重度**：minor（但直接影响「发布门禁能否判定通过」）
- **原编号**：—（EX4 新增）

## 现象（可复现步骤 / 报错原文）

`installer.nsi` 头部设定：

```nsis
RequestExecutionLevel admin
```

打包阶段 `makensis` 正常完成、`dist/WhalePet-0.3.0-setup.exe`（13.2 MB）产出，
但按 `workspace-manage` §6 的发布验证要求，需要再做一次
「静默安装到临时目录 → 静默卸载 → 目录清空」的往返。在**非提权的自动化会话**里执行
`setup.exe /S` 会触发 UAC 交互，无法在无人值守下完成，因此该验证项**本轮未能执行**。

## 根因

安装器按设计需要写 `HKLM`（`HKLM\...\Uninstall`）、`$PROGRAMFILES64` 安装目录并对
`stomach/`、`engine/` 授予普通用户写权限（`icacls`），因此必须提权；而 UAC 提权是**交互式**的，
不能由脚本静默完成。这不是脚本缺陷，而是「提权安装器 + 无人值守验证」之间的固有边界。

## 解决或规避

- 解决：**如实登记为待人工验收，不记作通过**。已登记为
  `docs/release.md` §四.6 与 `docs/TESTING.md` §8.2 第 10 项。
- 规避：
  1. 自动化会话只验「安装包**编译**成功 + 产物存在且非空 + 命名合规」，**不冒充**已验证往返；
  2. 需要自动化的 CI 场景应另做一个**提权后的 runner**，或另出 `RequestExecutionLevel user`
     的测试用安装器 —— 但**不得**为本项改动正式安装器的提权设定；
  3. 人工验证要点按 `docs/packages.md` §四「安装 ↔ 卸载对应表」逐条核对（每个安装期写入都要有卸载条目）。

## 影响与关联文档

- 关联：`scripts/installer.nsi`、`docs/release.md` §1.3 / §四.6、`docs/TESTING.md` §8.2 第 10 项、
  `docs/packages.md` §四；提权相关的既有陷阱 `P-069`（安装器提权拉起程序导致拖拽被 UIPI 拦截）。
- 结果：本轮该验证项状态为「**待用户验收**」，未计入通过。