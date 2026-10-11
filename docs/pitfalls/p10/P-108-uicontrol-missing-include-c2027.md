# P-108 · 新增 QComboBox 控件只在头文件前向声明，`.cpp` 未 include → C2027 / C2664

| 字段 | 取值 |
|---|---|
| 阶段 | P10（工作能力插件；③ 工作台面板 / 设置项） |
| 原编号 | —（P10 新增） |
| 模块 | `src/view/SettingsDialog.{h,cpp}` |
| 报错分层 | build（编译期） |
| 严重度 | minor（编译期即暴露；修正后 36/36 通过） |

## 一、现象（可复现步骤 / 报错原文）

把设置页「代码彩蛋」组替换为「工作台」组时，新增成员 `QComboBox *m_workDuration`，只在
`SettingsDialog.h` 加了 `class QComboBox;` 前向声明，`.cpp` 未 include 对应头，Debug 构建报错：

```text
F:\develop\desktoppet\src\view\SettingsDialog.cpp(156,15): error C2664:
  “void QFormLayout::addRow(QWidget *,QLayout *)”: 无法将参数 1 从“QString”转换为“QWidget *”
F:\develop\desktoppet\src\view\SettingsDialog.cpp(156,15): error C2664:
  “void QFormLayout::addRow(QWidget *,QWidget *)”: 无法将参数 1 从“QString”转换为“QWidget *”
F:\develop\desktoppet\src\view\SettingsDialog.cpp(360,27): error C2027: 使用了未定义类型“QComboBox”
F:\develop\desktoppet\src\view\SettingsDialog.cpp(360,19): error C2737: “index”: 必须初始化 const 对象
F:\develop\desktoppet\src\view\SettingsDialog.cpp(409,28): error C2027: 使用了未定义类型“QComboBox”
```

复现步骤：在 `.cpp` 中使用 `m_workDuration->addItem(...)` / `findData(...)` /
`setCurrentIndex(...)` / `currentData()`，而只为该类型做了前向声明。

## 二、根因

`class QComboBox;` 只声明「有这么个类型」，**不提供完整类型**：调用其成员方法、把它隐式转换为
`QWidget *`（`QFormLayout::addRow` 的重载解析）都需要完整定义。于是：
- `C2664`（`addRow` 重载匹配失败）是**表象**——`QComboBox *` 在类型不完整时无法转换到 `QWidget *`；
- `C2027`（使用未定义类型）与 `C2737`（`const` 对象必须初始化）才是根因的直接报错。

## 三、解决或规避

`src/view/SettingsDialog.cpp` 补 include（并顺带补时长预设真源头，见 P10 设计）：

```cpp
#include "viewmodel/WorkService.h" // P10：工作台时长预设（设置项与工作台面板共用同一真源）

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
```

修后 Debug 构建退出码 0；Debug / Release CTest 各 **36/36**。

**规避口径**：头文件保留前向声明是本项目惯例（减少编译依赖），但**一旦在 `.cpp` 里调用该类型的
成员方法或参与重载解析，就必须补 include**；新增控件后先对单目标编译一次（`--target whalepet_view`），
不要等整棵依赖树重编完才发现。

## 四、影响与关联文档

- 影响面：仅编译期；未产生错误产物（构建失败即停，未进入运行 / 打包环节）。
- 关联：`docs/pitfalls/p10/P-109-*`（同日同类「替换后漏改引用」）、`src/view/SettingsDialog.h`
  （前向声明清单）、`docs/SETTINGS.md` §2（工作台三键）。
