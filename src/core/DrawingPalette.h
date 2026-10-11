#pragma once

// P11 绘画模块：**配色**的纯逻辑（零 Qt、零界面依赖 → 可脱界面单测）。
//
// 随机配色口径（用户 2026-10-11 决策，见 docs/DRAWING.md §3）：
//   * 抽象画采用**完全随机 RGB**：每次运笔时三通道各自在 [0,255] 独立取值，
//     不做色相簇限制、不做对比度过滤（视觉最杂乱、抽象感最强）；
//   * 随机源经 core::IRandom 注入（生产 = SystemRandom，单测 = ScriptedRandom /
//     定种子 SystemRandom），因此「同一种子 → 同一张画」可复现；
//   * 颜色是**画面内容**（绘画本体），不是界面控件外观 —— qt-ui 的「无彩色字面量」
//     约束只作用于界面控件；界面（按钮 / 标签 / 工具栏）颜色仍全部来自
//     全局样式表与 common/UiPalette.h，本文件不参与任何界面取值。

#include "core/IRandom.h"

#include <string>

namespace whalepet::core {

// 8 位 RGBA 颜色（纯数据；不把 Qt 的 QColor 带进 core）
struct RgbColor {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;
};

inline bool operator==(const RgbColor &lhs, const RgbColor &rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

inline bool operator!=(const RgbColor &lhs, const RgbColor &rhs)
{
    return !(lhs == rhs);
}

// 除 alpha 外三通道相同（用于「同一色相不同透明度」的断言）
inline bool sameRgb(const RgbColor &lhs, const RgbColor &rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

// 抽象画笔触的 alpha 区间：允许半透明叠加（形成「不断刷新演化」的层次感），
// 但下限保证每一笔都看得见，上限保留与背景的融合空间。
inline constexpr int kMinStrokeAlpha = 64;
inline constexpr int kMaxStrokeAlpha = 220;

// 越界钳制（三通道 [0,255]、alpha [0,255]）
RgbColor clampedColor(const RgbColor &color);

// **完全随机 RGB**（三通道独立，各自 [0,255]）；alpha 在 [minAlpha, maxAlpha] 内随机。
// 参数越界时先钳制（minAlpha > maxAlpha 时取 minAlpha）。
RgbColor randomRgb(IRandom &rng, int minAlpha = kMinStrokeAlpha, int maxAlpha = kMaxStrokeAlpha);

// 十六进制文本 `#RRGGBB`（界面显示 / 复制到剪贴板用；alpha 不参与）
std::string toHexString(const RgbColor &color);

// 感知亮度（0..1，sRGB 加权）——供界面在色块上选择黑字还是白字，**不用于过滤颜色**
double perceivedLuminance(const RgbColor &color);

} // namespace whalepet::core
