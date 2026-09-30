#pragma once

// 自绘（paintEvent）所需的颜色唯一来源。
//
// 为什么需要这个文件：QSS（resources/qt-ui/default.qss）只能作用于控件绘制，
// QPainter 自绘（立绘粒子 / 气泡尖角等）读不到样式表取值。为满足「全工程颜色无散落字面量」
// 的约束，把自绘用到的取值集中登记在此，其余文件一律引用这里的常量。
//
// 取值来源：resources/qt-ui/default.qss 的调色板（见 skills/qt-ui §3.1），不新增任何颜色。

#include <QColor>
#include <QRgb>

namespace whalepet::ui {

// #FFFFFF 主文本（前景）
inline constexpr QRgb kForeground = 0xFFFFFFFFu;
// #B0B0B0 次要文本
inline constexpr QRgb kMuted = 0xFFB0B0B0u;

// 按透明度取色（自绘淡入淡出用）
inline QColor foreground(int alpha)
{
    QColor c(kForeground);
    c.setAlpha(qBound(0, alpha, 255));
    return c;
}

inline QColor muted(int alpha)
{
    QColor c(kMuted);
    c.setAlpha(qBound(0, alpha, 255));
    return c;
}

} // namespace whalepet::ui
