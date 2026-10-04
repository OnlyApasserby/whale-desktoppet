#include "view/SpeechBubble.h"

#include "common/PetVisuals.h"

#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>

namespace whalepet {

namespace {
// 需要额外停顿的标点（全角为主，兼顾半角）
const QString &pausePunct()
{
    static const QString punct = QStringLiteral("，。！？；：、…—～,.!?;:");
    return punct;
}
} // namespace

SpeechBubble::SpeechBubble(QWidget *parent)
    : QWidget(parent)
{
    // 透明背景 + 无边框工具窗口：不抢焦点、不进任务栏
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowTitle(QStringLiteral("WhalePet"));

    // 样式由全局样式表按 objectName 命中（project.qss）
    setObjectName(QStringLiteral("SpeechBubble"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 7, 10, 7);
    layout->setSpacing(0);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    m_label->setMaximumWidth(kBubbleMaxWidth);
    layout->addWidget(m_label);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &SpeechBubble::hideLine);

    m_streamTimer = new QTimer(this);
    m_streamTimer->setSingleShot(true);
    connect(m_streamTimer, &QTimer::timeout, this, &SpeechBubble::advanceStream);
}

void SpeechBubble::attachTo(QWidget *anchor)
{
    m_anchor = anchor;
}

void SpeechBubble::setSuppressed(bool suppressed)
{
    m_suppressed = suppressed;
    if (suppressed) {
        hideLine(); // 关闭气泡开关时，正在显示的台词立即消失
    }
}

void SpeechBubble::startStream(const QString &text, int ttlMs)
{
    if (text.isEmpty() || m_suppressed) {
        return;
    }

    // 打断：停掉上一条的流式与隐藏计时，从头重来（多次操作只保留最后一次）
    m_streamTimer->stop();
    m_hideTimer->stop();

    m_fullText = text;
    m_streamIndex = 0;
    m_ttlMs = ttlMs;

    // 先按**整句**预排版定下气泡尺寸，再逐字揭示：
    // 打字过程中尺寸与位置保持稳定，不会逐字抖动。
    m_label->setText(m_fullText);
    adjustSize();
    reposition();
    show();
    raise();

    m_label->setText(QString());
    advanceStream(); // 立即揭示第 1 个字，并按它决定下一字的间隔
}

void SpeechBubble::showLine(const QString &text, int ttlMs)
{
    startStream(text, ttlMs);
    if (m_fullText.isEmpty()) {
        return;
    }
    // 不走流式：一次揭示全文
    m_streamTimer->stop();
    m_streamIndex = m_fullText.size();
    m_label->setText(m_fullText);
    reposition();
    finishStream();
}

void SpeechBubble::advanceStream()
{
    if (m_streamIndex >= m_fullText.size()) {
        finishStream();
        return;
    }

    ++m_streamIndex;
    m_label->setText(m_fullText.left(m_streamIndex));

    if (m_streamIndex >= m_fullText.size()) {
        finishStream();
        return;
    }

    // 标点后多停一拍，读起来更自然
    const QChar last = m_fullText.at(m_streamIndex - 1);
    const int gap = isPausePunct(last) ? (kStreamCharIntervalMs + kStreamPunctPauseMs)
                                       : kStreamCharIntervalMs;
    m_streamTimer->start(gap);
}

void SpeechBubble::finishStream()
{
    m_streamTimer->stop();
    // TTL 从流式**结束**起算（而非从出现起算），长句不会还没打完就消失
    m_hideTimer->start(m_ttlMs > 0 ? m_ttlMs : kBubbleDefaultTtlMs);
}

void SpeechBubble::hideLine()
{
    m_hideTimer->stop();
    m_streamTimer->stop();
    m_label->clear();
    m_fullText.clear();
    m_streamIndex = 0;
    hide();
}

QString SpeechBubble::displayedText() const
{
    return m_label != nullptr ? m_label->text() : QString();
}

bool SpeechBubble::isPausePunct(QChar ch)
{
    return pausePunct().contains(ch);
}

// 圆角矩形背景框：**取值全部来自全局样式表** `#SpeechBubble`
// （resources/qt-ui/project.qss：background-color #000000 / border 1px #3C3C3C /
//  border-radius 7px），本类不写任何颜色字面量、不调用 setStyleSheet、不新增 CSS 类。
//
// 为什么必须自绘这一帧：SpeechBubble 是「无边框 + WA_TranslucentBackground」的
// **QWidget 子类顶层窗口**。Qt 不会把样式表的背景 / 边框自动画到自定义 QWidget 上
// （实测：不转发 PE_Widget 时中心像素 alpha = 0，即气泡无底；只加 WA_StyledBackground 同样为 0），
// 故按 Qt 官方口径在 paintEvent 中把 PE_Widget 交给 style() 绘制 —— 圆角、边框、底色
// 全部由 QSS 规则决定，圆角外的区域保持透明（不遮挡桌面与立绘）。
void SpeechBubble::paintEvent(QPaintEvent * /*event*/)
{
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void SpeechBubble::reposition()
{
    if (m_anchor == nullptr) {
        return;
    }
    if (!m_anchor->isVisible()) {
        hideLine();
        return;
    }

    // 立绘所在窗口矩形（= 需要避让的区域）。气泡是**独立顶层窗口**，无法用「降低 z 序」
    // 实现不遮挡（降 z 会被立绘窗口 / 桌面盖住），因此统一用「偏移方向」保证：
    // 气泡整体贴在该矩形**之外**（上方或下方，留 kBubbleGapPx），任何一侧都不与立绘重叠。
    const QRect anchorRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size());

    // 水平：与立绘窗口居中对齐（长文本靠 QLabel 换行控制宽度，不顶出屏幕）
    int x = anchorRect.x() + (anchorRect.width() - width()) / 2;

    // 垂直：优先挂在立绘**上方**；上方放不下则改挂**下方**；两侧都放不下（极小屏 / 超长文本）
    // 时取空间较大的一侧，并把气泡整体夹回可用区域（此时才可能出现重叠，属屏幕尺寸兜底）。
    const int aboveY = anchorRect.top() - height() - kBubbleGapPx;
    const int belowY = anchorRect.bottom() + 1 + kBubbleGapPx;
    int y = aboveY;

    QScreen *screen = QGuiApplication::screenAt(anchorRect.center());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        const QRect area = screen->availableGeometry();
        const int m = kBubbleScreenMarginPx;
        const int topLimit = area.top() + m;
        const int bottomLimit = area.bottom() - m;

        if (aboveY >= topLimit) {
            y = aboveY; // 上方能完整放下 → 保持上方（不与立绘重叠）
        } else if (belowY + height() <= bottomLimit) {
            y = belowY; // 上方不足但下方放得下 → 翻到立绘下方（仍不重叠）
        } else {
            // 两侧都放不下：按空间较大的一侧摆放，随后夹回屏内
            const int spaceAbove = anchorRect.top() - topLimit;
            const int spaceBelow = bottomLimit - anchorRect.bottom();
            y = (spaceAbove >= spaceBelow) ? aboveY : belowY;
        }

        if (x + width() > area.right() - m) {
            x = area.right() - m - width();
        }
        if (x < area.left() + m) {
            x = area.left() + m;
        }
        if (y < topLimit) {
            y = topLimit;
        }
        if (y + height() > bottomLimit) {
            y = bottomLimit - height();
        }
    }

    move(x, y);
}

} // namespace whalepet
