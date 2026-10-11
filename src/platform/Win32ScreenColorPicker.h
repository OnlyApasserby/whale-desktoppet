#pragma once

// P11 绘画模块：**屏幕取色的 Win32 实现**（架构口径见 docs/DRAWING.md §5）。
//
// 路线来源：P11 开工时只读核对过一个 Windows 工具集项目（WinEase）的取色 / 放大镜底层件 ——
// **该参考拷贝已按用户指示于 2026-10-11 从 references/ 移除**（属用户本人的个人项目，
// 见 docs/DRAWING.md §5.1）；其中**没有取色器本体**，故本文件为独立实现（未复制其源码）：
//   * 单点取色走 1×1 BitBlt —— 该项目的实测结论是 `GetPixel` 在 DWM 合成 / 分层窗口下
//     会返回过期或错误颜色，故刻意不用；
//   * 放大镜走**系统 Magnification API**（宿主窗口 + WC_MAGNIFIER 子窗口 + 排除自身 +
//     跟随光标 + 点击穿透），从而与系统放大镜观感一致。
//
// 依赖：Windows 系统库 user32 / gdi32 / magnification（**非第三方依赖**，随 Windows SDK 提供；
// 该口径已由用户在 2026-10-11 明确同意，见 docs/DRAWING.md §5.1）。
// 优雅降级：Magnification API 不可用（旧系统 / 远程会话）时 MagnifierWindow::available()
// 返回 false，界面改用「区域抓取 + 自绘放大」并在日志中写明原因 —— 不伪造、不静默。

#include <string>
#include <vector>

namespace whalepet::platform {

// 屏幕像素（**物理像素**坐标；sRGB 8 位三通道）
struct ScreenPixel {
    bool valid = false;
    int r = 0;
    int g = 0;
    int b = 0;
    std::string error; // valid=false 时的原因（可观测）
};

// 屏幕区域抓取结果（32bpp RGBA，行优先，长度 = width * height * 4）
struct ScreenPatch {
    bool valid = false;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    std::string error;
};

// 取屏幕单点颜色（物理坐标；越界或取不到时 valid=false 并带 error）
ScreenPixel sampleScreenPixel(int physicalX, int physicalY);

// 取屏幕区域（物理坐标 + 宽高；区域必须完整落在虚拟桌面内）
ScreenPatch captureScreenPatch(int physicalX, int physicalY, int width, int height);

// 当前物理光标位置（GetCursorPos）；失败返回 false
bool physicalCursorPos(int *x, int *y);

// 虚拟桌面矩形（物理像素；多显示器时原点可为负）——抓取前用它夹取区域
bool virtualDesktopBounds(int *x, int *y, int *width, int *height);

// 点是否在矩形内（放大镜摆位用；避免覆盖光标点污染取色）
bool rectContainsPoint(int rectX, int rectY, int rectWidth, int rectHeight, int x, int y);

// ---------------------------------------------------------------------------
// 系统放大镜（Magnification API）
// 生命周期：available() 探测 → create() → followCursor() 循环 → destroy()（成对 MagInitialize）
// ---------------------------------------------------------------------------
class MagnifierWindow {
public:
    MagnifierWindow();
    ~MagnifierWindow();

    MagnifierWindow(const MagnifierWindow &) = delete;
    MagnifierWindow &operator=(const MagnifierWindow &) = delete;

    // 系统是否支持 Magnification API（试开一次 MagInitialize / MagUninitialize）
    static bool available();

    // 创建放大镜：sizePx = 放大镜边长（物理像素），factor = 放大倍率。
    // 失败返回 false（error 非空，说明原因），调用方降级为自绘放大镜。
    bool create(int sizePx, float factor, std::string *error);
    void destroy();
    bool created() const { return m_created; }

    // 跟随光标：按物理光标摆位（靠边翻到另一侧），并**保证不覆盖光标点**
    void followCursor();

    // 已创建窗口的位置（物理像素；未创建返回 false）
    bool windowRect(int *x, int *y, int *width, int *height) const;

private:
    bool m_created = false;
    int m_sizePx = 0;
    float m_factor = 1.0f;
    void *m_host = nullptr;  // HWND：宿主窗口（无边框工具窗，点击穿透）
    void *m_child = nullptr; // HWND：WC_MAGNIFIER 子窗口
    int m_lastX = 0;
    int m_lastY = 0;
};

} // namespace whalepet::platform
