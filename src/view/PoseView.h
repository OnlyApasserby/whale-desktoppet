#pragma once

// PoseView：立绘渲染 + 程序化动效（docs/PRESENTATION.md §2）。
//
// 前提：92 张立绘已统一为 256x256（VP8X），画布尺寸完全一致。
// 因此控件尺寸**固定**为 kPetWindowSize，切换姿态不再触发窗口 resize；
// 所有动效只作用在「内容层」（缩放 / 旋转 / 位移 / 透明度），不会出现窗口抖动或残影。

#include "common/PetVisuals.h"
#include "core/PetTypes.h"

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;

namespace whalepet {

class PoseLibrary;

class PoseView : public QWidget {
    Q_OBJECT
public:
    explicit PoseView(QWidget *parent = nullptr);

    // 注入立绘库（惰性预载）；缺省时退化为「按需即时加载单张」
    void setLibrary(PoseLibrary *library);

    // 无过渡直接落图：首帧、兜底、单测用
    bool setPoseImmediate(const QString &poseKey);
    // 带「下压 → 换图 → 弹起」过渡遮断的姿态切换
    // 同一姿态重复调用不会重启动画（状态机每 tick 都会推同一个 pose）
    bool setPose(const QString &poseKey);

    QString poseName() const { return m_poseName; }   // 当前目标（请求）姿态
    bool hasPose() const { return !m_render.isNull(); }
    const QPixmap &pixmap() const { return m_render; }

    // ---- 交互 ----
    // 分区命中：仅内容矩形内有效，按高度比例划分 头 / 肚 / 尾（见 PetVisuals.h）
    core::Zone zoneAt(const QPoint &widgetPos) const;
    bool containsContent(const QPoint &widgetPos) const;

    // 拖拽：摇摆目标按累计水平位移插值；松手按瞬时速度做惯性滑行 + 旋转回正
    void beginDrag();
    void dragMove(const QPoint &deltaFromPress);
    void endDrag(const QPointF &velocityPxPerSec);
    // 单击即时反馈：快速缩放回弹 + 位移
    void clickFeedback();

    // 特效（由 Presenter 依语义结果触发；自带 500ms 强制间隔，见 kFxMinGapMs）
    void playFx(core::Fx fx);

    // 诊断/单测：当前存活粒子数
    int particleCount() const { return m_particles.size(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Particle {
        QPointF pos;
        QPointF vel;        // px/ms
        qreal size = 6.0;
        qreal rot = 0.0;
        qreal rotVel = 0.0; // 度/ms
        int ageMs = 0;
        int lifeMs = 900;
        core::Fx kind = core::Fx::None;
    };

    // 内容几何：固定居中基准矩形（不含任何动效），命中与绘制共用
    QRectF baseContentRect() const;

    // 把 pose 解析并预缩放为可绘制位图；成功时写入 out（不改动当前画面）
    bool resolvePixmap(const QString &poseKey, QPixmap &out);
    void applyPixmap(const QString &poseKey);
    void startTransition(const QString &poseKey);

    void onFrame();
    void updateTimerInterval();
    bool dynamicsActive() const;

    void stepDragSway(qint64 dtMs);
    void stepGlide(qint64 dtMs);
    void stepParticles(qint64 dtMs);
    void spawnParticles(core::Fx fx);
    void removeParticlesOfKind(core::Fx fx);

    // 当前帧的内容变换（呼吸 × 过渡 × 点击反馈 + 位移 + 旋转）
    qreal frameScale(qint64 nowMs) const;
    QPointF frameOffset(qint64 nowMs) const;
    qreal frameRotationDeg() const;

    void paintParticles(QPainter &painter) const;
    void paintShape(QPainter &painter, const Particle &p) const;

    PoseLibrary *m_library = nullptr;

    QPixmap m_render;              // 已预缩放到 kPetDisplaySize 的立绘（当前画面）
    QPixmap m_staged;              // 过渡中待换上的下一张（进度过半才生效）
    QString m_poseName;            // 目标姿态（状态机语义）
    QString m_displayedPose;       // 已真正换上的姿态

    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs = 0;

    // 过渡遮断
    qint64 m_transitionStartMs = -1;
    qint64 m_transitionMs = kTransitionMs;

    // 点击反馈
    qint64 m_clickStartMs = -1;

    // 呼吸
    qint64 m_breathPhaseMs = 0;

    // 拖拽摇摆
    bool m_dragging = false;
    qreal m_swayDeg = 0.0;
    qreal m_swayTargetDeg = 0.0;

    // 松手惯性（sin 脉冲：滑出 → 弹回）
    QPointF m_glideDir;            // 单位方向
    qreal m_glideAmpPx = 0.0;      // 最大位移
    qint64 m_glideElapsedMs = 0;

    QVector<Particle> m_particles;
    qint64 m_lastFxMs = -1;   // 上次成功触发特效的时刻（m_clock 基准），用于强制最小间隔
};

} // namespace whalepet
