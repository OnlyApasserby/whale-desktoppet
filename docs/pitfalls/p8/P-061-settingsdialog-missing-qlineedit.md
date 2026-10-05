# `SettingsDialog.cpp` 缺 `#include <QLineEdit>` → C2027

> **原编号**：`TRAP-P8-003`　**阶段**：P8　**来源**：原按阶段聚合的 `traps-P8.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：编译 ｜ **影响**：`whalepet_view` 编译失败。

### 现象

```
src/view/SettingsDialog.cpp(100,33): error C2027: 使用了未定义类型“QLineEdit”
src/view/SettingsDialog.cpp(103,15): error C2665: “QFormLayout::addRow”: 没有重载函数可以转换所有参数类型
```

### 根因

新增天气设置项时只在 `SettingsDialog.h` 做了 `class QLineEdit;` 前向声明，
但 `.cpp` 里实际 `new QLineEdit(...)` / 调用 `editingFinished` / `setText`，
需要完整类型；漏加 `#include <QLineEdit>`。连带 `QFormLayout::addRow` 因参数类型不完整而报重载不匹配。

### 解决

在 `SettingsDialog.cpp` 补 `#include <QLineEdit>`。

### 影响与关联文档

`src/view/SettingsDialog.{h,cpp}`；见 `SETTINGS.md` §4。

---
