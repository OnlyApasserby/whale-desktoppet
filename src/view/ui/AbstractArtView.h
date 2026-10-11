#pragma once

// P11 绘画模块之一：**鲸鱼娘抽象画生成器**视图（工作台页之一）。
//
// 需求口径：「使用随机数算法动态产生配色方案，并基于 QPainter 进行实时画布绘制，
// 需支持连续刷新与视觉变化」。
//   * 随机数算法（配色 + 图元 + 几何）在 core::AbstractArtGenerator（零 Qt，可单测）；
//   * 本视图只做两件事：把笔触用 QPainter 落到**离屏 QImage**（累积），以及按定时器
//     连续刷新。**不每帧重放全部笔触** —— 笔触数越多这种做法的代价越大，而累积位图
//     每帧只补画新笔触；
//   * 笔触累计到上限（core::kAbstractStrokeCap）后改为**淡出**（白色低透明度覆盖），
//     归零后重新生长：既保证「连续刷新与视觉变化」永不停止，又给内存 / 绘制量一个硬上限。
//
// 白底画布：底色取调色板白（ui::foreground），由本视图直接绘制到 QImage；
// 外围控件（按钮 / 标签）一律沿用全局样式表，本视图不写任何局部样式。

#include "core/AbstractArt.h"

#include <QImage>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace whalepet::ui {

// 画布控件（固定尺寸；自绘离屏位图）
class AbstractArtCanvas : public QWidget {
    Q_OBJECT
public:
    explicit AbstractArtCanvas(QWidget *parent = nullptr);

    void setRunning(bool running);
    bool running() const;
    void resetCanvas(); // 清空并复位随机演化（重新从白底开始）
    void reseed(quint32 seed);

    int strokeCount() const { return m_strokeCount; }
    bool fading() const { return m_fading; }
    const QImage &image() const { return m_image; }

    static constexpr int kCanvasWidth = 480;
    static constexpr int kCanvasHeight = 360;

signals:
    void advanced(int strokeCount, bool fading);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void advance();

    QImage m_image;
    QTimer *m_timer = nullptr;
    core::SystemRandom m_rng;
    core::AbstractArtGenerator m_generator;
    int m_strokeCount = 0;
    int m_fadeTicks = 0;
    bool m_fading = false;
};

// 抽象画页：画布 + 开始/暂停 / 清空 / 保存 PNG + 状态读数
class AbstractArtView : public QWidget {
    Q_OBJECT
public:
    explicit AbstractArtView(QWidget *parent = nullptr);

    AbstractArtCanvas *canvas() const { return m_canvas; }

private:
    void onToggleRunning();
    void onSave();
    void refreshStatus();

    AbstractArtCanvas *m_canvas = nullptr;
    QPushButton *m_toggle = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace whalepet::ui
