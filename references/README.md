# WhalePet · 参考项目归置说明

> 本目录（`references/`）存放**只读参考**的第三方项目资料。
> `.gitignore` 规则为 `/references/*` + `!/references/README.md`：
> **本文件是 `references/` 下唯一进入仓库的文件**，其余内容（含嵌套 git 仓库）一律不入库。

---

## 一、纪律

1. **只读**：参考项目用于「读思路、抄规则、对标资源」，**不得在其中修改任何文件**，也不得把它纳入本项目的构建（`CMakeLists.txt` 不得 `add_subdirectory` 到本目录）。
2. **不随包分发**：`references/` 不参与构建与打包，不进 `dist/`。
3. **不入库**：参考项目体积大且自带版本历史，本地保留即可；仓库只保留本说明文件。
   （历史上 `referances/DesktopPet`、`referances/dsh-whale-musume` 曾被 `git add` 成 gitlink 且无 `.gitmodules`，
   属于「半子模块」坏状态；已改为「本地目录 + 忽略」，避免 `git submodule` 报错。）
4. **不便归置的第三方源码**放根目录 `dummy/`（已忽略），不要再往 `references/` 里塞。

---

## 二、清单与复用边界

| 参考项目 | 本地路径 | 形态 | 复用什么 | 不复用什么 |
|---|---|---|---|---|
| DesktopPet | `references/DesktopPet/` | Qt/C++ 桌宠（Qt 6.9.1 + MinGW） | 透明置顶窗口 / 拖拽 / 右键菜单的实现思路；分层解耦思路 | 其构建系统、GIF 播放路线、RPG 玩法 |
| dsh-whale-musume | `references/dsh-whale-musume/` | Node/前端（注入式宿主插件） | 92 张立绘；状态机纯逻辑；养成/成就/任务/签到/日记规则；台词库与关键词感知 | DSH DOM 契约层、天气、余额、TTS、无障碍、主题适配、注入式设置 |

---

## 三、开源协议检测结论

检测范围：`references/**`（关键词 `GPL` / `AGPL` / `GNU GENERAL PUBLIC` / `SPDX-License-Identifier` / `CC-BY` / `Copyleft`）。
**结论：未命中任何强著佐权（Copyleft）信号；本项目维持 `LICENSE` 的 MIT 授权，无需调整为 GPL-3.0。**

| 参考项目 | 检测结果 | 证据（原文） |
|---|---|---|
| dsh-whale-musume | **MIT（已验证）** | `references/dsh-whale-musume/LICENSE:1` → `MIT License`；`:3` → `Copyright (c) 2026 Sutera-Diffusus`；`references/dsh-whale-musume/package.json:29` → `"license": "MIT",` |
| DesktopPet | **声明 MIT，但仓库内未落盘授权文件（已验证）** | `references/DesktopPet/README.md:232` → `本项目采用 [MIT License](LICENSE) 许可证。`；全目录 `LICENSE*` 检索**仅命中 dsh 的 1 个文件**，`references/DesktopPet/` 下不存在 `LICENSE` |

**风险与处置（DesktopPet）**：README 的链接指向不存在的 `LICENSE`，属「声明 MIT 但授权文本缺失」，
严格口径下不能据 README 一句话视为已授权。故本项目对 DesktopPet 的复用**收敛为「实现思路 / 架构参考」**，
不复用其源码与美术资产；若后续确需复用代码或资源，须先向原作者确认授权并补齐授权文本。

---

## 四、关联文档

- 设计与复用边界：`docs/README.md` §四「参考项目与复用边界」
- 资产来源：`docs/PRESENTATION.md`、`docs/POSE-ASSETS.md`
- 协议与发布流程：`docs/packages.md`
