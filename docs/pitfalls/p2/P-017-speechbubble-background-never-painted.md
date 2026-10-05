# `#SpeechBubble` 圆角背景框规则存在却**从未绘制**（气泡只有字没有底）

> **原编号**：`TRAP-P2-012`　**阶段**：P2　**来源**：原按阶段聚合的 `traps-P2.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：表现 / 窗口与样式表 ｜ **影响**：`PRESENTATION.md` §4 承诺的「深色底 + 圆角 + 白字」气泡
实际呈现为**裸文字**（无圆角矩形背景框），`project.qss` 的 `#SpeechBubble` 规则形同虚设。

### 现象（2026-10-04，用户侧）

真实桌面上鲸鱼娘说话时，字幕文字直接浮在桌面上，**看不到圆角矩形背景框**。

### 根因（已用一次性探针实测，非推测）

`SpeechBubble` 是**自定义 `QWidget` 子类**的「无边框 + `WA_TranslucentBackground`」顶层窗口。
Qt **不会**把样式表里的 `background-color` / `border` / `border-radius` 自动画到这类窗口上 ——
样式表只提供「外观描述」，还需要控件自己把 `QStyle::PE_Widget` 交给 `style()` 绘制。

探针（同一 `#SpeechBubble` 规则 + 同一窗口标志，`offscreen` 下 `grab()` 取像素）：

| 变体 | 中心 alpha | (0,0) alpha | 圆角内 (2,2) alpha | 结论 |
|---|---|---|---|---|
| 什么都不加 | **0** | 0 | 0 | 只有文字，**无背景框** |
| 只加 `WA_StyledBackground` | **0** | 0 | 0 | 仍然无背景框（该属性对本形态无效） |
| `paintEvent` 转发 `QStyle::PE_Widget` | **255** | 0 | 228 | 黑底 + 1px 边框 + 7px 圆角抗锯齿 ✅ |
| 属性 + `paintEvent`（对照） | 255 | 0 | 228 | 与上一行**完全一致** → 样式路径未重复绘制，无双绘 |

### 解决

在 `SpeechBubble::paintEvent()` 里转发 `PE_Widget`；**外观取值仍全部来自 `#SpeechBubble` 规则**
（不改样式表、不新增 CSS 类、不写颜色字面量、不调用 `setStyleSheet`）。
同时把定位改为「整体贴在立绘窗口之外」：上方优先，贴屏幕顶端时翻到下方，与立绘矩形永不相交
（气泡是独立顶层窗口，**不能**用 z 序避让 —— 降 z 会被立绘 / 桌面盖住）。

### 验证

- 新增 `tests/test_settings.cpp::bubbleDrawsRoundedBackground`：加载**真实全局样式表**
  （`:/qt-ui/default.qss` + `:/qt-ui/project.qss`）后断言中心 `alpha == 255`、(0,0) `alpha == 0`、
  圆角内 `alpha < 255`（抗锯齿）。修复前中心为 0，该用例必然失败。
- 新增 `tests/test_settings.cpp::bubbleAvoidsCoveringAnchor`：常规位置气泡在立绘**上方**且不相交；
  立绘贴屏幕顶端时翻到**下方**且不相交。
- `ctest -C Debug` 全量 **35/35 通过**（`test_settings` 7/7）。

> 教训：**「规则写了」不等于「画出来了」**。凡自定义 `QWidget` 子类 + 样式表背景 / 圆角，
> 必须有像素级断言或 `paintEvent` 转发兜底，不能只看 `.qss` 里是否存在规则。

### 影响与关联文档

`src/view/SpeechBubble.{h,cpp}`、`tests/test_settings.cpp`、
`PRESENTATION.md` §4、`resources/qt-ui/project.qss`（**未改动**）。

---
