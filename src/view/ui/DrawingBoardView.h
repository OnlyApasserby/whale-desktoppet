#pragma once

// P11 绘画模块之三：**用户交互式简易画板**（工作台页之一）。
//
// 需求口径：「画布为纯白背景，外围控件严格遵循现有 qt-ui 样式，并实现屏幕取色功能
// （原话：参考 references 下 src 中 WinEase 项目的屏幕选色实现；该参考拷贝已按用户指示
//   于 2026-10-11 移除 —— 属用户本人的个人项目，本实现为独立编写，见 docs/DRAWING.md §5.1）」。
//
// 三条实现约定（与 qt-ui 不冲突的做法）：
//   1. **纯白画布**：白底由画布自身的 QImage 提供，paintEvent 整块绘制 —— 全局样式表里
//      的 QWidget 背景（黑）不会露出来，因此**不需要**任何局部 setStyleSheet
//      （qt-ui 明令禁止子控件局部样式；这里根本不用它）；
//   2. 外围控件（工具 / 粗细 / 按钮 / 色块 / 状态）全部沿用全局样式表既有取值，
//      本页不新增颜色、不改动任何 default.qss 规则；
//   3. **屏幕取色**：ScreenColorPickerOverlay（Win32 1×1 BitBlt + 系统 Magnification API），
//      取到的颜色 → 画笔颜色 + 「最近取色」列表（可选回填）；覆盖层里同时把
//      #RRGGBB 写入剪贴板。

#include "core/CanvasBoard.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace whalepet::ui {

class ColorSwatch;
class ScreenColorPickerOverlay;

// 「最近取色」保留的条数
inline constexpr int kRecentColorCount = 5;

// 画布控件（固定尺寸；白底 + 笔触累积位图，工具 / 粗细由页面设置）
class DrawingBoardCanvas : public QWidget {
    Q_OBJECT
public:
    explicit DrawingBoardCanvas(QWidget *parent = nullptr);

    void setBrushColor(const core::RgbColor &color);
    void setBrushWidth(int width);
    void setEraser(bool eraser);

    core::RgbColor brushColor() const { return m_brushColor; }
    int brushWidth() const { return m_brushWidth; }
    bool eraser() const { return m_eraser; }

    bool undo();
    void clearBoard();

    const QImage &image() const { return m_image; }
    const core::CanvasModel &model() const { return m_model; }

    static constexpr int kCanvasWidth = 480;
    static constexpr int kCanvasHeight = 360;

signals:
    void changed(); // 笔触 / 撤销 / 清空后发出（页面刷新状态用）

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    core::CanvasPoint toCanvasPoint(const QPointF &position) const;
    QColor strokeColor() const;
    void renderAll();
    void paintSegment(const core::CanvasPoint &from, const core::CanvasPoint &to);

    core::CanvasModel m_model;
    QImage m_image; // 白底 + 已落笔的笔触（撤销 / 清空时按模型重放）
    core::RgbColor m_brushColor;
    int m_brushWidth = core::kCanvasDefaultBrushWidth;
    bool m_eraser = false;
};

// 画板页：画布 + 工具 / 粗细 / 当前色 / 最近取色 / 屏幕取色 / 撤销 / 清空 / 保存
class DrawingBoardView : public QWidget {
    Q_OBJECT
public:
    explicit DrawingBoardView(QWidget *parent = nullptr);

    DrawingBoardCanvas *canvas() const { return m_canvas; }
    const QList<core::RgbColor> &recentColors() const { return m_recentColors; }

    // 采用一个取到的颜色：设为画笔色 + 进入「最近取色」列表（按钮与单测共用同一条路径）
    void applyPickedColor(const core::RgbColor &color);

private:
    void onPickColor();
    void onSave();
    void refreshStatus();
    void refreshRecent();
    void setCurrentColor(const core::RgbColor &color);

    DrawingBoardCanvas *m_canvas = nullptr;
    ColorSwatch *m_current = nullptr;
    QList<ColorSwatch *> m_recentSwatches;
    QList<core::RgbColor> m_recentColors;
    QComboBox *m_tool = nullptr;
    QComboBox *m_width = nullptr;
    QPushButton *m_pick = nullptr;
    QLabel *m_status = nullptr;
    ScreenColorPickerOverlay *m_picker = nullptr; // 懒建（首次点「屏幕取色」）
};

} // namespace whalepet::ui
