#pragma once

// P11 绘画模块之二：**鲸鱼娘线条画生成器**（纯逻辑，零 Qt）。
//
// 需求（用户 2026-10-11）：「利用基础平面几何图形（如圆、矩形、三角形、多边形）的线框
// 组合构建角色轮廓，需实现图形拼接与风格一致性」。
//
// 设计要点（为什么这样能同时满足「拼接」与「风格一致」）：
//   1. **拓扑固定**：部位集合与绘制顺序固定（枚举 LineArtPart），每张图都由同一组部位构成
//      → 「换一张」只是姿态抖动，不会变成另一个角色；
//   2. **拼接由派生保证**：头 / 鳍 / 尾 / 眼 / 水柱全部由**躯干与头的锚点派生**（不独立抖动），
//      因此「相切 / 插入」关系是构造性的（不靠容差凑数），单测可断言到 1e-4；
//   3. **风格一致性**：全部形状共用同一 LineArtStyle（线宽 / 主体色 / 次要色），
//      颜色由**界面注入**（ui::foreground / ui::muted → core 不硬编码任何颜色）；
//   4. 只有装饰物（气泡）是自由随机的——它不参与拼接，不影响轮廓识别。
//
// 确定性：一切抖动经 core::IRandom 注入 → 同一随机源 + 同一种子 → 同一张图（可复现）。

#include "core/DrawingPalette.h"

#include <cstdint>
#include <vector>

namespace whalepet::core {

// 基础平面几何图形（用户点名的四类 + 直线；都是「线框」用法）
enum class LineArtPrimitive {
    Circle = 0, // 圆（头 / 眼 / 气泡）
    Rectangle,  // 矩形（躯干）
    Triangle,   // 三角形（双鳍）
    Polygon,    // 多边形（尾鳍）
    Line,       // 直线（水柱）
    Count,
};

// 角色部位（拓扑固定的唯一真源：顺序即绘制顺序）
enum class LineArtPart {
    Tail = 0,   // 尾鳍（多边形，左边缘切在躯干右边界）
    Body,       // 躯干（矩形）
    Head,       // 头（圆，底部插入躯干上边）
    FinLeft,    // 左鳍（三角，顶点在躯干左上角）
    FinRight,   // 右鳍（三角，顶点在躯干右下角）
    EyeLeft,    // 左眼（圆，落在头圆内）
    EyeRight,   // 右眼（圆，落在头圆内）
    SproutLine, // 水柱（直线，下端接头顶）
    Bubble,     // 气泡（圆，自由随机；不参与拼接）
    Count,
};

// 单个部位（几何全部归一化到 [0,1]，与像素尺寸解耦）
struct LineArtShape {
    LineArtPart part = LineArtPart::Body;
    LineArtPrimitive primitive = LineArtPrimitive::Rectangle;
    float x = 0.5f;       // 中心（归一化）
    float y = 0.5f;
    float w = 0.1f;       // 宽（圆 = 直径）
    float h = 0.1f;       // 高
    float rotation = 0.0f; // 度（直线用：0 = 水平，90 = 垂直）
    int sides = 5;        // Polygon 边数（尾鳍用 5）
    bool accent = false;  // true = 用次要色，false = 用主体色（风格一致性的「两族」口径）
};

// 风格参数：**整张图共用一份**（线宽 / 主体色 / 次要色），故不存在逐形状样式漂移
struct LineArtStyle {
    RgbColor outline;          // 主体线框色（界面注入 ui::foreground）
    RgbColor accent;           // 次要线框色（界面注入 ui::muted）
    int strokeWidth = 2;       // 线宽（像素）

    bool operator==(const LineArtStyle &other) const
    {
        return outline == other.outline && accent == other.accent
               && strokeWidth == other.strokeWidth;
    }
    bool operator!=(const LineArtStyle &other) const { return !(*this == other); }
};

struct LineArtPlan {
    std::vector<LineArtShape> shapes; // 顺序 = 绘制顺序 = LineArtPart 枚举顺序
    LineArtStyle style;               // 本图风格（由调用方注入）
    std::uint32_t seed = 0;           // 本图种子（界面显示 / 复现用）
};

// 抖动幅度（归一化）：决定「同一只鲸鱼娘」的姿态变化范围
inline constexpr float kLineArtBodyJitterX = 0.015f;
inline constexpr float kLineArtBodyJitterY = 0.012f;
inline constexpr float kLineArtBodySizeJitter = 0.02f;
inline constexpr float kLineArtHeadRadiusJitter = 0.012f;
inline constexpr float kLineArtTailJitter = 0.012f;
inline constexpr float kLineArtEyeJitter = 0.004f;

// 生成一张线条画：按固定拓扑产出 LineArtPart::Count 个形状（顺序与枚举一致）。
// style 由调用方注入（颜色来源见 docs/DRAWING.md §4）；seed 只记录在计划里供复现。
LineArtPlan makeLineArt(IRandom &rng, const LineArtStyle &style, std::uint32_t seed);

// 便捷查表：部位 → 期望图元（单测与界面提示共用；越界返回 Count）
LineArtPrimitive primitiveOfPart(LineArtPart part);

// 直线形状的两个端点（x,y 为中心 + w 为长度 + rotation 为角度）——界面绘制与单测共用
void lineEndpoints(const LineArtShape &shape, float *x1, float *y1, float *x2, float *y2);

} // namespace whalepet::core
