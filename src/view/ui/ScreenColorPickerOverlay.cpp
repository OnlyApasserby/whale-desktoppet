#include "view/ui/ScreenColorPickerOverlay.h"

#include "common/UiPalette.h"
#include "view/ui/DrawingColors.h"

#include <QClipboard>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QTimer>

#include <algorithm>

namespace whalepet::ui {

namespace {

constexpr int kPollIntervalMs = 33;      // ≈30fps：光标读数与放大镜跟随的节奏
constexpr int kGapRadius = 8;            // 十字线 / 取色框在光标周围留出的空隙（逻辑像素）
constexpr int kMagnifierSizePx = 180;    // 系统放大镜边长（物理像素）
constexpr float kMagnifierFactor = 6.0f; // 放大倍率
constexpr int kFallbackMagnifierSize = 150; // 自绘放大镜边长（逻辑像素）
constexpr int kFallbackSourcePx = 30;    // 自绘放大镜的源区域（物理像素）
constexpr int kHudGap = 22;              // 读数块 / 放大镜相对光标的偏移（逻辑像素）

// 画一条「黑底白芯」的线：任何屏幕底色上都看得见（颜色全部取自调色板）
void drawVisibleLine(QPainter &painter, const QLine &line)
{
    painter.setPen(QPen(background(255), 3.0));
    painter.drawLine(line);
    painter.setPen(QPen(foreground(255), 1.0));
    painter.drawLine(line);
}

// 画一个「黑底白芯」的矩形边框
void drawVisibleRect(QPainter &painter, const QRect &rect)
{
    painter.setPen(QPen(background(255), 3.0));
    painter.drawRect(rect);
    painter.setPen(QPen(foreground(255), 1.0));
    painter.drawRect(rect);
}

} // namespace

ScreenColorPickerOverlay::ScreenColorPickerOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false); // 取色需要键盘（ESC / 回车）
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("WhalePet 屏幕取色"));

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ScreenColorPickerOverlay::tick);
}

ScreenColorPickerOverlay::~ScreenColorPickerOverlay()
{
    m_magnifier.destroy();
}

void ScreenColorPickerOverlay::setCursorSource(CursorSource source)
{
    m_source = std::move(source);
}

void ScreenColorPickerOverlay::start()
{
    if (m_picking) {
        return;
    }
    m_picking = true;
    m_colorValid = false;
    m_status.clear();
    m_fallbackPatch = platform::ScreenPatch();

    // 覆盖**虚拟桌面**（所有屏幕的并集）：多显示器下光标跑到哪块屏都能取色
    const QScreen *primary = QGuiApplication::primaryScreen();
    setGeometry(primary != nullptr ? primary->virtualGeometry() : QRect(0, 0, 640, 480));

    // 系统放大镜（真实屏幕路径）。注入替身时（单测）不创建任何原生窗口、不访问真实屏幕。
    m_usingSystemMagnifier = false;
    if (!m_source.sampler && platform::MagnifierWindow::available()) {
        std::string error;
        if (m_magnifier.create(kMagnifierSizePx, kMagnifierFactor, &error)) {
            m_usingSystemMagnifier = true;
        } else {
            qInfo() << "[ScreenColorPicker] 系统放大镜不可用，降级为自绘放大镜："
                    << QString::fromStdString(error);
        }
    } else if (!m_source.sampler) {
        qInfo() << "[ScreenColorPicker] 系统放大镜不可用（Magnification API 探测失败），"
                   "降级为自绘放大镜";
    }

    show();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    m_timer->start();
    applyCursor();
}

void ScreenColorPickerOverlay::cancel()
{
    if (!m_picking) {
        return;
    }
    stopPicking();
    emit pickCanceled();
}

void ScreenColorPickerOverlay::stopPicking()
{
    m_picking = false;
    m_timer->stop();
    m_magnifier.destroy();
    m_usingSystemMagnifier = false;
    m_fallbackPatch = platform::ScreenPatch();
    hide();
}

void ScreenColorPickerOverlay::confirm()
{
    if (!m_picking) {
        return;
    }
    if (!m_colorValid) {
        m_status = QStringLiteral("还没取到有效像素：把鼠标移到想取的位置");
        update();
        return;
    }
    const core::RgbColor picked = m_current;
    // 取色器惯例：顺手把十六进制写进剪贴板（不改变取色结果本身）
    QGuiApplication::clipboard()->setText(QString::fromStdString(core::toHexString(picked)));
    stopPicking();
    emit colorPicked(toQColor(picked));
}

void ScreenColorPickerOverlay::tick()
{
    applyCursor();
}

