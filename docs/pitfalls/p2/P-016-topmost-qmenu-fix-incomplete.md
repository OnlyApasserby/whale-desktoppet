# `TRAP-P2-010` 修复不彻底：立绘压住菜单 + 桌宠抢焦点

> **原编号**：`TRAP-P2-011`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：窗口层级 / 焦点 ｜ **影响**：右键菜单被立绘**遮挡**（视觉问题，菜单项仍可点击）；
桌宠窗口会抢占系统焦点（`PRESENTATION.md §3` 与 `PetWindow.h` 注释声称的「不抢焦点」实际未落实）

### 现象（用户复现）

真实桌面右键立绘：菜单弹出后**仍被立绘图像盖住**（仅视觉，不影响菜单选项点击）。
即 `TRAP-P2-010` 的「菜单置顶 + `Show` 时 `raise()`」并未彻底解决。

### 根因

`PetWindow` 的窗口标志只有 `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool`：

- `Qt::Tool` 只保证「不进任务栏」，**并不等于不抢焦点**；
- 桌宠窗口因此仍可成为系统的**活动窗口**。右键菜单是 `Qt::Popup`，弹出后立绘若仍是
  「活动的置顶窗口」，就会在同为 `WS_EX_TOPMOST` 的 Z 序里保持在最前，从而压住菜单。
- 同一文档/头注释里「不抢焦点（Qt::Tool）」的表述与实现不符（对照 `SpeechBubble` 早已显式
  加了 `Qt::WindowDoesNotAcceptFocus`）。

### 解决

给桌宠窗口补上「不抢焦点」的显式声明（与 `SpeechBubble` / 唤回入口一致）：

```cpp
setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
               | Qt::WindowDoesNotAcceptFocus);   // Windows 等价 WS_EX_NOACTIVATE
setAttribute(Qt::WA_ShowWithoutActivating);
```

同时在菜单 `Show` 事件里除 `menu->raise()` 外，再抬一次其原生 `QWindow`：

```cpp
if (QWindow *handle = menu->windowHandle()) { handle->raise(); }
```

鼠标点击与拖拽不受 `WindowDoesNotAcceptFocus` 影响（该属性只拒绝键盘焦点/激活）。

### 验证状态

- **已验证**：Debug / Release 均可构建；`ctest` 各 **9/9 通过**。
- **待人工目视复验**：真实桌面上右键菜单不再被立绘遮挡、桌宠不再抢焦点
  （`offscreen` 无窗口管理，无法自动化验证层级）。

### 影响与关联文档

- 修正 `TRAP-P2-010` 的结论：**「同级置顶」不足以解决**，还需「桌宠不参与激活」。
- `PRESENTATION.md §3` 窗口标志行需同步为含 `Qt::WindowDoesNotAcceptFocus`。

---
