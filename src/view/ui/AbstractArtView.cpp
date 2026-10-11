#include "view/ui/AbstractArtView.h"

#include "common/UiPalette.h"
#include "view/ui/DrawingColors.h"
#include "view/ui/DrawingShapes.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace whalepet::ui {

namespace {

constexpr int kTickIntervalMs = 33; // ≈30fps
constexpr int kFadeAlpha = 10;      // 每帧淡出的白色覆盖透明度
constexpr int kFadeTicks = 26;      // 淡出持续帧数（≈0.9s）

// 把一笔 core 抽象画落到 QPainter（归一化几何 → 画布像素）
void paintAbstractStroke(QPainter &painter, const core::AbstractStroke &stroke, const QSize &size)
{
    const QColor color = toQColor(stroke.color);
    QPen pen(color);
    pen.setWidthF(std::max(1.0, static_cast<double>(stroke.lineWidth)));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    painter.setPen(stroke.filled ? Qt::NoPen : pen);
    painter.setBrush(stroke.filled ? QBrush(color) : Qt::NoBrush);

    const double width = static_cast<double>(size.width()) * static_cast<double>(stroke.w);
    const double height = static_cast<double>(size.height()) * static_cast<double>(stroke.h);
    const QRectF local(-width * 0.5, -height * 0.5, width, height);

    painter.save();
    painter.translate(static_cast<double>(size.width()) * static_cast<double>(stroke.x),
                      static_cast<double>(size.height()) * static_cast<double>(stroke.y));
    painter.rotate(static_cast<double>(stroke.rotation));

    switch (stroke.shape) {
    case core::AbstractShape::Circle:
        painter.drawEllipse(local);
        break;
    case core::AbstractShape::Rectangle:
        painter.drawRect(local);
        break;
    case core::AbstractShape::Triangle:
        painter.drawPolygon(trianglePolygon(local));
        break;
    case core::AbstractShape::Polygon:
        painter.drawPolygon(regularPolygon(local, stroke.sides));
        break;
    case core::AbstractShape::Line:
        painter.drawLine(QPointF(local.left(), 0.0), QPointF(local.right(), 0.0));
        break;
    case core::AbstractShape::Arc:
        painter.drawArc(local, 0, stroke.arcSpan * 16);
        break;
    case core::AbstractShape::Count:
        break;
    }

    painter.restore();
}

QString defaultSaveName(const QString &prefix)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
        .filePath(prefix + QStringLiteral("-")
                  + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                  + QStringLiteral(".png"));
}

} // namespace

// ---------------------------------------------------------------------------
// 画布
// ---------------------------------------------------------------------------
AbstractArtCanvas::AbstractArtCanvas(QWidget *parent)
    : QWidget(parent)
    , m_generator(kCanvasWidth, kCanvasHeight)
{
    setFixedSize(kCanvasWidth, kCanvasHeight);
    m_image = QImage(kCanvasWidth, kCanvasHeight, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(foreground(255)); // 白底（调色板白，不是新造的颜色）

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &AbstractArtCanvas::advance);
    m_timer->start(); // 打开页面即开始「实时画布」
}

void AbstractArtCanvas::setRunning(bool running)
{
    if (running) {
        m_timer->start();
    } else {
        m_timer->stop();
    }
}

bool AbstractArtCanvas::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

void AbstractArtCanvas::reseed(quint32 seed)
{
    m_rng = core::SystemRandom(seed);
    resetCanvas();
}

void AbstractArtCanvas::resetCanvas()
{
    m_image.fill(foreground(255));
    m_strokeCount = 0;
    m_fadeTicks = 0;
    m_fading = false;
    m_rng = core::SystemRandom(); // 重新随机演化（与上一张不再相关）
    update();
    emit advanced(m_strokeCount, m_fading);
}

void AbstractArtCanvas::advance()
{
    if (m_image.isNull()) {
        return;
    }

    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_fading) {
        // 画面满了：每帧盖一层极淡的白，逐渐回到白底（视觉上是「呼吸 / 重来」而不是硬清屏）
        painter.fillRect(m_image.rect(), foreground(kFadeAlpha));
        painter.end();
        ++m_fadeTicks;
        if (m_fadeTicks >= kFadeTicks) {
            resetCanvas();
            return;
        }
        update();
        emit advanced(m_strokeCount, true);
        return;
    }

    for (int i = 0; i < core::kAbstractBatchSize; ++i) {
        const core::AbstractStroke stroke = m_generator.next(m_rng);
        paintAbstractStroke(painter, stroke, m_image.size());
        ++m_strokeCount;
    }
    painter.end();

    if (m_strokeCount >= core::kAbstractStrokeCap) {
        m_fading = true;
        m_fadeTicks = 0;
    }
    update();
    emit advanced(m_strokeCount, m_fading);
}

void AbstractArtCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    // 只画累积位图（不重放笔触）：每帧成本与「画布大小」成正比，与笔触数无关
    painter.drawImage(0, 0, m_image);
}

// ---------------------------------------------------------------------------
// 页面
// ---------------------------------------------------------------------------
AbstractArtView::AbstractArtView(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_canvas = new AbstractArtCanvas(this);
    root->addWidget(m_canvas, 0, Qt::AlignCenter);

    auto *row = new QHBoxLayout();
    m_toggle = new QPushButton(this);
    auto *reset = new QPushButton(QStringLiteral("清空重来"), this);
    auto *save = new QPushButton(QStringLiteral("保存 PNG"), this);
    row->addWidget(m_toggle);
    row->addWidget(reset);
    row->addWidget(save);
    row->addStretch();
    root->addLayout(row);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(m_toggle, &QPushButton::clicked, this, &AbstractArtView::onToggleRunning);
    connect(reset, &QPushButton::clicked, this, [this] {
        m_canvas->resetCanvas();
        refreshStatus();
    });
    connect(save, &QPushButton::clicked, this, &AbstractArtView::onSave);
    connect(m_canvas, &AbstractArtCanvas::advanced, this, [this](int, bool) { refreshStatus(); });

    refreshStatus();
}

void AbstractArtView::onToggleRunning()
{
    m_canvas->setRunning(!m_canvas->running());
    refreshStatus();
}

void AbstractArtView::onSave()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存抽象画"), defaultSaveName(QStringLiteral("whalepet-abstract")),
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

void AbstractArtView::refreshStatus()
{
    const bool running = m_canvas->running();
    m_toggle->setText(running ? QStringLiteral("暂停") : QStringLiteral("继续"));

    QString text = QStringLiteral("已画 %1 笔（完全随机 RGB 配色）").arg(m_canvas->strokeCount());
    if (m_canvas->fading()) {
        text += QStringLiteral("；画面已满，正在淡出重来");
    }
    if (!running) {
        text += QStringLiteral("；已暂停");
    }
    if (m_status->text() == text) {
        return; // 每帧刷新时避免无意义的重排
    }
    m_status->setText(text);
}

} // namespace whalepet::ui
