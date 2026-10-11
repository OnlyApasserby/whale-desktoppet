#pragma once

// P11 绘画模块：core::RgbColor（纯逻辑层的颜色）↔ QColor（Qt 绘制）的**唯一转换点**。
//
// 为什么需要它：绘画模块的颜色一律以 core::RgbColor 存储 / 传递（core 零 Qt），
// 只有真正落笔到 QPainter 时才转成 QColor。把转换收敛到这两个 inline 函数，
// 避免各视图各写一份、也避免任何地方出现颜色字面量。
//
// 颜色口径（见 docs/DRAWING.md §3）：
//   * 画面内容（抽象画的随机色 / 画板笔色 / 取色结果）来自 core（用户确认的「完全随机 RGB」）；
//   * 界面控件外观仍由全局样式表负责；自绘的界面装饰（十字线 / 读数块 / 取色框）取
//     common/UiPalette.h 的既有调色板值。

#include "core/DrawingPalette.h"

#include <QColor>

namespace whalepet::ui {

inline QColor toQColor(const core::RgbColor &color)
{
    const core::RgbColor safe = core::clampedColor(color);
    return QColor(safe.r, safe.g, safe.b, safe.a);
}

inline core::RgbColor fromQColor(const QColor &color)
{
    core::RgbColor out;
    out.r = color.red();
    out.g = color.green();
    out.b = color.blue();
    out.a = color.alpha();
    return core::clampedColor(out);
}

} // namespace whalepet::ui
