#include "view/PoseView.h"

#include "common/UiPalette.h"
#include "view/PoseLibrary.h"

#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QTimer>
#include <QTransform>
#include <QtMath>

#include <cmath>

// 立绘以 Qt 资源方式内嵌到 whalepet_view 静态库中。
// 静态库的资源不会自动注册，需在使用前显式初始化。
//
// 注意：Q_INIT_RESOURCE 宏**不能出现在任何命名空间内**（包括匿名命名空间），
// 否则宏内声明的 qInitResources_<name> 会被 C++ 名称修饰成带命名空间的符号，
// 与 rcc 在全局作用域生成的符号不匹配，链接期报 LNK2019。
// 因此该辅助函数必须定义在全局作用域。
static void whalepetInitAssetsResource()
{
    static bool initialized = false;
    if (!initialized) {
        Q_INIT_RESOURCE(assets);
        initialized = true;
    }
}

namespace whalepet {

namespace {

constexpr qreal kPi = 3.14159265358979323846;

// 呼吸微幅抖动：周期 ±kBreathJitter
constexpr qreal kBreathJitter = 0.025;
// 摇摆/位移的收敛阈值：低于该值即认为静止
constexpr qreal kSwayEpsilonDeg = 0.05;
constexpr qreal kOffsetEpsilonPx = 0.1;

inline qreal easeOut(qreal p)
{
    return 1.0 - (1.0 - p) * (1.0 - p);
}

} // namespace

PoseView::PoseView(QWidget *parent)
    : QWidget(parent)
{
    // 立绘透明背景：容器自身不绘制底色
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // 统一画布：默认尺寸固定，切换立绘不 resize（避免抖动与残影）；
    // 仅在设置项 pose_size 变化时经 setDisplaySize() 做一次性调整。
    setFixedSize(m_displaySize + kPetMargin * 2, m_displaySize + kPetMargin * 2);

    m_clock.start();

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(kFrameIntervalIdleMs);
    connect(m_timer, &QTimer::timeout, this, &PoseView::onFrame);
    m_timer->start();
}

void PoseView::setLibrary(PoseLibrary *library)
{
    if (m_library == library) {
        return;
    }
    if (m_library != nullptr) {
        disconnect(m_library, nullptr, this, nullptr);
    }
    m_library = library;

    if (m_library != nullptr) {
        // 惰性预载补齐后，兑现当前请求（若此前因缺图未换上）
        connect(m_library, &PoseLibrary::poseReady, this, [this](const QString &key) {
            if (key != m_poseName || key == m_displayedPose) {
                return;
            }
            // 此前因缺图没能换上的目标姿态：现在直接落图（过渡已走完，不再重放）
            QPixmap resolved;
            if (resolvePixmap(key, resolved)) {
                m_staged = QPixmap();
                m_render = resolved;
                m_transitionStartMs = -1;
                applyPixmap(key);
            }
        });
    }
}

QSize PoseView::sizeHint() const
{
    const int side = m_displaySize + kPetMargin * 2;
    return QSize(side, side);
}

QSize PoseView::minimumSizeHint() const
{
    return sizeHint();
}

void PoseView::setDisplaySize(int px)
{
    const int size = qBound(kMinDisplaySize, px, kMaxDisplaySize);
    if (size == m_displaySize) {
        return;
    }
    m_displaySize = size;
    setFixedSize(size + kPetMargin * 2, size + kPetMargin * 2);

    // 当前画面与过渡暂存图都按新尺寸重采样（失败时保留旧图，不闪空）
    QPixmap rescaled;
    if (!m_displayedPose.isEmpty() && resolvePixmap(m_displayedPose, rescaled)) {
        m_render = rescaled;
    }
    if (!m_staged.isNull() && !m_poseName.isEmpty()) {
        QPixmap staged;
        if (resolvePixmap(m_poseName, staged)) {
            m_staged = staged;
        }
    }
    // 贴边立绘按新尺寸重采样，并按新的可见内容包围盒重新贴合边框
    if (m_edge != core::DesktopEdge::None) {
        refreshEdgePixmap();
    }
    updateTimerInterval();
    update();
}

void PoseView::setParticlesEnabled(bool enabled)
{
    m_particlesEnabled = enabled;
    if (!enabled) {
        m_particles.clear(); // 关闭时立即清空存量，避免残留
    }
    updateTimerInterval();
}

void PoseView::setDragInertiaEnabled(bool enabled)
{
    m_dragInertiaEnabled = enabled;
    if (!enabled) {
        m_glideAmpPx = 0.0;
        m_glideElapsedMs = 0;
    }
    updateTimerInterval();
}

// ---------------------------------------------------------------------------
// 姿态切换
// ---------------------------------------------------------------------------

bool PoseView::setPoseImmediate(const QString &poseKey)
{
    if (poseKey.isEmpty()) {
        return false;
    }
    m_poseName = poseKey;

    QPixmap resolved;
    if (!resolvePixmap(poseKey, resolved)) {
        // 立绘缺失：保留上一张（不闪空），但目标姿态已更新
        return false;
    }
    m_transitionStartMs = -1;
    m_staged = QPixmap();
    m_render = resolved;
    applyPixmap(poseKey);
    return true;
}

bool PoseView::setPose(const QString &poseKey)
{
    if (poseKey.isEmpty()) {
        return false;
    }
    // 目标姿态未变：不重启动画（状态机每 tick 都会推同一个 pose）
    if (poseKey == m_poseName && !m_render.isNull()) {
        return true;
    }
    // 贴边表现优先：拖动过程中窗口若已贴合桌面边框，**不**用拖动立绘（pick-up）替换探头立绘。
    // 这里连目标姿态都不改（m_poseName 保持原值），拖离边框后由正常路径恢复；
    // 拖拽期间状态机每 tick 都会重推缓存结果，故恢复延迟不超过一个帧间隔。
    if (m_dragging && m_edge != core::DesktopEdge::None) {
        return true;
    }
    // 首帧 / 当前无图：直接落图，不做过渡
    if (m_displayedPose.isEmpty() || m_render.isNull()) {
        return setPoseImmediate(poseKey);
    }

    startTransition(poseKey);
    return true;
}

void PoseView::startTransition(const QString &poseKey)
{
    // 先把目标位图准备好，但**不**立刻换上：真正换图发生在过渡进度过半时，
    // 否则「下压 → 换图 → 弹起」会退化成「先换图再走动画」。
    if (!resolvePixmap(poseKey, m_staged)) {
        qWarning() << "[PoseView] 过渡目标立绘缺失，保持当前画面:" << poseKey;
        m_staged = QPixmap();
        m_poseName = poseKey; // 目标已记录，待预载补齐后由 poseReady 兑现
        return;
    }
    m_poseName = poseKey;
    m_transitionStartMs = m_clock.elapsed();
    m_transitionMs = kTransitionMs;
    updateTimerInterval();
}

QPixmap PoseView::loadSourcePixmap(const QString &poseKey) const
{
    whalepetInitAssetsResource();

    if (m_library != nullptr && m_library->isLoaded(poseKey)) {
        const QPixmap fromLibrary = m_library->pixmap(poseKey);
        if (!fromLibrary.isNull()) {
            return fromLibrary;
        }
    }
    // 库未补齐（或未注入库）：按需即时加载单张，避免出现空白
    const QString path = PoseLibrary::resourcePath(poseKey);
    if (path.isEmpty()) {
        return QPixmap();
    }
    QPixmap direct;
    if (direct.load(path)) {
        return direct;
    }
    return QPixmap();
}

bool PoseView::resolvePixmap(const QString &poseKey, QPixmap &out)
{
    const QPixmap source = loadSourcePixmap(poseKey);
    if (source.isNull()) {
        qWarning() << "[PoseView] 立绘不可用:" << poseKey;
        return false;
    }

    // 预缩放到当前显示尺寸：每帧只做几何变换，不做重采样（降低空闲 CPU）
    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    QPixmap scaled = source.scaled(QSize(m_displaySize, m_displaySize),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    out = scaled;
    return true;
}

void PoseView::applyPixmap(const QString &poseKey)
{
    m_displayedPose = poseKey;
    update();
}

// ---------------------------------------------------------------------------
// 桌面贴边（docs/PRESENTATION.md §3.1）
// ---------------------------------------------------------------------------

void PoseView::setEdgeAttachment(core::DesktopEdge edge)
{
    if (m_edge == edge) {
        return;
    }
    m_edge = edge;
    // 立即换图（不走「下压 → 换图 → 弹起」过渡遮断，避免探头立绘延迟出现）；
    // None 时 refreshEdgePixmap 会清空贴边位图，绘制自动回到常规居中方式
    refreshEdgePixmap();
    update();
}

void PoseView::refreshEdgePixmap()
{
    m_edgePose.clear();
    m_edgeRender = QPixmap();
    m_edgeRect = QRectF();

    const char *key = core::edgePoseKey(m_edge);
    if (key == nullptr) {
        return; // None：无贴边立绘
    }
    const QString poseKey = QString::fromUtf8(key);
    const QPixmap source = loadSourcePixmap(poseKey);
    if (source.isNull()) {
        qWarning() << "[PoseView] 贴边立绘不可用，保持常规显示:" << poseKey;
        return;
    }

    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    QPixmap scaled = source.scaled(QSize(m_displaySize, m_displaySize),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);

    m_edgePose = poseKey;
    m_edgeRender = scaled;
    layoutEdge();
}

QRectF PoseView::contentBBox(const QString &poseKey)
{
    const auto cached = m_contentBBoxCache.constFind(poseKey);
    if (cached != m_contentBBoxCache.constEnd()) {
        return cached.value();
    }

    // 兜底：整幅画布（缺图 / 全透明时不做偏移，至少不会把立绘推出窗口）
    QRectF bbox(0.0, 0.0, 1.0, 1.0);
    const QPixmap source = loadSourcePixmap(poseKey);
    if (!source.isNull()) {
        const QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
        if (!image.isNull() && image.width() > 0 && image.height() > 0) {
            int minX = image.width();
            int minY = image.height();
            int maxX = -1;
            int maxY = -1;
            for (int y = 0; y < image.height(); ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(line[x]) > kPeekAlphaThreshold) {
                        minX = qMin(minX, x);
                        maxX = qMax(maxX, x);
                        minY = qMin(minY, y);
                        maxY = qMax(maxY, y);
                    }
                }
            }
            if (maxX >= minX && maxY >= minY) {
                bbox = QRectF(static_cast<qreal>(minX) / image.width(),
                              static_cast<qreal>(minY) / image.height(),
                              static_cast<qreal>(maxX - minX + 1) / image.width(),
                              static_cast<qreal>(maxY - minY + 1) / image.height());
            }
        }
    }
    m_contentBBoxCache.insert(poseKey, bbox);
    return bbox;
}

