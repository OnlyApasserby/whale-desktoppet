#include "core/DrawingPalette.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace whalepet::core {

namespace {

int clampChannel(int value)
{
    return std::max(0, std::min(255, value));
}

// 在 [low, high] 上取整型均匀随机（low > high 时退化为 low）
int randomInRange(IRandom &rng, int low, int high)
{
    if (high <= low) {
        return low;
    }
    return low + rng.nextInt(high - low + 1);
}

} // namespace

RgbColor clampedColor(const RgbColor &color)
{
    RgbColor out;
    out.r = clampChannel(color.r);
    out.g = clampChannel(color.g);
    out.b = clampChannel(color.b);
    out.a = clampChannel(color.a);
    return out;
}

RgbColor randomRgb(IRandom &rng, int minAlpha, int maxAlpha)
{
    // 完全随机 RGB：三通道各自独立取 [0,255]（用户确认的口径，不做任何过滤 / 约束）
    RgbColor color;
    color.r = rng.nextInt(256);
    color.g = rng.nextInt(256);
    color.b = rng.nextInt(256);

    const int low = clampChannel(std::min(minAlpha, maxAlpha));
    const int high = clampChannel(std::max(minAlpha, maxAlpha));
    color.a = randomInRange(rng, low, high);
    return clampedColor(color);
}

std::string toHexString(const RgbColor &color)
{
    const RgbColor safe = clampedColor(color);
    char buffer[10] = { 0 };
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", safe.r, safe.g, safe.b);
    return std::string(buffer);
}

double perceivedLuminance(const RgbColor &color)
{
    const RgbColor safe = clampedColor(color);
    // sRGB 相对亮度系数（ITU-R BT.601 近似）：整数运算 + 归一化，避免引入颜色库
    const double luma = 0.299 * static_cast<double>(safe.r) + 0.587 * static_cast<double>(safe.g)
                        + 0.114 * static_cast<double>(safe.b);
    return std::max(0.0, std::min(1.0, luma / 255.0));
}

} // namespace whalepet::core
