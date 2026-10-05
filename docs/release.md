# WhalePet · 发布记录

> **本文件是发布相关内容的唯一台账**：版本号、产物名与路径、协议关联、待确认项。
> 打包/安装的**操作步骤与维护清单**见 [`packages.md`](packages.md)；构建目录归属与复用记录见 [`BUILD.md`](BUILD.md) §10；目录与命名规范见 [`README.md`](README.md) §二。

---

## 一、发布流程与命名规范

### 1.1 目录与产物命名（固定）

| 项 | 约定 | 本工程取值 |
|---|---|---|
| 打包构建目录 | **必须复用工作区已有构建目录**；**禁止新建、删除或切换构建目录** | `build-package/`（`-DWHALEPET_PACKAGE=ON`，Release 产物落 `dist/WhalePet`，无 PDB） |
| 免安装版目录 | `dist/<App>-<Version>-portable/` | `dist/WhalePet-0.2.0-portable/` |
| 免安装版压缩包 | `dist/<App>-<Version>-portable.zip` | `dist/WhalePet-0.2.0-portable.zip` |
| 安装版 | `dist/<App>-<Version>-setup.exe` | `dist/WhalePet-0.2.0-setup.exe` |
| 发布产物存放 | **只进 `dist/`**，其他位置不得存放发布产物 | — |
| 中间目录（非发布产物） | `deploy-release/`（开发用 Release + PDB）、`dist/WhalePet/`（CMake 的 exe 落点） | 均不入库 |

### 1.2 脚本

| 脚本 | 作用 |
|---|---|
| `scripts/package-release.ps1` | **唯一发布驱动**：复用既有构建目录 → 暂存 `dist/<App>-<Version>-portable/` → `windeployqt` → 调试文件清理 / `engine/` 置空 / `plugins/` 清理 / 随包 `README.md`+`LICENSE` → 打 zip → 调 `makensis` 产出 `-setup.exe` |
| `scripts/installer.nsi` | NSIS 安装包定义：消费 `/DAPP_NAME` `/DAPP_VERSION` `/DAPP_VERSION4` `/DSRC_DIR` `/DOUT_FILE`（绝对路径）；保留完整性级别（UIPI 拖拽）、运行期写权限、64 位安装视图与卸载清理等既有修复（见 `packages.md`） |
| `scripts/init-workspace.ps1` | 工作区目录初始化（技能第 1～2 步）。本工作区已完成该步，**仅在新建工作区时使用** |
| `scripts/init-git-remote.ps1` | `git init` 与远程仓库确认流程 |

用法：

```powershell
# 必须在工作区根目录执行；构建目录必须已存在且已构建（脚本只复用，不新建）
powershell -ExecutionPolicy Bypass -File scripts/package-release.ps1 `
    -AppName WhalePet -Version 0.2.0 -BuildDir build-package