void PoseView::layoutEdge()
{
    m_edgeRect = QRectF();
    if (m_edgeRender.isNull() || m_edge == core::DesktopEdge::None) {
        return;
    }

    const qreal side = m_displaySize;
    const QRectF bbox = contentBBox(m_edgePose);
    // 可见内容在绘制矩形内的位置（bbox 为源图归一化坐标）
    const qreal contentX = bbox.x() * side;
    const qreal contentY = bbox.y() * side;
    const qreal contentW = bbox.width() * side;
    const qreal contentH = bbox.height() * side;

    // 默认居中；贴边方向只挪动「另一轴」，让可见内容贴齐对应边框
    qreal ix = (width() - side) / 2.0;
    qreal iy = (height() - side) / 2.0;
    switch (m_edge) {
    case core::DesktopEdge::Top:
        iy = -contentY; // 内容上边 = 窗口上边
        break;
    case core::DesktopEdge::Bottom:
        iy = height() - (contentY + contentH); // 内容下边 = 窗口下边
        break;
    case core::DesktopEdge::Left:
        ix = -contentX; // 内容左边 = 窗口左边
        break;
    case core::DesktopEdge::Right:
        ix = width() - (contentX + contentW); // 内容右边 = 窗口右边
        break;
    case core::DesktopEdge::None:
        break;
    }
    m_edgeRect = QRectF(ix, iy, side, side);
}

