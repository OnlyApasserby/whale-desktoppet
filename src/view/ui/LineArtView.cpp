#include "view/ui/LineArtView.h"

#include "common/UiPalette.h"
#include "view/ui/DrawingColors.h"
#include "view/ui/DrawingShapes.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

namespace whalepet::ui {

namespace {

constexpr int kSeedMax = 1000000000;
constexpr int kStrokeWidth = 2;

// 把一张线条画落到 QPainter：**全部为线框**（NoBrush），图形种类按 core 的图元枚举映射
void paintPlan(QPainter &painter, const core::LineArtPlan &plan, const QSize &size)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(QRect(QPoint(0, 0), size), background(255));
    painter.setBrush(Qt::NoBrush);

    for (const core::LineArtShape &shape : plan.shapes) {
        QPen pen(toQColor(shape.accent ? plan.style.accent : plan.style.outline));
        pen.setWidthF(std::max(1.0, static_cast<double>(std::max(1, plan.style.strokeWidth))));
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        // 归一化几何 → 画布像素；每个形状在自己的中心坐标系里绘制（便于旋转）
        const double width = static_cast<double>(size.width()) * static_cast<double>(shape.w);
        const double height = static_cast<double>(size.height()) * static_cast<double>(shape.h);
        const QRectF local(-width * 0.5, -height * 0.5, width, height);

        painter.save();
        painter.translate(static_cast<double>(size.width()) * static_cast<double>(shape.x),
                          static_cast<double>(size.height()) * static_cast<double>(shape.y));
        painter.rotate(static_cast<double>(shape.rotation));

        switch (shape.primitive) {
        case core::LineArtPrimitive::Circle:
            painter.drawEllipse(local);
            break;
        case core::LineArtPrimitive::Rectangle:
            painter.drawRect(local);
            break;
        case core::LineArtPrimitive::Triangle:
            painter.drawPolygon(trianglePolygon(local));
            break;
        case core::LineArtPrimitive::Polygon:
            painter.drawPolygon(regularPolygon(local, shape.sides));
            break;
        case core::LineArtPrimitive::Line:
            painter.drawLine(QPointF(local.left(), 0.0), QPointF(local.right(), 0.0));
            break;
        case core::LineArtPrimitive::Count:
            break;
        }

        painter.restore();
    }
    painter.setPen(QPen(foreground(128), 1.0));
    painter.drawRect(QRect(QPoint(0, 0), size).adjusted(0, 0, -1, -1));
}

} // namespace

// ---------------------------------------------------------------------------
// 画布
// ---------------------------------------------------------------------------
LineArtCanvas::LineArtCanvas(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(kCanvasWidth, kCanvasHeight);
    regenerate();
}

core::LineArtStyle LineArtCanvas::makeStyle() const
{
    // 颜色由**界面**注入（core 不硬编码颜色）：主体白、次要灰，线宽统一
    core::LineArtStyle style;
    style.outline = fromQColor(foreground(255));
    style.accent = fromQColor(muted(255));
    style.strokeWidth = kStrokeWidth;
    return style;
}

void LineArtCanvas::regenerate()
{
    const quint32 seed = static_cast<quint32>(m_seedSource.nextInt(kSeedMax)) + 1u;
    core::SystemRandom rng(seed); // 种子固定 → 抖动可复现（种子显示在界面上）
    m_plan = core::makeLineArt(rng, makeStyle(), seed);
    ++m_generation;
    update();
    emit regenerated();
}

void LineArtCanvas::setPlan(const core::LineArtPlan &plan)
{
    m_plan = plan;
    update();
    emit regenerated();
}

void LineArtCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    paintPlan(painter, m_plan, size());
}

// ---------------------------------------------------------------------------
// 页面
// ---------------------------------------------------------------------------
LineArtView::LineArtView(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_canvas = new LineArtCanvas(this);
    root->addWidget(m_canvas, 0, Qt::AlignCenter);

    auto *row = new QHBoxLayout();
    auto *regenerate = new QPushButton(QStringLiteral("换一只"), this);
    auto *save = new QPushButton(QStringLiteral("保存 PNG"), this);
    row->addWidget(regenerate);
    row->addWidget(save);
    row->addStretch();
    root->addLayout(row);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(regenerate, &QPushButton::clicked, this, [this] {
        m_canvas->regenerate();
        refreshStatus();
    });
    connect(save, &QPushButton::clicked, this, &LineArtView::onSave);
    connect(m_canvas, &LineArtCanvas::regenerated, this, &LineArtView::refreshStatus);

    refreshStatus();
}

void LineArtView::onSave()
{
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("whalepet-lineart-%1.png").arg(m_canvas->seed()));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存线条画"), suggested,
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;
    }

    QImage image(LineArtCanvas::kCanvasWidth, LineArtCanvas::kCanvasHeight,
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(background(255));
    QPainter painter(&image);
    paintPlan(painter, m_canvas->plan(), image.size());
    painter.end();

    if (!image.save(path, "PNG")) {
        m_status->setText(QStringLiteral("保存失败：") + path);
        return;
    }
    m_status->setText(QStringLiteral("已保存：") + path);
}

void LineArtView::refreshStatus()
{
    m_status->setText(QStringLiteral("图形 %1 个（圆 / 矩形 / 三角 / 多边形 / 直线线框）· 种子 %2")
                          .arg(m_canvas->plan().shapes.size())
                          .arg(m_canvas->seed()));
}

} // namespace whalepet::ui
