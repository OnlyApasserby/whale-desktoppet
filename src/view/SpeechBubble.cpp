#include "view/SpeechBubble.h"

#include "common/PetVisuals.h"

#include <QApplication>
#include <QLabel>
#include <QScreen>
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

void SpeechBubble::reposition()
{
    if (m_anchor == nullptr) {
        return;
    }
    if (!m_anchor->isVisible()) {
        hideLine();
        return;
    }

    const QPoint anchorTopLeft = m_anchor->mapToGlobal(QPoint(0, 0));
    int x = anchorTopLeft.x() + (m_anchor->width() - width()) / 2;
    int y = anchorTopLeft.y() - height() - kBubbleGapPx;

    // 夹回当前屏幕可用区域，避免气泡跑到屏幕外
    QScreen *screen = QGuiApplication::screenAt(m_anchor->frameGeometry().center());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        const QRect area = screen->availableGeometry();
        const int m = kBubbleScreenMarginPx;
        if (x + width() > area.right() - m) {
            x = area.right() - m - width();
        }
        if (x < area.left() + m) {
            x = area.left() + m;
        }
        if (y < area.top() + m) {
            // 上方空间不足时改挂在立绘下方
            const QPoint anchorBottom = m_anchor->mapToGlobal(QPoint(0, m_anchor->height()));
            y = anchorBottom.y() + kBubbleGapPx;
        }
        if (y + height() > area.bottom() - m) {
            y = area.bottom() - m - height();
        }
    }

    move(x, y);
}

} // namespace whalepet