QPointF PoseView::edgeAnchor() const
{
    switch (m_edge) {
    case core::DesktopEdge::Top:
        return QPointF(width() / 2.0, 0.0);
    case core::DesktopEdge::Bottom:
        return QPointF(width() / 2.0, static_cast<qreal>(height()));
    case core::DesktopEdge::Left:
        return QPointF(0.0, height() / 2.0);
    case core::DesktopEdge::Right:
        return QPointF(static_cast<qreal>(width()), height() / 2.0);
    case core::DesktopEdge::None:
        break;
    }
    return baseContentRect().center();
}

void PoseView::paintEdge(QPainter &painter, qint64 /*nowMs*/) const
{
    if (m_edgeRender.isNull() || m_edgeRect.isEmpty()) {
        return;
    }

    // 贴边只保留呼吸缩放：摇摆 / 惯性 / 点击位移都会把立绘从边框上挪开；
    // 缩放锚点取**贴合边**，保证呼吸过程中立绘始终贴着边框。
    const qreal scale = 1.0 + kBreathAmp * std::sin(2.0 * kPi * m_breathPhaseMs / kBreathPeriodMs);
    const QPointF anchor = edgeAnchor();

    QTransform t;
    t.translate(anchor.x(), anchor.y());
    t.scale(scale, scale);
    t.translate(-anchor.x(), -anchor.y());

    painter.setTransform(t);
    painter.drawPixmap(m_edgeRect.topLeft(), m_edgeRender,
                       QRectF(QPointF(0.0, 0.0), QSizeF(m_displaySize, m_displaySize)));
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------

QRectF PoseView::baseContentRect() const
{
    const qreal x = (width() - m_displaySize) / 2.0;
    const qreal y = (height() - m_displaySize) / 2.0;
    return QRectF(x, y, m_displaySize, m_displaySize);
}

bool PoseView::containsContent(const QPoint &widgetPos) const
{
    if (m_edge != core::DesktopEdge::None) {
        return m_edgeRect.contains(widgetPos);
    }
    return baseContentRect().contains(widgetPos);
}

core::Zone PoseView::zoneAt(const QPoint &widgetPos) const
{
    // 贴边立绘只露出「探头」（脸），整块命中区都算头——与参考项目 peek 分区一致
    if (m_edge != core::DesktopEdge::None) {
        return m_edgeRect.contains(widgetPos) ? core::Zone::Head : core::Zone::None;
    }

    const QRectF base = baseContentRect();
    if (!base.contains(widgetPos)) {
        return core::Zone::None;
    }
    const qreal rel = (widgetPos.y() - base.top()) / base.height();
    if (rel < kHeadZoneEnd) {
        return core::Zone::Head;
    }
    if (rel < kBellyZoneEnd) {
        return core::Zone::Belly;
    }
    return core::Zone::Tail;
}

void PoseView::beginDrag()
{
    m_dragging = true;
    m_glideAmpPx = 0.0;
    m_glideElapsedMs = 0;
    updateTimerInterval();
}

void PoseView::dragMove(const QPoint &deltaFromPress)
{
    // 摇摆目标 = 累计水平位移 × 角度系数（负号：向右拖时头部顺势后仰）
    const qreal target = -deltaFromPress.x() * kSwayDegPerPx;
    m_swayTargetDeg = qBound(-kMaxSwayDeg, target, kMaxSwayDeg);
}

void PoseView::endDrag(const QPointF &velocityPxPerSec)
{
    m_dragging = false;
    m_swayTargetDeg = 0.0; // 旋转回正

    const qreal speed = std::hypot(velocityPxPerSec.x(), velocityPxPerSec.y());
    // drag_inertia 关闭（或速度不足）→ 不做惯性滑行
    if (!m_dragInertiaEnabled || speed < kInertiaMinSpeed) {
        m_glideAmpPx = 0.0;
    } else {
        const qreal amp = qMin(speed * kInertiaFactor, kInertiaMaxOffsetPx);
        m_glideDir = QPointF(velocityPxPerSec.x() / speed, velocityPxPerSec.y() / speed);
        m_glideAmpPx = amp;
        m_glideElapsedMs = 0;
    }
    updateTimerInterval();
}

void PoseView::clickFeedback()
{
    m_clickStartMs = m_clock.elapsed();
    updateTimerInterval();
}

// ---------------------------------------------------------------------------
// 动效推进
// ---------------------------------------------------------------------------

void PoseView::onFrame()
{
    const qint64 now = m_clock.elapsed();
    const qint64 dt = qMax<qint64>(1, now - m_lastFrameMs);
    m_lastFrameMs = now;

    // 过渡遮断：压到最扁的瞬间换图（kTransitionSwapAt），再弹起
    if (m_transitionStartMs >= 0) {
        const qreal p = static_cast<qreal>(now - m_transitionStartMs) / m_transitionMs;
        if (p >= kTransitionSwapAt && m_displayedPose != m_poseName) {
            if (!m_staged.isNull()) {
                m_render = m_staged;
                m_staged = QPixmap();
            }
            applyPixmap(m_poseName);
        }
        if (p >= 1.0) {
            m_transitionStartMs = -1;
        }
    }

    if (m_clickStartMs >= 0 && now - m_clickStartMs >= kClickFeedbackMs) {
        m_clickStartMs = -1;
    }

    m_breathPhaseMs += dt;
    stepDragSway(dt);
    stepGlide(dt);
    stepParticles(dt);

    updateTimerInterval();
    update();
}

void PoseView::stepDragSway(qint64 dtMs)
{
    const qreal alpha = 1.0 - std::exp(-static_cast<qreal>(dtMs) / kSwayTauMs);
    m_swayDeg += (m_swayTargetDeg - m_swayDeg) * alpha;
    if (!m_dragging && std::fabs(m_swayDeg) < kSwayEpsilonDeg) {
        m_swayDeg = 0.0;
    }
}

void PoseView::stepGlide(qint64 dtMs)
{
    if (m_glideAmpPx <= 0.0) {
        return;
    }
    m_glideElapsedMs += dtMs;
    if (m_glideElapsedMs >= kInertiaMs) {
        m_glideAmpPx = 0.0;
        m_glideElapsedMs = 0;
    }
}

void PoseView::stepParticles(qint64 dtMs)
{
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle &p = m_particles[i];
        p.ageMs += static_cast<int>(dtMs);
        if (p.ageMs >= p.lifeMs) {
            m_particles.remove(i);
            continue;
        }
        const qreal f = static_cast<qreal>(dtMs);
        p.vel.ry() += kParticleGravity * f * f / 1000.0; // 重力按 dt² 积分
        p.pos += p.vel * f;
        p.rot += p.rotVel * f;
    }
}

