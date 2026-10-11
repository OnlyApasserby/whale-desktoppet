#include "core/AbstractArt.h"

#include <algorithm>

namespace whalepet::core {

namespace {

float randomRange(IRandom &rng, float low, float high)
{
    if (high <= low) {
        return low;
    }
    const double t = rng.next01();
    return static_cast<float>(low + (high - low) * t);
}

int randomInt(IRandom &rng, int low, int high)
{
    if (high <= low) {
        return low;
    }
    return low + rng.nextInt(high - low + 1);
}

} // namespace

AbstractArtGenerator::AbstractArtGenerator(int canvasWidth, int canvasHeight)
    : m_canvasWidth(std::max(1, canvasWidth))
    , m_canvasHeight(std::max(1, canvasHeight))
{
}

AbstractStroke AbstractArtGenerator::next(IRandom &rng) const
{
    AbstractStroke stroke;

    // 1) 图元种类：全量随机（含三角形 / 多边形 —— 与线条画共用「基础平面几何图形」的词汇）
    stroke.shape = static_cast<AbstractShape>(randomInt(rng, 0, static_cast<int>(AbstractShape::Count) - 1));

    // 2) 完全随机 RGB（用户口径）；alpha 半透明以便叠加演化
    stroke.color = randomRgb(rng);

    // 3) 几何：中心在归一化画布内，尺寸偏小（多笔叠加才形成整体画面）
    const float maxW = (stroke.shape == AbstractShape::Line) ? 0.42f : 0.30f;
    const float maxH = (stroke.shape == AbstractShape::Line) ? 0.42f : 0.30f;
    stroke.w = randomRange(rng, 0.04f, maxW);
    stroke.h = randomRange(rng, 0.04f, maxH);

    // 让整笔（含中心偏移的半宽半高）落在 [0,1] 内：中心只在可用范围内取
    const float halfW = stroke.w * 0.5f;
    const float halfH = stroke.h * 0.5f;
    stroke.x = randomRange(rng, halfW, 1.0f - halfW);
    stroke.y = randomRange(rng, halfH, 1.0f - halfH);

    // 4) 角度 / 边数 / 张角 / 线宽
    stroke.rotation = randomRange(rng, 0.0f, 360.0f);
    stroke.sides = randomInt(rng, 5, 7);
    stroke.arcSpan = randomInt(rng, 60, 300);
    stroke.lineWidth = randomInt(rng, kAbstractMinLineWidth, kAbstractMaxLineWidth);

    // 5) 实心 / 描边：圆与矩形较常实心（形成色块），其余以线框为主
    const bool solidCapable = (stroke.shape == AbstractShape::Circle
                               || stroke.shape == AbstractShape::Rectangle
                               || stroke.shape == AbstractShape::Triangle
                               || stroke.shape == AbstractShape::Polygon);
    stroke.filled = solidCapable && (rng.next01() < 0.45);

    return stroke;
}

} // namespace whalepet::core
