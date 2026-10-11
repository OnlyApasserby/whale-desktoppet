#pragma once

// 随机闪现（random flicker，EX6）的**纯逻辑**：从屏幕可用区域里抽一个「安全落点」，
// 以及下一次闪现的随机等待时长。
//
// 零 Qt 依赖（只用整型几何 + core::IRandom），可脱离界面单测。
// 设计说明见 docs/PRESENTATION.md §3.2；视图接线见 src/view/PetWindow.cpp。
//
// 「安全落点」= 屏幕可用区域四边各内缩 marginPx 之后仍然合法的窗口左上角坐标，因此：
//   * 不越出可用区域（任务栏 / 多屏边界已由 availableGeometry 扣除）；
//   * 不进入四条边框的**贴边带**（见 docs/PRESENTATION.md §3.1）——窗口一旦被判定为贴边，
//     立绘会立刻换成探头立绘、且贴边分支只保留呼吸缩放，闪现的淡出淡入将完全不可见；
//   * 与当前位置至少相距 minDistancePx（避免「几乎没动」被读成卡顿）。

#include "core/IRandom.h"

namespace whalepet::core {

// 纯整型矩形（避免引入 Qt；语义与 QRect 一致：(x + w) / (y + h) 为**排他**边界）
struct FlickerRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool isEmpty() const { return w <= 0 || h <= 0; }
};

struct FlickerPoint {
    int x = 0;
    int y = 0;
};

// 一次落点抽样的结果
struct FlickerTarget {
    bool ok = false;  // false = 没有可行落点（屏幕比窗口 + 边距还小）→ 调用方放弃本次闪现
    FlickerPoint pos; // 窗口左上角目标坐标（仅 ok == true 时有效）
};

// 安全落点矩形：可用区域四边各内缩 marginPx。内缩后为空 → 返回空矩形（调用方放弃）。
inline FlickerRect flickerSafeRect(int areaX, int areaY, int areaW, int areaH, int marginPx)
{
    const int m = marginPx > 0 ? marginPx : 0;
    return FlickerRect{ areaX + m, areaY + m, areaW - 2 * m, areaH - 2 * m };
}

// 窗口（左上角 + 尺寸）是否**完整**落在安全矩形内
inline bool flickerPointIsSafe(int x, int y, int winW, int winH, FlickerRect safe)
{
    if (safe.isEmpty() || winW <= 0 || winH <= 0) {
        return false;
    }
    return x >= safe.x && y >= safe.y && x + winW <= safe.x + safe.w && y + winH <= safe.y + safe.h;
}

// 抽一个落点：
//   1. 在安全矩形内均匀抽取窗口左上角的合法位置，最多尝试 maxAttempts 次，
//      要求与当前位置的**切比雪夫距离**（单轴最大值）>= minDistancePx；
//   2. 全部尝试都太近时退化为**对角镜像**（把当前位置相对安全矩形中心镜像，并夹回合法范围），
//      保证「恒合法、且不可能停在原地」；镜像同样贴到边界时退化为该角，仍不越界。
// rng 为空 / 安全矩形不足以容纳窗口 → ok = false（调用方放弃本次闪现，不猜坐标）。
inline FlickerTarget pickFlickerTarget(int curX, int curY, int winW, int winH, FlickerRect safe,
                                      int minDistancePx, int maxAttempts, IRandom *rng)
{
    FlickerTarget out;
    if (rng == nullptr || !flickerPointIsSafe(safe.x, safe.y, winW, winH, safe)) {
        return out;
    }

    // 左上角取值区间（含端点）：保证「左上角 + 窗口尺寸」仍在安全矩形内
    const int maxX = safe.x + safe.w - winW;
    const int maxY = safe.y + safe.h - winH;
    const int spanX = maxX - safe.x + 1;
    const int spanY = maxY - safe.y + 1;

    const int attempts = maxAttempts > 0 ? maxAttempts : 1;
    const int minDist = minDistancePx > 0 ? minDistancePx : 0;
    for (int i = 0; i < attempts; ++i) {
        const int x = safe.x + rng->nextInt(spanX);
        const int y = safe.y + rng->nextInt(spanY);
        const int dx = x > curX ? x - curX : curX - x;
        const int dy = y > curY ? y - curY : curY - y;
        if (dx >= minDist || dy >= minDist) {
            out.ok = true;
            out.pos = FlickerPoint{ x, y };
            return out;
        }
    }

    const int mirrorX = safe.x + maxX - curX;
    const int mirrorY = safe.y + maxY - curY;
    out.ok = true;
    out.pos = FlickerPoint{ mirrorX < safe.x ? safe.x : (mirrorX > maxX ? maxX : mirrorX),
                            mirrorY < safe.y ? safe.y : (mirrorY > maxY ? maxY : mirrorY) };
    return out;
}

// 下一次闪现的等待时长（毫秒）：[minMs, maxMs] 闭区间内均匀抽样。
// 区间非法 / rng 为空 → 返回 minMs（保持确定性，绝不静默退化为 0 造成忙循环）。
inline int nextFlickerDelayMs(IRandom *rng, int minMs, int maxMs)
{
    if (minMs < 0) {
        minMs = 0;
    }
    if (maxMs < minMs) {
        maxMs = minMs;
    }
    if (rng == nullptr) {
        return minMs;
    }
    const int span = maxMs - minMs;
    return (span > 0) ? (minMs + rng->nextInt(span + 1)) : minMs;
}

} // namespace whalepet::core
