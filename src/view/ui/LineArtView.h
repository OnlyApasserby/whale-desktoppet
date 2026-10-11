#pragma once

// P11 绘画模块之二：**鲸鱼娘线条画生成器**视图（工作台页之一）。
//
// 需求口径：「利用基础平面几何图形（如圆、矩形、三角形、多边形）的线框组合构建角色轮廓，
// 需实现图形拼接与风格一致性」。拼接与一致性由 core::makeLineArt 构造性保证（见 core/LineArt.h），
// 本视图只负责：把 plan 用 QPainter 的**线框**画出来 + 「换一只 / 保存」。
//
// 底色：黑底白线（与项目既有自绘口径一致，颜色取自 common/UiPalette.h），
// 不使用任何颜色字面量、不写局部样式表。

#include "core/LineArt.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace whalepet::ui {

// 画布控件（固定尺寸；每次 regenerate() 重新生成一张）
class LineArtCanvas : public QWidget {
    Q_OBJECT
public:
    explicit LineArtCanvas(QWidget *parent = nullptr);

    // 用新种子重新生成（界面按钮 / 首次显示）
    void regenerate();
    // 直接注入一张计划（单测用：确定性校验渲染路径不崩、也不改数据）
    void setPlan(const core::LineArtPlan &plan);

    const core::LineArtPlan &plan() const { return m_plan; }
    quint32 seed() const { return m_plan.seed; }

    static constexpr int kCanvasWidth = 480;
    static constexpr int kCanvasHeight = 360;

signals:
    void regenerated();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    core::LineArtStyle makeStyle() const;

    core::LineArtPlan m_plan;
    core::SystemRandom m_seedSource; // 只用来抽取种子（每种子的抖动可复现）
    int m_generation = 0;
};

// 线条画页：画布 + 换一只 / 保存 PNG + 种子读数
class LineArtView : public QWidget {
    Q_OBJECT
public:
    explicit LineArtView(QWidget *parent = nullptr);

    LineArtCanvas *canvas() const { return m_canvas; }

private:
    void onSave();
    void refreshStatus();

    LineArtCanvas *m_canvas = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace whalepet::ui
