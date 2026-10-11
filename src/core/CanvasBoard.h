#pragma once

// P11 绘画模块之三：**用户交互式简易画板**的笔触模型（纯逻辑，零 Qt）。
//
// 需求（用户 2026-10-11）：「画布为纯白背景，外围控件严格遵循现有 qt-ui 样式」。
// 分工：
//   * 本文件只管「笔触数据 + 增删语义」（开笔 / 加点 / 收笔 / 撤销 / 清空 / 包围盒），
//     不碰任何 Qt 类型 —— 因此画板行为可脱界面单测；
//   * 渲染（画到离屏 QImage）与白底取值在视图层（src/view/ui/DrawingBoardView.h），
//     纯白画布由 project.qss 的项目控件规则提供（不写局部样式表）。
//
// 坐标系：画布像素坐标（x 向右、y 向下、原点在画布左上角），点在建笔时即被钳制到画布内。

#include "core/DrawingPalette.h"

#include <vector>

namespace whalepet::core {

struct CanvasPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// 一笔画板笔触（画笔或橡皮；橡皮只影响渲染时的合成方式，数据形状与画笔一致）
struct CanvasStroke {
    std::vector<CanvasPoint> points;
    RgbColor color;      // 画笔色；橡皮忽略
    int width = 3;       // 线宽（像素，已钳制到 [1, kCanvasMaxBrushWidth]）
    bool eraser = false; // true = 橡皮擦除
};

// 笔触包围盒（像素，已含线宽半径）——视图据此做增量重绘
struct CanvasBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool valid = false;
};

inline constexpr int kCanvasMinBrushWidth = 1;
inline constexpr int kCanvasMaxBrushWidth = 64;
inline constexpr int kCanvasDefaultBrushWidth = 3;

// 画板笔触模型：先 begin（落笔）→ 可多次 extend（移动）→ end（抬笔）后该笔进入历史。
// 语义细节（单测逐条覆盖）：
//   * begin 时若已有一笔正在进行，先自动 end（不丢数据）；
//   * 未 begin 直接 extend / end：忽略（不产生半笔）；
//   * 只有抬笔（end）后的笔才可被 undo —— 撤销不会破坏正在画的一笔；
//   * clear 清空历史并丢弃进行中的笔。
class CanvasModel {
public:
    CanvasModel(int canvasWidth, int canvasHeight);

    void begin(const CanvasPoint &point, const RgbColor &color, int width, bool eraser);
    void extend(const CanvasPoint &point);
    void end();

    bool drawing() const { return m_drawing; }
    bool undo();
    void clear();

    const std::vector<CanvasStroke> &strokes() const { return m_strokes; }
    const CanvasStroke &activeStroke() const { return m_active; }

    int canvasWidth() const { return m_canvasWidth; }
    int canvasHeight() const { return m_canvasHeight; }

    // 单笔包围盒（像素；含线宽半径；空笔触 valid = false）
    static CanvasBounds boundsOf(const CanvasStroke &stroke);

    // 把点钳制到画布内（[-0.0, width-1] × [0, height-1]）
    CanvasPoint clampPoint(const CanvasPoint &point) const;

private:
    int m_canvasWidth = 1;
    int m_canvasHeight = 1;
    std::vector<CanvasStroke> m_strokes;
    CanvasStroke m_active;
    bool m_drawing = false;
};

} // namespace whalepet::core
