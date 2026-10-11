#pragma once

// P11 绘画模块：**屏幕取色覆盖层**（架构与取舍见 docs/DRAWING.md §5）。
//
// 交互：进入取色 → 全屏覆盖层（覆盖虚拟桌面的所有屏幕）→ 鼠标移动实时更新
//   十字线 / 取色框 / 十六进制读数 / 放大镜；**左键单击 = 取色**，**右键 / ESC = 取消**。
//
// 采样：像素由 platform::Win32ScreenColorPicker 提供（1×1 BitBlt，物理坐标）。
// 因此覆盖层自身**绝不能盖住光标点**：十字线与取色框在光标周围留出空隙、读数块与放大镜
// 一律摆在光标之外 —— 否则采到的是自己的像素（这是「覆盖层 + 取屏」组合最容易踩的坑）。
//
// 命中测试：Qt 半透明窗口在 Windows 上按逐像素 alpha 命中，alpha=0 的区域收不到鼠标事件；
// 故整屏铺一层 alpha=1/255 的极淡底（肉眼不可见），既保证「整屏可点」，对取色值的偏差
// 又 ≤1 个色阶（不做反向补偿：补偿的取整误差反而更大，见 §5.3）。
//
// 降级：Magnification API 不可用（旧系统 / 远程会话）→ 改用「区域抓取 + 自绘放大镜」，
// 并在覆盖层上写明降级原因（可观测、不静默）。
//
// 单测：`setCursorSource()` 可注入光标与像素读取，注入后**不创建任何原生窗口、不触碰真实屏幕**。

#include "core/DrawingPalette.h"
#include "platform/Win32ScreenColorPicker.h"

#include <QPoint>
#include <QString>
#include <QWidget>

#include <functional>

class QColor;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QTimer;

namespace whalepet::ui {

class ScreenColorPickerOverlay : public QWidget {
    Q_OBJECT
public:
    // 单测注入：三个读取口都可为空（空 = 走真实 Win32 / Qt 实现）
    struct CursorSource {
        std::function<bool(int *, int *)> physicalCursor;       // 物理光标位置
        std::function<QPoint()> logicalCursor;                  // 逻辑光标位置（Qt 坐标系）
        std::function<platform::ScreenPixel(int, int)> sampler; // 像素读取（物理坐标）
    };

    explicit ScreenColorPickerOverlay(QWidget *parent = nullptr);
    ~ScreenColorPickerOverlay() override;

    // 注入读取口（单测用；注入 sampler 后不再创建系统放大镜、不访问真实屏幕）
    void setCursorSource(CursorSource source);

    void start();  // 开始取色（幂等）
    void cancel(); // 取消取色（不发 colorPicked）

    bool picking() const { return m_picking; }
    core::RgbColor currentColor() const { return m_current; }
    bool colorValid() const { return m_colorValid; }
    bool usingSystemMagnifier() const { return m_usingSystemMagnifier; }
    QString statusText() const { return m_status; }

signals:
    void colorPicked(const QColor &color); // 确认取色（同时把 #RRGGBB 写入剪贴板）
    void pickCanceled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void tick();          // 定时轮询（33ms）
    void applyCursor();   // 读光标 → 采样 → 放大镜跟随 → 重绘
    void refreshSample(); // 采样光标像素（失败保持上一有效值并写状态）
    void stopPicking();   // 停止定时器 + 销毁放大镜 + 隐藏
    void confirm();       // 确认取色
    void captureFallbackPatch();
    void drawChrome(QPainter &painter, const QPoint &cursorLocal) const;
    void drawFallbackMagnifier(QPainter &painter, const QPoint &cursorLocal) const;
    void drawReadout(QPainter &painter, const QPoint &cursorLocal) const;

    CursorSource m_source;
    QTimer *m_timer = nullptr;
    platform::MagnifierWindow m_magnifier;
    platform::ScreenPatch m_fallbackPatch; // 自绘放大镜的源（30×30 物理像素）

    bool m_picking = false;
    bool m_usingSystemMagnifier = false;
    bool m_colorValid = false;
    core::RgbColor m_current;
    QString m_status;

    QPoint m_logicalCursor; // 逻辑坐标（Qt 全局坐标系）
    int m_physicalX = 0;
    int m_physicalY = 0;
};

} // namespace whalepet::ui