void PoseView::spawnParticles(core::Fx fx)
{
    if (fx == core::Fx::None) {
        return;
    }
    auto *rng = QRandomGenerator::global();
    const QRectF base = baseContentRect();
    const QPointF center = base.center();

    const auto push = [this](const Particle &p) {
        if (m_particles.size() >= kMaxParticles) {
            m_particles.removeFirst();
        }
        m_particles.append(p);
    };

    switch (fx) {
    case core::Fx::Heart: {
        for (int i = 0; i < kHeartCount; ++i) {
            Particle p;
            p.kind = core::Fx::Heart;
            p.pos = QPointF(center.x() - 30.0 + rng->bounded(60.0),
                            base.bottom() - 60.0 - rng->bounded(30.0));
            p.vel = QPointF(-0.012 + rng->bounded(0.024), -0.03 - rng->bounded(0.02));
            p.size = 12.0 + rng->bounded(6.0);
            p.rot = -12.0 + rng->bounded(24.0);
            p.rotVel = -0.002 + rng->bounded(0.004);
            p.lifeMs = kParticleLifeLongMs;
            p.ageMs = -i * 60; // 依次冒出
            push(p);
        }
        break;
    }
    case core::Fx::Star: {
        for (int i = 0; i < kStarCount; ++i) {
            const qreal ang = (2.0 * kPi * i) / kStarCount + rng->bounded(0.4);
            const qreal speed = 0.05 + rng->bounded(0.06);
            Particle p;
            p.kind = core::Fx::Star;
            p.pos = center + QPointF(std::cos(ang), std::sin(ang)) * 24.0;
            p.vel = QPointF(std::cos(ang), std::sin(ang)) * speed;
            p.size = 9.0 + rng->bounded(7.0);
            p.rot = rng->bounded(360.0);
            p.rotVel = -0.01 + rng->bounded(0.02);
            p.lifeMs = kParticleLifeLongMs;
            push(p);
        }
        break;
    }
    case core::Fx::Particle: {
        for (int i = 0; i < kSparkCount; ++i) {
            const qreal ang = rng->bounded(2.0 * kPi);
            const qreal speed = 0.04 + rng->bounded(0.09);
            Particle p;
            p.kind = core::Fx::Particle;
            p.pos = center + QPointF(std::cos(ang), std::sin(ang)) * rng->bounded(20.0);
            p.vel = QPointF(std::cos(ang), std::sin(ang)) * speed;
            p.size = 2.0 + rng->bounded(3.0);
            p.lifeMs = kParticleLifeMs;
            push(p);
        }
        break;
    }
    case core::Fx::None:
        break;
    }

    updateTimerInterval();
}

