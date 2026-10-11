#include "view/ui/DrawingShapes.h"

#include <algorithm>
#include <cmath>

namespace whalepet::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

QPolygonF regularPolygon(const QRectF &box, int sides)
{
    const int count = std::max(3, sides);
    const QPointF center = box.center();
    const double radiusX = box.width() * 0.5;
    const double radiusY = box.height() * 0.5;

    QPolygonF polygon;
    polygon.reserve(count);
    for (int i = 0; i < count; ++i) {
        // 第一个顶点朝左（180°）—— 见头文件：尾鳍与躯干的「精确相切」依赖这条约定
        const double angle = kPi + (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(count);
        polygon << QPointF(center.x() + radiusX * std::cos(angle),
                           center.y() + radiusY * std::sin(angle));
    }
    return polygon;
}

QPolygonF trianglePolygon(const QRectF &box)
{
    QPolygonF polygon;
    polygon.reserve(3);
    polygon << QPointF(box.center().x(), box.top())
            << QPointF(box.left(), box.bottom())
            << QPointF(box.right(), box.bottom());
    return polygon;
}

QPolygonF rectanglePolygon(const QRectF &box)
{
    QPolygonF polygon;
    polygon.reserve(4);
    polygon << box.topLeft() << box.topRight() << box.bottomRight() << box.bottomLeft();
    return polygon;
}

} // namespace whalepet::ui
