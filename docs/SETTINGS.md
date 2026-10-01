# 设置面板设计（SETTINGS）

> 去宿主化：whale 的「注入式设置 slot」**重做**为独立 Qt 设置对话框。

## 1. 形态

- 独立非模态 `QDialog`（`SettingsDialog`），由右键菜单 / 托盘打开。
- 分组折叠或标签页布局，样式遵循工作区 Qt UI 规范。

## 2. 设置项清单

| 分组 | 设置项 | 键 | 默认 |
|---|---|---|---|
| 陪伴表现 | 桌宠显示开关 | `pet_enabled` | 开 |
| | 台词气泡 | `bubble_enabled` | 开 |
| | 粒子/特效 | `particles_enabled` | 开 |
| | 关键词感知 | `keyword_aware` | **关** |
| | 深夜静默 | `night_quiet` | 开 |
| | 拖拽惯性 | `drag_inertia` | 开 |
| | 立绘尺寸 | `pose_size` | 200 |
| 养成与日常 | 每日任务 / 周签到 / 称号（展示 + 领取） | — | — |
| 成就墙 | 39 项成就展示（高亮/灰显） | — | — |
| 成长日记 | 最近 12 条，倒序 | — | — |
| 小游戏 | 小游戏开关（预留） | `minigame_enabled` | **关** |
| 数据与重置 | 重置位置 / 重置养成数据 / 打开数据目录 | — | — |

> 说明：whale 的「余额 / 天气 / TTS / 无障碍 / 主题」分组**全部移除**。

## 3. 存储

- 常规设置存 `settings` 表（单例行）；新增项写入 `json_ext`（JSON），保证向后兼容。
- 变更即时落库（见 `DATA-MODEL.md`）。

## 4. 交互约定

- 开关采用胶囊样式；「数据与重置」操作需二次确认。
- 「打开数据目录」用 `QDesktopServices` 打开 `data/`。
- 「重置位置」清除 `pos_x/pos_y` 并回到默认右下角。

## 5. 「找不到看板娘」防护

- 关闭桌宠显示后，**托盘/左下角保留唤回入口**（沿用 whale 🐋 思路），点击恢复显示。
- 位置越界时自动夹回可见区域，避免「跑到屏幕外找不到」。

## 6. 实现状态（P6）

| 交付项 | 代码位置 |
|---|---|
| 设置对话框（标签页式，非模态） | `src/view/SettingsDialog.{h,cpp}` |
| 陪伴表现：6 个开关 + 立绘尺寸 | `SettingsDialog::buildAppearanceTab` |
| 日常 / 成就墙 / 成长日记（内嵌复用 P4 面板） | `ContentPanel::embedInto`（独立窗口走 `showStandalone`） |
| 小游戏开关（预留 + 说明文案） | `SettingsDialog::buildMiniGameTab` |
| 数据与重置（重置位置 / 打开数据目录 / 重置养成，二次确认） | `SettingsDialog::buildDataTab` + `PetWindow` |
| 设置持久化（含 `json_ext` 扩展键） | `SettingsRepo::load` / `save` |
| 设置生效（尺寸 / 粒子 / 惯性 / 气泡 / 深夜静默 / 显隐） | `PetWindow::applySettings` |
| 唤回入口（托盘菜单 + 左下角浮动按钮） | `PetWindow::setupTray` / `setupRecallEntry` |
| 位置越界夹回 | `PetWindow::clampToVisibleArea`（启动 + 松手时） |

**落库位置**（按 §3 约定）：

- `pose_size` / `bubble_enabled` / `particles_enabled` / `keyword_aware` / `minigame_enabled` → `settings` 表既有列；
- `pet_enabled` / `night_quiet` / `drag_inertia` → `json_ext`（JSON），**不新建列**，保留未知键向后兼容。

**验证**：`ctest -C Debug` / `-C Release` 均 **9/9 通过**（新增 `test_settings`）；部署与冒烟结论见 `ROADMAP-P6-Fin.md`。