void PoseView::removeParticlesOfKind(core::Fx fx)
{
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        if (m_particles.at(i).kind == fx) {
            m_particles.remove(i);
        }
    }
}

void PoseView::playFx(core::Fx fx)
{
    if (fx == core::Fx::None || !m_particlesEnabled) {
        return;
    }

    // 强制最小间隔：一次操作只迸发一次；间隔内的新触发**直接丢弃**（不排队）。
    // 序号去重（Presenter）已保证同一结果不被重放，这里再兜住「急速连点」的叠加。
    const qint64 now = m_clock.elapsed();
    if (m_lastFxMs >= 0 && (now - m_lastFxMs) < kFxMinGapMs) {
        return;
    }
    m_lastFxMs = now;

    // 同类粒子仍在飘时先清掉，避免两批叠加被看成「持续迸发」
    removeParticlesOfKind(fx);
    spawnParticles(fx);
}

// ---------------------------------------------------------------------------
// 帧间隔
// ---------------------------------------------------------------------------

bool PoseView::dynamicsActive() const
{
    if (m_transitionStartMs >= 0 || m_clickStartMs >= 0) {
        return true;
    }
    if (m_dragging || m_glideAmpPx > 0.0) {
        return true;
    }
    if (std::fabs(m_swayDeg) > kSwayEpsilonDeg) {
        return true;
    }
    return !m_particles.isEmpty();
}

