#pragma once

// P11 绘画模块：**基础平面几何图形**的顶点计算（抽象画与线条画共用）。
//
// 约定（决定「图形拼接」能否对齐）：
//   * 所有图形都用一个「中心 + 宽高矩形」（QRectF）描述，绘制时按该矩形生成顶点，
//     因此 core 层的几何（归一化中心 + 尺寸）与这里的顶点一一对应；
//   * `regularPolygon()` 的第一个顶点**固定朝左**（角度 180°）→ 半径框的左边界恰好等于
//     `x - w/2`。线条画的尾鳍正是靠这条约定**精确**切在躯干右边界上（不是近似相切）；
//   * `trianglePolygon()` 顶点朝上、底边与矩形底边重合 —— 线条画的双鳍锚点依赖它。

#include <QPolygonF>
#include <QRectF>

namespace whalepet::ui {

// 正 sides 边形（内接于给定矩形；第一个顶点朝左）
QPolygonF regularPolygon(const QRectF &box, int sides);

// 等腰三角形（顶点在上边中点，底边与矩形底边重合）
QPolygonF trianglePolygon(const QRectF &box);

// 矩形的四个顶点（左上起顺时针）
QPolygonF rectanglePolygon(const QRectF &box);

} // namespace whalepet::ui