```

WhalePet 专属默认值（可用参数覆盖）：`-ExtraExe whalepet-mcp.exe`（MCP 桥接随包）、
`-PurgeDebugFiles`（清理 `*.pdb/*.ilk/*.exp/*.lib`）、`-EmptyFolders engine`（空目录随包）、
`-PurgeFolders plugins`（第三方插件绝不随包）、`-ShipFiles README.md,LICENSE`（MIT 要求随包携带声明）。
其余：`-QtRoot`、`-NsisExe`、`-NsiScript`、`-NsisInputCharset`、`-SkipZip`、`-SkipInstaller`。

> **历史脚本已退役**：旧 `scripts/make-package.ps1` 因**自行新建** `build-package/` 构建目录，
> 与「禁止新建构建目录」约束冲突，且与 `package-release.ps1` 职责重复，已删除；其 WhalePet 特有
> 后处理（mcp 随包、调试文件清理、`engine/` 置空、`plugins/` 清理、`README.md`/`LICENSE` 随包）
> 已全部迁入 `package-release.ps1` 并参数化。

---

## 二、协议关联

| 项 | 结论 | 依据 |
|---|---|---|
| 本项目协议 | **MIT** | 根目录 `LICENSE`（`Copyright (c) 2026 OnlyApasserby`），与技能 `assets/LICENSE-MIT.txt` 模板一致 |
| 参考项目检测 | **未命中 Copyleft（GPL/AGPL/CC-BY/SPDX 均无）**，**无需调整为 GPL-3.0** | 检测范围 `references/**`，详见 [`../references/README.md`](../references/README.md) §三 |
| 参考项目 1：dsh-whale-musume | **MIT（已验证）** | `references/dsh-whale-musume/LICENSE`、`package.json` 均声明 MIT |
| 参考项目 2：DesktopPet | **声明 MIT 但仓库内未落盘授权文件（已验证）** | `references/DesktopPet/README.md:232` 链接指向不存在的 `LICENSE` |
| 处置 | DesktopPet 复用**收敛为「实现思路 / 架构参考」**，不复用其源码与美术资产；如需复用须先补齐授权 | 同上 |

**分发注意**：免安装版压缩包与安装包内**必须随包携带 `LICENSE`**（MIT 要求保留版权与许可声明）；
这一条已由 `package-release.ps1 -ShipFiles`（默认含 `README.md`、`LICENSE`）与 `installer.nsi` 的
`File /r` + 卸载 `Delete "$INSTDIR\LICENSE"` 对应关系共同保证。

---

## 三、已发布记录

| 版本 | 日期 | 产物 | 路径 | 命名是否符合 §1.1 | 备注 |
|---|---|---|---|---|---|
| 0.2.0 | 2026-10（历史） | 免安装版目录 | `dist/WhalePet/` | ❌ 不符合 | 由旧 `make-package.ps1` 产出 |
| 0.2.0 | 2026-10（历史） | 免安装版压缩包 | `dist/WhalePet.zip` | ❌ 不符合 | 同上 |
| 0.2.0 | 2026-10（历史） | 安装包 | `dist/WhalePet-Setup-0.2.0.exe` | ❌ 不符合 | 同上，NSIS 旧默认 `OutFile` |
| — | — | — | — | — | **尚无按 §1.1 规范产出的发布记录**（见 §四 #1） |

---

## 四、待确认项

| # | 事项 | 状态 | 说明 |
|---|---|---|---|
| 1 | 按 §1.1 规范重出 0.2.0 三个产物（覆盖旧命名） | ⏳ 待用户执行 | 命令已就绪（§1.2）；需用户自行跑构建与打包，AI 不代跑构建 |
| 2 | 旧发布脚本 `make-package.ps1` 的去留 | ✅ 已决 | 已删除并替换为 `package-release.ps1`，WhalePet 特有后处理已迁入（§1.2） |
| 3 | `installer.nsi` 与技能 `/D` 契约对齐 | ✅ 已完成 | 已改为消费 `APP_NAME/APP_VERSION/APP_VERSION4/SRC_DIR/OUT_FILE`，并保留 UIPI 拖拽、卸载残留、64 位安装视图、运行期写权限等既有修复 |
| 4 | `VIProductVersion` 手工同步风险 | ✅ 已消除 | 4 段版本改由发布脚本按 `-Version` 补零推导（`/DAPP_VERSION4`） |
| 5 | DesktopPet 授权补齐后可扩大复用范围 | ⏳ 待用户确认 | 需用户向原作者确认 |

---

## 五、变更历史

| 日期 | 变更 | 说明 |
|---|---|---|
| 2026-10-05 | 建立本文件 | 目录改造（`packaging/` → `scripts/`、`referances/` → `references/`、`traps-*.md` → `pitfalls/`）的一部分，补上此前缺失的发布台账 |
| 2026-10-05 | 发布脚本换代 | `make-package.ps1` → `package-release.ps1`（复用既有构建目录 + §1.1 命名）；`installer.nsi` 改为 `/D` 契约并自动推导 4 段版本 |
| 2026-10-05 | 踩坑记录重组 | `docs/pitfalls/` 改为**按阶段分文件夹**（`p1/` … `p8/`、`ext0/`、`ex1/`，共 80 条），索引统一为 `docs/pitfalls/index.md` |
