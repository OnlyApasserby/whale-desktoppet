# P-110 · 覆盖层几何用 `QGuiApplication::virtualGeometry()` → C2039 / C3861（该静态方法不存在）

| 字段 | 取值 |
|---|---|
| 阶段 | P11（绘画模块；屏幕取色覆盖层） |
| 原编号 | —（P11 新增） |
| 模块 | `src/view/ui/ScreenColorPickerOverlay.cpp` |
| 报错分层 | build（编译期） |
| 严重度 | minor（编译期即暴露；修正后 Debug / Release 各 37/37 通过） |

## 一、现象（可复现步骤 / 报错原文）

屏幕取色覆盖层需要覆盖**虚拟桌面**（多显示器并集），按下述直觉写法取几何，Debug 构建报错：

```text
F:\develop\desktoppet\src\view\ui\ScreenColorPickerOverlay.cpp(87,34): error C2039:
  "virtualGeometry": 不是 "QGuiApplication" 的成员 [F:\develop\desktoppet\build\whalepet_view.vcxproj]
F:\develop\desktoppet\src\view\ui\ScreenColorPickerOverlay.cpp(87,34): error C3861:
  “virtualGeometry”: 找不到标识符
```

复现步骤：`cmake --build build --config Debug --parallel`（覆盖层首次进构建时即命中）。

## 二、根因

`virtualGeometry()` 是 **`QScreen` 的成员**（「所有屏幕的并集」语义挂在**屏幕**上），
`QGuiApplication` 只有 `screens()` / `primaryScreen()` / `screenAt()` 等，**没有**该静态方法。
写成 `QGuiApplication::virtualGeometry()` 是把「应用级几何」的直觉套到了类型上。

## 三、解决或规避

改为经主屏取并集，并为「无屏幕」的极端情形给兜底矩形（覆盖层不允许因此拿不到几何）：

```cpp
// 覆盖**虚拟桌面**（所有屏幕的并集）：多显示器下光标跑到哪块屏都能取色
const QScreen *primary = QGuiApplication::primaryScreen();
setGeometry(primary != nullptr ? primary->virtualGeometry() : QRect(0, 0, 640, 480));
```

修后 Debug 构建退出码 0；Debug / Release CTest 各 **37/37**。
（单测路径走 offscreen 平台，`primaryScreen()->virtualGeometry()` 同样可用，故无需条件编译。）

**规避口径**：Qt 里「并集 / 虚拟桌面」一类语义多在 `QScreen` 上，`QGuiApplication` 只提供
全局枚举与首选屏；写覆盖层 / 多屏几何前先确认方法挂在哪个类上，别按命名直觉写。

## 四、影响与关联文档

- 影响面：仅编译期；未产生错误产物（构建失败即停，未进入运行 / 打包环节）。
- 关联：`docs/DRAWING.md` §5.2（覆盖层几何与坐标口径）、
  `src/platform/Win32ScreenColorPicker.cpp`（同一功能在 Win32 侧的虚拟桌面矩形取自
  `GetSystemMetrics(SM_*VIRTUALSCREEN)`，与这里的 Qt 侧并集互为对照）。
