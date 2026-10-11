#include "core/CanvasBoard.h"

#include <algorithm>

namespace whalepet::core {

namespace {

int clampBrushWidth(int width)
{
    return std::max(kCanvasMinBrushWidth, std::min(kCanvasMaxBrushWidth, width));
}

} // namespace

CanvasModel::CanvasModel(int canvasWidth, int canvasHeight)
    : m_canvasWidth(std::max(1, canvasWidth))
    , m_canvasHeight(std::max(1, canvasHeight))
{
}

CanvasPoint CanvasModel::clampPoint(const CanvasPoint &point) const
{
    CanvasPoint clamped;
    clamped.x = std::max(0.0f, std::min(static_cast<float>(m_canvasWidth - 1), point.x));
    clamped.y = std::max(0.0f, std::min(static_cast<float>(m_canvasHeight - 1), point.y));
    return clamped;
}

void CanvasModel::begin(const CanvasPoint &point, const RgbColor &color, int width, bool eraser)
{
    if (m_drawing) {
        end(); // 上一笔没抬笔：先收笔（不丢数据）
    }
    m_active = CanvasStroke();
    m_active.color = clampedColor(color);
    m_active.width = clampBrushWidth(width);
    m_active.eraser = eraser;
    m_active.points.push_back(clampPoint(point));
    m_drawing = true;
}

void CanvasModel::extend(const CanvasPoint &point)
{
    if (!m_drawing) {
        return; // 没落笔就不产生半笔
    }
    const CanvasPoint clamped = clampPoint(point);
    // 同一像素内的重复点不入库（鼠标停留会刷出成百上千个同点，白白放大内存与重绘量）
    const CanvasPoint &last = m_active.points.back();
    if (clamped.x == last.x && clamped.y == last.y) {
        return;
    }
    m_active.points.push_back(clamped);
}

void CanvasModel::end()
{
    if (!m_drawing) {
        return;
    }
    m_drawing = false;
    if (!m_active.points.empty()) {
        m_strokes.push_back(m_active);
    }
    m_active = CanvasStroke();
}

bool CanvasModel::undo()
{
    if (m_drawing) {
        return false; // 进行中的一笔不受撤销影响（由调用方决定是否先 end）
    }
    if (m_strokes.empty()) {
        return false;
    }
    m_strokes.pop_back();
    return true;
}

void CanvasModel::clear()
{
    m_strokes.clear();
    m_active = CanvasStroke();
    m_drawing = false;
}

CanvasBounds CanvasModel::boundsOf(const CanvasStroke &stroke)
{
    CanvasBounds bounds;
    if (stroke.points.empty()) {
        return bounds;
    }
    const float radius = static_cast<float>(std::max(kCanvasMinBrushWidth, stroke.width)) * 0.5f;
    float minX = stroke.points.front().x;
    float maxX = minX;
    float minY = stroke.points.front().y;
    float maxY = minY;
    for (const CanvasPoint &point : stroke.points) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    bounds.minX = minX - radius;
    bounds.minY = minY - radius;
    bounds.maxX = maxX + radius;
    bounds.maxY = maxY + radius;
    bounds.valid = true;
    return bounds;
}

} // namespace whalepet::core
