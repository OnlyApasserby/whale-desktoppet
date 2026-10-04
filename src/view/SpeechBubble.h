#pragma once

// SpeechBubble：台词气泡（docs/PRESENTATION.md §4）。
//
// 独立无边框子窗口（Qt::Tool），跟随立绘位置，到时自动消失。
// 外观完全由全局样式表 `#SpeechBubble` 规则决定（resources/qt-ui/project.qss：
// 黑底 + 1px 边框 + 7px 圆角），本类不写任何颜色字面量、不调用 setStyleSheet；
// 圆角矩形背景框由 paintEvent 转发 `QStyle::PE_Widget` 绘制（见 .cpp 说明）。
//
// 流式输出（P2 增补，见 docs/ROADMAP-P2.md §B）：
//   - 逐字揭示，节奏由 kStreamCharIntervalMs / kStreamPunctPauseMs 控制；
//   - 新台词**打断**上一条并从头开始（多次操作只保留最后一次的台词）；
//   - 隐藏计时从**流式结束后**起算，长句不会没打完就消失；
//   - 尺寸按整句预排版后固定，打字过程中气泡不抖动。

#include <QWidget>

class QLabel;
class QTimer;

namespace whalepet {

class SpeechBubble : public QWidget {
    Q_OBJECT
public:
    explicit SpeechBubble(QWidget *parent = nullptr);

    // 跟随目标（立绘所在窗口）
    void attachTo(QWidget *anchor);

    // 不流式：一次性显示整条台词（测试/兜底路径）
    void showLine(const QString &text, int ttlMs = 0);
    // 流式：逐字揭示；若已在流式中则立即打断并从头开始
    void startStream(const QString &text, int ttlMs = 0);
    void hideLine();

    // P6 设置项 bubble_enabled：关闭后不再显示任何台词（当前气泡立即隐藏）。
    // 门控在本类内部完成，调用方（Presenter / 状态机）无需感知。
    void setSuppressed(bool suppressed);
    bool suppressed() const { return m_suppressed; }

    bool bubbleVisible() const { return isVisible(); }
    bool streamFinished() const { return m_streamIndex >= m_fullText.size(); }
    // 诊断/单测：当前已揭示的文本
    QString displayedText() const;

    // 按锚点重新定位（立绘移动时调用）
    void reposition();

protected:
    // 绘制圆角矩形背景框（取值全部来自全局样式表 `#SpeechBubble`，见 .cpp）。
    // 自定义 QWidget 子类不会自动应用样式表的背景 / 边框，必须在此转发。
    void paintEvent(QPaintEvent *event) override;

private:
    void advanceStream();
    void finishStream();
    static bool isPausePunct(QChar ch);

    QWidget *m_anchor = nullptr;
    QLabel *m_label = nullptr;
    QTimer *m_hideTimer = nullptr;
    QTimer *m_streamTimer = nullptr;

    QString m_fullText;      // 本条完整台词
    int m_streamIndex = 0;   // 已揭示的字符数
    int m_ttlMs = 0;         // 流式结束后的停留时长（<=0 用 kBubbleDefaultTtlMs）
    bool m_suppressed = false; // bubble_enabled == false 时抑制全部台词
};

} // namespace whalepet
