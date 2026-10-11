#include "view/ui/ColorSwatch.h"

#include "common/UiPalette.h"
#include "view/ui/DrawingColors.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

namespace whalepet::ui {

namespace {

// 色块上文字的黑 / 白选择阈值（感知亮度中点）：亮底用黑字、暗底用白字
constexpr double kDarkTextBelowLuminance = 0.55;

} // namespace

ColorSwatch::ColorSwatch(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(52, 26);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void ColorSwatch::setColor(const core::RgbColor &color)
{
    const core::RgbColor safe = core::clampedColor(color);
    if (safe == m_color) {
        return;
    }
    m_color = safe;
    update();
}

void ColorSwatch::setTextVisible(bool visible)
{
    if (m_textVisible == visible) {
        return;
    }
    m_textVisible = visible;
    update();
}

void ColorSwatch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRect box = rect().adjusted(0, 0, -1, -1);
    painter.fillRect(box, toQColor(m_color));
    painter.setPen(foreground(255));
    painter.drawRect(box);

    if (!m_textVisible) {
        return;
    }

    const QString hex = QString::fromStdString(core::toHexString(m_color));
    QFont font = painter.font();
    font.setPointSizeF(8.0);
    painter.setFont(font);
    // 任意底色都能读清：亮底黑字、暗底白字（取值都来自调色板）
    painter.setPen(core::perceivedLuminance(m_color) > kDarkTextBelowLuminance ? background(255)
                                                                              : foreground(255));
    painter.drawText(rect(), Qt::AlignCenter, hex);
}

void ColorSwatch::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

} // namespace whalepet::ui