void ScreenColorPickerOverlay::applyCursor()
{
    int physicalX = 0;
    int physicalY = 0;
    const bool physicalOk = m_source.physicalCursor ? m_source.physicalCursor(&physicalX, &physicalY)
                                                    : platform::physicalCursorPos(&physicalX, &physicalY);
    const QPoint logical = m_source.logicalCursor ? m_source.logicalCursor() : QCursor::pos();

    m_logicalCursor = logical;
    if (physicalOk) {
        m_physicalX = physicalX;
        m_physicalY = physicalY;
    }

    if (physicalOk) {
        const platform::ScreenPixel pixel =
            m_source.sampler ? m_source.sampler(m_physicalX, m_physicalY)
                             : platform::sampleScreenPixel(m_physicalX, m_physicalY);
        if (pixel.valid) {
            core::RgbColor color;
            color.r = pixel.r;
            color.g = pixel.g;
            color.b = pixel.b;
            color.a = 255;
            m_current = core::clampedColor(color);
            m_colorValid = true;
            m_status.clear();
        } else {
            m_colorValid = false;
            m_status = QStringLiteral("取不到像素：") + QString::fromStdString(pixel.error);
        }
    } else {
        m_colorValid = false;
        m_status = QStringLiteral("读不到光标位置");
    }

    if (m_usingSystemMagnifier) {
        m_magnifier.followCursor(); // 内部保证不覆盖光标点
    } else if (!m_source.sampler) {
        captureFallbackPatch(); // 降级路径才需要自抓区域；注入替身（单测）不做任何取屏
    }

    update();
}

void ScreenColorPickerOverlay::captureFallbackPatch()
{
    int desktopX = 0;
    int desktopY = 0;
    int desktopW = 0;
    int desktopH = 0;
    if (!platform::virtualDesktopBounds(&desktopX, &desktopY, &desktopW, &desktopH)) {
        m_fallbackPatch = platform::ScreenPatch();
        return;
    }

    const int half = kFallbackSourcePx / 2;
    int x = m_physicalX - half;
    int y = m_physicalY - half;
    x = std::max(desktopX, std::min(desktopX + desktopW - kFallbackSourcePx, x));
    y = std::max(desktopY, std::min(desktopY + desktopH - kFallbackSourcePx, y));
    if (desktopW < kFallbackSourcePx || desktopH < kFallbackSourcePx) {
        m_fallbackPatch = platform::ScreenPatch();
        return;
    }
    m_fallbackPatch = platform::captureScreenPatch(x, y, kFallbackSourcePx, kFallbackSourcePx);
}

void ScreenColorPickerOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        confirm();
        return;
    }
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        cancel();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ScreenColorPickerOverlay::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        cancel();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        confirm();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ScreenColorPickerOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // 极淡底（alpha = 1/255）：保证整屏都能收到鼠标事件（半透明窗口按逐像素 alpha 命中），
    // 对取色值的偏差 ≤1 个色阶（见头文件与 docs/DRAWING.md §5.3）。
    painter.fillRect(rect(), background(1));

    if (!m_picking) {
        return;
    }

    const QPoint cursorLocal = mapFromGlobal(m_logicalCursor);
    drawChrome(painter, cursorLocal);
    if (!m_usingSystemMagnifier) {
        drawFallbackMagnifier(painter, cursorLocal);
    }
    drawReadout(painter, cursorLocal);

    // 底部操作提示（含降级说明，可观测）
    const QString hint = m_usingSystemMagnifier
                             ? QStringLiteral("左键取色 · 右键 / ESC 取消")
                             : QStringLiteral("左键取色 · 右键 / ESC 取消 ｜ 放大镜已降级"
                                              "（系统 Magnification API 不可用，改为自绘放大）");
    QFont hintFont = painter.font();
    hintFont.setPointSizeF(9.0);
    painter.setFont(hintFont);
    const QFontMetrics metrics(painter.font());
    const QRect hintRect(8, height() - metrics.height() - 12, metrics.horizontalAdvance(hint) + 16,
                         metrics.height() + 4);
    painter.fillRect(hintRect, background(220));
    painter.setPen(foreground(255));
    painter.drawText(hintRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, hint);
}

void ScreenColorPickerOverlay::drawChrome(QPainter &painter, const QPoint &cursorLocal) const
{
    const int cx = cursorLocal.x();
    const int cy = cursorLocal.y();

    // 十字线：四条射线在光标周围留出 kGapRadius 的空隙 —— 空隙不是美观问题，而是**采样正确性**：
    // 覆盖层自身像素会被 1×1 BitBlt 一起采到，盖住光标点就会取到自己的线色。
    drawVisibleLine(painter, QLine(0, cy, cx - kGapRadius, cy));
    drawVisibleLine(painter, QLine(cx + kGapRadius, cy, width(), cy));
    drawVisibleLine(painter, QLine(cx, 0, cx, cy - kGapRadius));
    drawVisibleLine(painter, QLine(cx, cy + kGapRadius, cx, height()));

    // 取色框（空心方框，中心即被采样的那一个像素）
    drawVisibleRect(painter, QRect(cx - kGapRadius, cy - kGapRadius, kGapRadius * 2 + 1,
                                   kGapRadius * 2 + 1));
}

