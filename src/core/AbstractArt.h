#pragma once

// P11 绘画模块之一：**鲸鱼娘抽象画生成器**（纯逻辑，零 Qt）。
//
// 需求（用户 2026-10-11）：「使用随机数算法动态产生配色方案，并基于 QPainter 进行
// 实时画布绘制，需支持连续刷新与视觉变化」。
//
// 分工：
//   * 本文件负责「随机数算法 + 笔触序列」——几何、颜色、线宽、图元种类全部在这里决定；
//   * 视图（src/view/ui/AbstractArtView.h）负责 QPainter 落笔与定时刷新，
//     并把笔触**累积**到离屏 QImage（而不是每帧重放全部笔触）。
//
// 确定性：一切随机都经 core::IRandom 注入 → 同一随机源序列产生同一序列笔触（可单测）。
// 配色：**完全随机 RGB**（见 core/DrawingPalette.h 的口径说明）。

#include "core/DrawingPalette.h"

namespace whalepet::core {

// 抽象画图元种类（视图侧一一映射到 QPainter 的绘制调用）
enum class AbstractShape {
    Circle = 0, // 圆 / 椭圆（可实心或描边）
    Rectangle,  // 矩形
    Triangle,   // 三角形
    Polygon,    // 多边形（5~7 边）
    Line,       // 直线
    Arc,        // 弧线
    Count,
};

// 一笔抽象画：**几何全部为归一化坐标 [0,1]**，与画布像素尺寸解耦
// （画布尺寸变化时同一笔触仍落在同一相对位置）。
struct AbstractStroke {
    AbstractShape shape = AbstractShape::Circle;
    RgbColor color;           // 完全随机 RGB
    float x = 0.5f;           // 中心 / 起点（归一化）
    float y = 0.5f;
    float w = 0.1f;           // 归一化宽（圆为直径）
    float h = 0.1f;           // 归一化高
    float rotation = 0.0f;    // 旋转角（度；矩形 / 三角 / 多边形 / 直线有效）
    int lineWidth = 2;        // 描边线宽（像素；实心图元忽略）
    int sides = 3;            // 多边形边数（Polygon 用 5~7，Triangle 固定 3）
    int arcSpan = 180;        // 弧线张角（度，Arc 用）
    bool filled = false;      // 实心 / 描边
};

// 单个定时器周期的笔触数（「连续刷新」的节奏）与**累计笔触上限**（防止无界增长）
inline constexpr int kAbstractBatchSize = 3;
inline constexpr int kAbstractStrokeCap = 4000;
// 线宽区间（像素）
inline constexpr int kAbstractMinLineWidth = 1;
inline constexpr int kAbstractMaxLineWidth = 12;

// 抽象画生成器：给定随机源逐笔产出，不持有画布状态（状态在视图的离屏位图上）。
class AbstractArtGenerator {
public:
    AbstractArtGenerator(int canvasWidth, int canvasHeight);

    // 下一笔（每次调用推进随机源；同一随机源 → 同一序列）
    AbstractStroke next(IRandom &rng) const;

    int canvasWidth() const { return m_canvasWidth; }
    int canvasHeight() const { return m_canvasHeight; }

private:
    int m_canvasWidth = 1;
    int m_canvasHeight = 1;
};

} // namespace whalepet::core
