#include "core/LineArt.h"

#include <algorithm>
#include <cmath>

namespace whalepet::core {

namespace {

// ---------------------------------------------------------------------------
// 骨架基线（归一化，y 向下）——**全部部位由锚点派生**，因此拼接是构造性的
// ---------------------------------------------------------------------------
constexpr float kBodyCenterX = 0.52f;
constexpr float kBodyCenterY = 0.60f;
constexpr float kBodyWidth = 0.44f;
constexpr float kBodyHeight = 0.30f;

constexpr float kHeadRadius = 0.14f;
constexpr float kHeadOverlap = 0.04f;  // 头圆插入躯干上边的深度（> 0 ⇒ 必然拼接）
constexpr float kHeadOffsetX = -0.10f; // 头相对躯干中心的水平偏移（鲸头偏左）
constexpr float kHeadJitterX = 0.012f;

constexpr float kEyeRadius = 0.025f;
constexpr float kEyeSpreadX = 0.055f;
constexpr float kEyeOffsetY = -0.03f;

constexpr float kFinWidth = 0.14f;
constexpr float kFinHeight = 0.12f;

constexpr float kTailWidth = 0.16f;
constexpr float kTailHeight = 0.20f;
constexpr int kTailSides = 5;

constexpr float kSproutLength = 0.11f;

// 气泡（装饰）：自由随机，不参与拼接
constexpr float kBubbleXMin = 0.30f;
constexpr float kBubbleXMax = 0.82f;
constexpr float kBubbleYMin = 0.07f;
constexpr float kBubbleYMax = 0.24f;
constexpr float kBubbleSizeMin = 0.04f;
constexpr float kBubbleSizeMax = 0.09f;

float jitter(IRandom &rng, float amplitude)
{
    if (amplitude <= 0.0f) {
        return 0.0f;
    }
    return static_cast<float>((rng.next01() * 2.0 - 1.0) * amplitude);
}

float randomRange(IRandom &rng, float low, float high)
{
    if (high <= low) {
        return low;
    }
    return static_cast<float>(low + (high - low) * rng.next01());
}

void setPart(LineArtShape &shape, LineArtPart part, LineArtPrimitive primitive, bool accent)
{
    shape.part = part;
    shape.primitive = primitive;
    shape.accent = accent;
}

// 圆（x,y = 圆心；w = h = 直径）
LineArtShape makeCircle(LineArtPart part, float cx, float cy, float diameter, bool accent)
{
    LineArtShape shape;
    setPart(shape, part, LineArtPrimitive::Circle, accent);
    shape.x = cx;
    shape.y = cy;
    shape.w = diameter;
    shape.h = diameter;
    return shape;
}

} // namespace

LineArtPrimitive primitiveOfPart(LineArtPart part)
{
    switch (part) {
    case LineArtPart::Tail:
        return LineArtPrimitive::Polygon;
    case LineArtPart::Body:
        return LineArtPrimitive::Rectangle;
    case LineArtPart::Head:
        return LineArtPrimitive::Circle;
    case LineArtPart::FinLeft:
    case LineArtPart::FinRight:
        return LineArtPrimitive::Triangle;
    case LineArtPart::EyeLeft:
    case LineArtPart::EyeRight:
    case LineArtPart::Bubble:
        return LineArtPrimitive::Circle;
    case LineArtPart::SproutLine:
        return LineArtPrimitive::Line;
    case LineArtPart::Count:
        break;
    }
    return LineArtPrimitive::Count;
}

void lineEndpoints(const LineArtShape &shape, float *x1, float *y1, float *x2, float *y2)
{
    const double radians = shape.rotation * 3.14159265358979323846 / 180.0;
    const float halfX = static_cast<float>(std::cos(radians)) * shape.w * 0.5f;
    const float halfY = static_cast<float>(std::sin(radians)) * shape.w * 0.5f;
    if (x1 != nullptr) {
        *x1 = shape.x - halfX;
    }
    if (y1 != nullptr) {
        *y1 = shape.y - halfY;
    }
    if (x2 != nullptr) {
        *x2 = shape.x + halfX;
    }
    if (y2 != nullptr) {
        *y2 = shape.y + halfY;
    }
}

LineArtPlan makeLineArt(IRandom &rng, const LineArtStyle &style, std::uint32_t seed)
{
    LineArtPlan plan;
    plan.style = style;
    plan.seed = seed;
    plan.shapes.reserve(static_cast<std::size_t>(LineArtPart::Count));

    // 1) 躯干（唯一的自由锚点：位置与尺寸一起抖动）
    const float bodyW = kBodyWidth + jitter(rng, kLineArtBodySizeJitter);
    const float bodyH = kBodyHeight + jitter(rng, kLineArtBodySizeJitter);
    const float bodyCx = kBodyCenterX + jitter(rng, kLineArtBodyJitterX);
    const float bodyCy = kBodyCenterY + jitter(rng, kLineArtBodyJitterY);
    const float bodyLeft = bodyCx - bodyW * 0.5f;
    const float bodyRight = bodyCx + bodyW * 0.5f;
    const float bodyTop = bodyCy - bodyH * 0.5f;
    const float bodyBottom = bodyCy + bodyH * 0.5f;

    // 2) 头：**底部插入躯干上边** kHeadOverlap（拼接由这里保证）
    const float headRadius = kHeadRadius + jitter(rng, kLineArtHeadRadiusJitter);
    const float headCx = bodyCx + kHeadOffsetX + jitter(rng, kHeadJitterX);
    const float headCy = bodyTop + kHeadOverlap - headRadius;

    // 3) 尾：左边缘切在躯干右边界、垂直中心与躯干一致
    const float tailW = kTailWidth + jitter(rng, kLineArtTailJitter);
    const float tailH = kTailHeight + jitter(rng, kLineArtTailJitter);
    const float tailCx = bodyRight + tailW * 0.5f;
    const float tailCy = bodyCy;

    // 绘制顺序 = 枚举顺序（拓扑固定）
    LineArtShape tail;
    setPart(tail, LineArtPart::Tail, LineArtPrimitive::Polygon, false);
    tail.x = tailCx;
    tail.y = tailCy;
    tail.w = tailW;
    tail.h = tailH;
    tail.sides = kTailSides;
    plan.shapes.push_back(tail);

    LineArtShape body;
    setPart(body, LineArtPart::Body, LineArtPrimitive::Rectangle, false);
    body.x = bodyCx;
    body.y = bodyCy;
    body.w = bodyW;
    body.h = bodyH;
    plan.shapes.push_back(body);

    plan.shapes.push_back(makeCircle(LineArtPart::Head, headCx, headCy, headRadius * 2.0f, false));

    // 4) 双鳍：顶点分别落在躯干左上角 / 右下角（拼接）
    LineArtShape finLeft;
    setPart(finLeft, LineArtPart::FinLeft, LineArtPrimitive::Triangle, true);
    finLeft.x = bodyLeft;
    finLeft.y = bodyTop + kFinHeight * 0.5f;
    finLeft.w = kFinWidth;
    finLeft.h = kFinHeight;
    plan.shapes.push_back(finLeft);

    LineArtShape finRight;
    setPart(finRight, LineArtPart::FinRight, LineArtPrimitive::Triangle, true);
    finRight.x = bodyRight;
    finRight.y = bodyBottom - kFinHeight * 0.5f;
    finRight.w = kFinWidth;
    finRight.h = kFinHeight;
    plan.shapes.push_back(finRight);

    // 5) 双眼：落在头圆内（由头心派生 + 小幅抖动）
    const float eyeRadius = kEyeRadius + jitter(rng, kLineArtEyeJitter);
    const float eyeLeftCx = headCx - kEyeSpreadX + jitter(rng, kLineArtEyeJitter);
    const float eyeRightCx = headCx + kEyeSpreadX + jitter(rng, kLineArtEyeJitter);
    const float eyeCy = headCy + kEyeOffsetY + jitter(rng, kLineArtEyeJitter);
    plan.shapes.push_back(makeCircle(LineArtPart::EyeLeft, eyeLeftCx, eyeCy, eyeRadius * 2.0f, true));
    plan.shapes.push_back(makeCircle(LineArtPart::EyeRight, eyeRightCx, eyeCy, eyeRadius * 2.0f, true));

    // 6) 水柱：下端正好接在头顶（拼接）
    LineArtShape sprout;
    setPart(sprout, LineArtPart::SproutLine, LineArtPrimitive::Line, true);
    sprout.w = kSproutLength;
    sprout.rotation = 90.0f; // 竖直向上
    sprout.x = headCx;
    sprout.y = (headCy - headRadius) - kSproutLength * 0.5f;
    plan.shapes.push_back(sprout);

    // 7) 气泡（唯一自由随机的部位：不参与拼接，不影响轮廓识别）
    const float bubbleSize = randomRange(rng, kBubbleSizeMin, kBubbleSizeMax);
    plan.shapes.push_back(makeCircle(LineArtPart::Bubble,
                                     randomRange(rng, kBubbleXMin, kBubbleXMax),
                                     randomRange(rng, kBubbleYMin, kBubbleYMax), bubbleSize, true));

    return plan;
}

} // namespace whalepet::core
