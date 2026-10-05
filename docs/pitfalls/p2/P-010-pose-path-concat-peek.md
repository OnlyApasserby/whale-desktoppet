# 立绘路径拼装方式对 `*-peek` 不通用

> **原编号**：`TRAP-P2-005`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：数据/资产 ｜ **影响**：3 张 `*-peek` 立绘永远加载不出来（静默缺图）

### 现象

P1 沿用的取图方式是「pose 名 → `state-<name>`」再拼资源路径。P2 接入全部 92 张时发现：
`home-peek` / `settings-peek` / 另一张 peek 的实际文件名是 `dsh-whale-home-peek.webp`，
**不含 `state-` 前缀**，按 `state-%1` 拼出来的路径不存在 → 命中「缺图 → 占位待图」分支。

### 根因

92 张资产并非**单一命名规则**：89 张为 `dsh-whale-state-*`，3 张为 `dsh-whale-*-peek`。
用规则推导路径在资产集合不齐整时必然漏项。

### 解决

放弃拼装，改为**显式清单映射**：由脚本扫描 `assets/poses/*.webp` 生成
`src/core/PoseNames.h`（`PoseEntry{key,file}` 表 + `kPoseCount`），取图走
`PoseCatalog::poseFile()`；`*.qrc` 同样由脚本生成，避免手写 92 项出错。

- 清单文件头部明确「本文件自动生成，禁止手改」。
- 单测补「清单完整性」用例：`kPoseCount == 92`、key 唯一、`poseExists()` 与状态机输出一致。

### 影响与关联文档

- 约定：**资产清单与路径一律来自生成文件/单一目录**，禁止在代码里用命名规则推导资源文件名。
- 关联：`src/core/PoseNames.h`、`src/core/PoseCatalog.h`、`src/view/PoseLibrary.cpp`、`docs/PRESENTATION.md`。

---
