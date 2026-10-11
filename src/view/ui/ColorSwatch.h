#pragma once

// P11 绘画模块：**自绘色块**（画板当前色 / 最近取色列表）。
//
// 为什么自绘：QSS 无法表达「一个只显示任意颜色的方块」；色块内容是**数据**（当前笔色），
// 不是界面设计 —— 因此本控件不调用 setStyleSheet、不引入任何颜色字面量：
//   * 方块填充 = 传入的颜色本身（core::RgbColor）；
//   * 边框 / 文字颜色 = common/UiPalette.h 的既有调色板值（黑白灰，与全局样式同源）；
//   * 文字用黑还是白由 core::perceivedLuminance() 决定（保证任意底色上都读得清）。

#include "core/DrawingPalette.h"

#include <QWidget>

class QPaintEvent;

namespace whalepet::ui {

class ColorSwatch : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatch(QWidget *parent = nullptr);

    void setColor(const core::RgbColor &color);
    const core::RgbColor &color() const { return m_color; }

    // 显示十六进制文本（小色块可关掉，例如「最近取色」列表）
    void setTextVisible(bool visible);

signals:
    void clicked(); // 供「最近取色」列表复用：点一下就选中该颜色

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    core::RgbColor m_color;
    bool m_textVisible = true;
};

} // namespace whalepet::ui
