#include "view/ui/DrawingBoardView.h"

#include "common/UiPalette.h"
#include "view/ui/ColorSwatch.h"
#include "view/ui/DrawingColors.h"
#include "view/ui/ScreenColorPickerOverlay.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace whalepet::ui {

namespace {

// 笔刷粗细预设（取值落在 core 的 [1, 64] 钳制区间内）
const int kWidthPresets[] = { 1, 2, 4, 8, 16, 32 };

} // namespace

// ---------------------------------------------------------------------------
// 画布
// ---------------------------------------------------------------------------
DrawingBoardCanvas::DrawingBoardCanvas(QWidget *parent)
    : QWidget(parent)
    , m_model(kCanvasWidth, kCanvasHeight)
{
    setFixedSize(kCanvasWidth, kCanvasHeight);
    setCursor(Qt::CrossCursor);

    m_brushColor = fromQColor(background(255)); // 默认黑笔（调色板黑；白底上看得见）
    m_image = QImage(kCanvasWidth, kCanvasHeight, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(foreground(255)); // 纯白底（用户口径；取值 = 调色板白）
}

void DrawingBoardCanvas::setBrushColor(const core::RgbColor &color)
{
    m_brushColor = core::clampedColor(color);
}

void DrawingBoardCanvas::setBrushWidth(int width)
{
    m_brushWidth = std::max(core::kCanvasMinBrushWidth, std::min(core::kCanvasMaxBrushWidth, width));
}

void DrawingBoardCanvas::setEraser(bool eraser)
{
    m_eraser = eraser;
}

QColor DrawingBoardCanvas::strokeColor() const
{
    // 橡皮 = 用调色板白覆盖（画布本来就是白底，因此不需要透明通道，画布始终不透明）
    return m_eraser ? foreground(255) : toQColor(m_brushColor);
}

core::CanvasPoint DrawingBoardCanvas::toCanvasPoint(const QPointF &position) const
{
    core::CanvasPoint point;
    point.x = static_cast<float>(position.x());
    point.y = static_cast<float>(position.y());
    return point;
}

void DrawingBoardCanvas::paintSegment(const core::CanvasPoint &from, const core::CanvasPoint &to)
{
    const core::CanvasStroke &active = m_model.activeStroke();

    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(strokeColor());
    pen.setWidthF(std::max(1.0, static_cast<double>(active.width)));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    if (from.x == to.x && from.y == to.y) {
        painter.drawPoint(QPointF(from.x, from.y)); // 单点落笔（点一下就有一个圆点）
    } else {
        painter.drawLine(QPointF(from.x, from.y), QPointF(to.x, to.y));
    }
    painter.end();

    // 只重绘这一小段（增量刷新；画布越大越省）
    const double radius = static_cast<double>(active.width) * 0.5 + 2.0;
    const int left = static_cast<int>(std::floor(std::min(from.x, to.x) - radius));
    const int top = static_cast<int>(std::floor(std::min(from.y, to.y) - radius));
    const int right = static_cast<int>(std::ceil(std::max(from.x, to.x) + radius));
    const int bottom = static_cast<int>(std::ceil(std::max(from.y, to.y) + radius));
    update(QRect(QPoint(left, top), QPoint(right, bottom)));
}

void DrawingBoardCanvas::renderAll()
{
    m_image.fill(foreground(255));

    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const core::CanvasStroke &stroke : m_model.strokes()) {
        QPen pen(stroke.eraser ? foreground(255) : toQColor(stroke.color));
        pen.setWidthF(std::max(1.0, static_cast<double>(stroke.width)));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        if (stroke.points.size() == 1) {
            painter.drawPoint(QPointF(stroke.points.front().x, stroke.points.front().y));
            continue;
        }
        QPolygonF polyline;
        polyline.reserve(static_cast<int>(stroke.points.size()));
        for (const core::CanvasPoint &point : stroke.points) {
            polyline << QPointF(point.x, point.y);
        }
        painter.drawPolyline(polyline);
    }
    update();
}

bool DrawingBoardCanvas::undo()
{
    if (!m_model.undo()) {
        return false;
    }
    renderAll();
    emit changed();
    return true;
}

void DrawingBoardCanvas::clearBoard()
{
    m_model.clear();
    renderAll();
    emit changed();
}

void DrawingBoardCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.drawImage(0, 0, m_image);
}

void DrawingBoardCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_model.begin(toCanvasPoint(event->position()), m_brushColor, m_brushWidth, m_eraser);
    const core::CanvasPoint &start = m_model.activeStroke().points.back();
    paintSegment(start, start);
    emit changed();
}

void DrawingBoardCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_model.drawing()) {
        return;
    }
    const core::CanvasPoint previous = m_model.activeStroke().points.back();
    const std::size_t before = m_model.activeStroke().points.size();
    m_model.extend(toCanvasPoint(event->position()));
    if (m_model.activeStroke().points.size() == before) {
        return; // 同一像素内移动：不重复落笔
    }
    paintSegment(previous, m_model.activeStroke().points.back());
    emit changed();
}

void DrawingBoardCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_model.drawing()) {
        m_model.end();
        emit changed();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// 页面
// ---------------------------------------------------------------------------
DrawingBoardView::DrawingBoardView(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *toolRow = new QHBoxLayout();
    toolRow->addWidget(new QLabel(QStringLiteral("工具"), this));
    m_tool = new QComboBox(this);
    m_tool->addItem(QStringLiteral("画笔"));
    m_tool->addItem(QStringLiteral("橡皮"));
    toolRow->addWidget(m_tool);

    toolRow->addWidget(new QLabel(QStringLiteral("粗细"), this));
    m_width = new QComboBox(this);
    for (const int width : kWidthPresets) {
        m_width->addItem(QStringLiteral("%1 px").arg(width), width);
    }
    m_width->setCurrentIndex(1); // 默认 2px
    toolRow->addWidget(m_width);

    toolRow->addWidget(new QLabel(QStringLiteral("当前色"), this));
    m_current = new ColorSwatch(this);
    toolRow->addWidget(m_current);

    m_pick = new QPushButton(QStringLiteral("屏幕取色"), this);
    toolRow->addWidget(m_pick);
    toolRow->addStretch();
    root->addLayout(toolRow);

    auto *recentRow = new QHBoxLayout();
    recentRow->addWidget(new QLabel(QStringLiteral("最近取色"), this));
    for (int i = 0; i < kRecentColorCount; ++i) {
        auto *swatch = new ColorSwatch(this);
        swatch->setTextVisible(false);
        swatch->setMinimumSize(26, 26);
        swatch->setMaximumWidth(30);
        swatch->setVisible(false);
        const int index = i;
        connect(swatch, &ColorSwatch::clicked, this, [this, index] {
            if (index < m_recentColors.size()) {
                setCurrentColor(m_recentColors.at(index));
            }
        });
        m_recentSwatches.append(swatch);
        recentRow->addWidget(swatch);
    }
    recentRow->addStretch();
    root->addLayout(recentRow);

    m_canvas = new DrawingBoardCanvas(this);
    root->addWidget(m_canvas, 0, Qt::AlignCenter);

    auto *actionRow = new QHBoxLayout();
    auto *undo = new QPushButton(QStringLiteral("撤销"), this);
    auto *clear = new QPushButton(QStringLiteral("清空"), this);
    auto *save = new QPushButton(QStringLiteral("保存 PNG"), this);
    actionRow->addWidget(undo);
    actionRow->addWidget(clear);
    actionRow->addWidget(save);
    actionRow->addStretch();
    root->addLayout(actionRow);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(m_tool, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_canvas->setEraser(index == 1);
        refreshStatus();
    });
    connect(m_width, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_canvas->setBrushWidth(m_width->currentData().toInt());
        refreshStatus();
    });
    connect(undo, &QPushButton::clicked, this, [this] {
        if (!m_canvas->undo()) {
            m_status->setText(QStringLiteral("没有可撤销的笔画了"));
        }
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_canvas->clearBoard();
        m_status->setText(QStringLiteral("画布已清空（纯白底）"));
    });
    connect(save, &QPushButton::clicked, this, &DrawingBoardView::onSave);
    connect(m_pick, &QPushButton::clicked, this, &DrawingBoardView::onPickColor);
    connect(m_canvas, &DrawingBoardCanvas::changed, this, &DrawingBoardView::refreshStatus);

    m_canvas->setBrushWidth(m_width->currentData().toInt());
    refreshRecent();
    refreshStatus();
}

void DrawingBoardView::setCurrentColor(const core::RgbColor &color)
{
    m_canvas->setBrushColor(color);
    m_current->setColor(color);
    if (m_tool->currentIndex() != 0) {
        m_tool->setCurrentIndex(0); // 选了颜色就回到画笔（避免「以为在画，其实在擦」）
    }
    refreshStatus();
}

void DrawingBoardView::applyPickedColor(const core::RgbColor &color)
{
    const core::RgbColor safe = core::clampedColor(color);
    setCurrentColor(safe);

    m_recentColors.removeAll(safe);
    m_recentColors.prepend(safe);
    while (m_recentColors.size() > kRecentColorCount) {
        m_recentColors.removeLast();
    }
    refreshRecent();
}

void DrawingBoardView::onPickColor()
{
    if (m_picker == nullptr) {
        m_picker = new ScreenColorPickerOverlay(this);
        connect(m_picker, &ScreenColorPickerOverlay::colorPicked, this, [this](const QColor &color) {
            const core::RgbColor picked = fromQColor(color);
            applyPickedColor(picked);
            m_status->setText(QStringLiteral("取到了 %1（已设为画笔色，并复制到剪贴板）")
                                  .arg(QString::fromStdString(core::toHexString(picked))));
        });
        connect(m_picker, &ScreenColorPickerOverlay::pickCanceled, this, [this] {
            m_status->setText(QStringLiteral("已取消取色"));
        });
    }
    m_picker->start();
    m_status->setText(QStringLiteral("取色中：左键取色 · 右键 / ESC 取消"));
}

void DrawingBoardView::onSave()
{
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("whalepet-board-")
                      + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                      + QStringLiteral(".png"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存画板"), suggested,
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    if (!m_canvas->image().save(path, "PNG")) {
        m_status->setText(QStringLiteral("保存失败：") + path);
        return;
    }
    m_status->setText(QStringLiteral("已保存：") + path);
}

void DrawingBoardView::refreshRecent()
{
    for (int i = 0; i < m_recentSwatches.size(); ++i) {
        ColorSwatch *swatch = m_recentSwatches.at(i);
        const bool has = i < m_recentColors.size();
        swatch->setVisible(has);
        if (has) {
            swatch->setColor(m_recentColors.at(i));
        }
    }
}

void DrawingBoardView::refreshStatus()
{
    const QString tool = (m_tool->currentIndex() == 1) ? QStringLiteral("橡皮") : QStringLiteral("画笔");
    QString text = QStringLiteral("已画 %1 笔 · %2 %3 px · 当前色 %4")
                       .arg(m_canvas->model().strokes().size())
                       .arg(tool)
                       .arg(m_canvas->brushWidth())
                       .arg(QString::fromStdString(core::toHexString(m_canvas->brushColor())));
    if (m_status->text() == text) {
        return;
    }
    m_status->setText(text);
}

} // namespace whalepet::ui