void ScreenColorPickerOverlay::drawFallbackMagnifier(QPainter &painter, const QPoint &cursorLocal) const
{
    if (!m_fallbackPatch.valid || m_fallbackPatch.width <= 0 || m_fallbackPatch.height <= 0) {
        return;
    }

    QRect target(cursorLocal.x() + kHudGap, cursorLocal.y() + kHudGap, kFallbackMagnifierSize,
                 kFallbackMagnifierSize);
    if (target.right() > width()) {
        target.moveLeft(cursorLocal.x() - kHudGap - target.width());
    }
    if (target.bottom() > height()) {
        target.moveTop(cursorLocal.y() - kHudGap - target.height());
    }
    if (target.contains(cursorLocal)) {
        return; // 摆不下就不画：宁可没有放大镜，也不能盖住光标点污染取色
    }

    const QImage patch(m_fallbackPatch.rgba.data(), m_fallbackPatch.width, m_fallbackPatch.height,
                       m_fallbackPatch.width * 4, QImage::Format_RGBA8888);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // 保留像素块，便于逐像素判色
    painter.drawImage(target, patch);

    // 中心像素的标记（被采样的那一格）
    const int cell = std::max(1, target.width() / m_fallbackPatch.width);
    const QRect centerCell(target.center().x() - cell / 2, target.center().y() - cell / 2, cell, cell);
    drawVisibleRect(painter, centerCell.adjusted(-1, -1, 0, 0));
    painter.setPen(QPen(background(255), 1.0));
    painter.drawRect(target.adjusted(-1, -1, 0, 0));
}

void ScreenColorPickerOverlay::drawReadout(QPainter &painter, const QPoint &cursorLocal) const
{
    QFont font = painter.font();
    font.setPointSizeF(9.0);
    painter.setFont(font);
    const QFontMetrics metrics(painter.font());

    const QString hex = m_colorValid ? QString::fromStdString(core::toHexString(m_current))
                                     : QStringLiteral("-----------");
    const QString body = m_status.isEmpty() ? hex
                                            : (hex + QStringLiteral("  (") + m_status + QLatin1Char(')'));
    const QString coordinate =
        QStringLiteral("x %1 · y %2").arg(m_physicalX).arg(m_physicalY);

    const int padX = 8;
    const int padY = 4;
    const int swatch = 26;
    const int textWidth = std::max(metrics.horizontalAdvance(body), metrics.horizontalAdvance(coordinate));
    const QSize hud(padX * 2 + swatch + 8 + textWidth, padY * 2 + metrics.height() * 2 + 2);

    // 放大镜占位：读数块摆在它下方（系统放大镜默认在光标右下；靠边会翻到左上，此时读数块仍在下方空白处）
    qreal dpr = 1.0;
    for (const QScreen *screen : QGuiApplication::screens()) {
        if (screen->geometry().contains(m_logicalCursor)) {
            dpr = screen->devicePixelRatio();
            break;
        }
    }
    const int magnifierLogical = m_usingSystemMagnifier
                                     ? qRound(static_cast<qreal>(kMagnifierSizePx) / std::max(1.0, dpr))
                                     : kFallbackMagnifierSize;

    QPoint pos(cursorLocal.x() + kHudGap, cursorLocal.y() + kHudGap + magnifierLogical + 6);
    if (pos.y() + hud.height() > height()) {
        pos.setY(cursorLocal.y() - 6 - hud.height());
    }
    if (pos.x() + hud.width() > width()) {
        pos.setX(cursorLocal.x() - kHudGap - hud.width());
    }
    pos.setX(std::max(4, pos.x()));
    pos.setY(std::max(4, std::min(height() - hud.height() - 4, pos.y())));

    QRect hudRect(pos, hud);
    if (hudRect.contains(cursorLocal)) {
        hudRect.moveTo(std::max(4, cursorLocal.x() - kHudGap - hud.width()), 4);
    }
    if (hudRect.contains(cursorLocal)) {
        return; // 极小屏幕：宁可不显示读数，也不覆盖被采样的像素
    }

    painter.fillRect(hudRect, background(220));
    drawVisibleRect(painter, hudRect);

    // 颜色预览方块（内容 = 取到的颜色本身）
    const QRect swatchRect(hudRect.left() + padX, hudRect.top() + padY, swatch, swatch);
    painter.fillRect(swatchRect, m_colorValid ? toQColor(m_current) : background(255));
    painter.setPen(QPen(foreground(255), 1.0));
    painter.drawRect(swatchRect);

    painter.setPen(foreground(255));
    const QRect textRect(swatchRect.right() + 8, hudRect.top() + padY,
                         hudRect.width() - padX * 2 - swatch - 8, metrics.height() * 2 + 2);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, body);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, coordinate);
}

} // namespace whalepet::ui