void PoseView::updateTimerInterval()
{
    if (m_timer == nullptr) {
        return;
    }
    const int want = dynamicsActive() ? kFrameIntervalActiveMs : kFrameIntervalIdleMs;
    if (m_timer->interval() != want) {
        m_timer->setInterval(want);
    }
    if (!m_timer->isActive()) {
        m_timer->start();
    }
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

qreal PoseView::frameScale(qint64 nowMs) const
{
    qreal scale = 1.0;

    // 呼吸：轻度幅度抖动，避免机械感
    const qreal jitter = 1.0 + kBreathJitter * std::sin(nowMs / 900.0);
    const qreal breathAmp = kBreathAmp * jitter;
    scale *= 1.0 + breathAmp * std::sin(2.0 * kPi * m_breathPhaseMs / kBreathPeriodMs);

    // 过渡遮断：中点压到最扁，两点回到原尺寸
    if (m_transitionStartMs >= 0) {
        const qreal p = qBound(0.0, static_cast<qreal>(nowMs - m_transitionStartMs) / m_transitionMs, 1.0);
        scale *= 1.0 - (1.0 - kTransitionSquash) * std::sin(kPi * p);
    }

    // 点击反馈：快速回弹
    if (m_clickStartMs >= 0) {
        const qreal p = qBound(0.0, static_cast<qreal>(nowMs - m_clickStartMs) / kClickFeedbackMs, 1.0);
        scale *= 1.0 + kClickSquash * std::sin(kPi * p);
    }

    return scale;
}

QPointF PoseView::frameOffset(qint64 nowMs) const
{
    QPointF offset;

    // 惯性滑行：sin 脉冲（0 → 最大 → 0），产生「滑出去再弹回」的手感
    if (m_glideAmpPx > 0.0) {
        const qreal p = qBound(0.0, static_cast<qreal>(m_glideElapsedMs) / kInertiaMs, 1.0);
        offset += m_glideDir * (m_glideAmpPx * std::sin(kPi * p));
    }

    // 点击反馈：向上轻顶一下
    if (m_clickStartMs >= 0) {
        const qreal p = qBound(0.0, static_cast<qreal>(nowMs - m_clickStartMs) / kClickFeedbackMs, 1.0);
        offset.ry() -= kClickNudgePx * std::sin(kPi * p);
    }

    return offset;
}

qreal PoseView::frameRotationDeg() const
{
    return m_swayDeg;
}

void PoseView::paintParticles(QPainter &painter) const
{
    for (const Particle &p : m_particles) {
        paintShape(painter, p);
    }
}

void PoseView::paintShape(QPainter &painter, const Particle &p) const
{
    if (p.ageMs < 0) {
        return;
    }
    const qreal life = qBound(0.0, static_cast<qreal>(p.ageMs) / p.lifeMs, 1.0);
    // 淡入 15% → 保持 → 淡出
    qreal alpha = 1.0;
    if (life < 0.15) {
        alpha = life / 0.15;
    } else if (life > 0.6) {
        alpha = 1.0 - (life - 0.6) / 0.4;
    }
    const int a = static_cast<int>(qBound(0.0, alpha, 1.0) * 255.0);
    if (a <= 0) {
        return;
    }

    painter.save();
    painter.translate(p.pos);
    painter.rotate(p.rot);

    const qreal s = p.size;
    switch (p.kind) {
    case core::Fx::Heart: {
        QPainterPath path;
        path.moveTo(0.0, s * 0.35);
        path.cubicTo(-s * 0.9, -s * 0.35, -s * 0.35, -s, 0.0, -s * 0.4);
        path.cubicTo(s * 0.35, -s, s * 0.9, -s * 0.35, 0.0, s * 0.35);
        painter.setBrush(ui::foreground(a));
        painter.setPen(Qt::NoPen);
        painter.drawPath(path);
        break;
    }
    case core::Fx::Star: {
        QPolygonF star;
        const int points = 5;
        for (int i = 0; i < points * 2; ++i) {
            const qreal r = (i % 2 == 0) ? s : s * 0.45;
            const qreal ang = kPi * i / points - kPi / 2.0;
            star << QPointF(std::cos(ang) * r, std::sin(ang) * r);
        }
        painter.setBrush(ui::foreground(a));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(star);
        break;
    }
    case core::Fx::Particle:
    default: {
        painter.setBrush(ui::muted(a));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(0.0, 0.0), s, s);
        break;
    }
    }

    painter.restore();
}

void PoseView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qint64 now = m_clock.elapsed();

    if (m_edge != core::DesktopEdge::None && !m_edgeRender.isNull()) {
        // 贴边优先：探头立绘贴齐对应边框；常规姿态照常保留在 m_render 中，离开边框即恢复
        paintEdge(painter, now);
    } else if (!m_render.isNull()) {
        const QSizeF drawSize(m_displaySize, m_displaySize);
        const QPointF center = baseContentRect().center() + frameOffset(now);

        QTransform t;
        t.translate(center.x(), center.y());
        t.rotate(frameRotationDeg());
        const qreal scale = frameScale(now);
        t.scale(scale, scale);
        t.translate(-drawSize.width() / 2.0, -drawSize.height() / 2.0);

        painter.setTransform(t);
        painter.drawPixmap(QPointF(0.0, 0.0), m_render, QRectF(QPointF(0.0, 0.0), drawSize));
    }

    // 粒子绘制在内容坐标系之外（跟随内容位移），故重置变换
    painter.resetTransform();
    if (!m_particles.isEmpty()) {
        const QPointF off = frameOffset(now);
        painter.translate(off);
        paintParticles(painter);
        painter.translate(-off);
    }
}

} // namespace whalepet
