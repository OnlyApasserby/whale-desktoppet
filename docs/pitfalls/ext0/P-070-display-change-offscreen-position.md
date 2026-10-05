# 分辨率调整 / 显示器增删后立绘「看不见」：启动沿用历史坐标，未按当前屏幕重算

> **原编号**：`TRAP-EXT0-002`　**阶段**：EXT0　**来源**：原按阶段聚合的 `traps-extend0.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

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
