#include "view/DialoguePanel.h"

#include "common/PetVisuals.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace whalepet {

namespace {
// 问题按钮的最小宽度：问题文本多为 10–20 字，给足宽度避免频繁换行抖动
constexpr int kOptionMinWidth = 280;
} // namespace

DialoguePanel::DialoguePanel(QWidget *parent)
    : QDialog(parent)
{
    // 与 SpeechBubble 同一套窗口策略：无边框 / 置顶 / 工具窗口（不进任务栏）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_ShowWithoutActivating);
    // 需求：提问界面的窗口标题固定为「主人的问题」
    setWindowTitle(QString::fromUtf8(kPanelTitle));
    // 样式由全局样式表按 objectName 命中（project.qss 只补边框/圆角）
    setObjectName(QStringLiteral("DialoguePanel"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 7, 10, 7);
    m_layout->setSpacing(6);

    // 可见的标题行（无边框窗口不显示原生标题栏，故在面板内同步显示同一文案）
    m_titleLabel = new QLabel(QString::fromUtf8(kPanelTitle), this);
    m_titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_layout->addWidget(m_titleLabel);

    m_hintLabel = new QLabel(QStringLiteral("点一个问题，我来回答～"), this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setMaximumWidth(kBubbleMaxWidth + 60);
    m_hintLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_layout->addWidget(m_hintLabel);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        // 超时 = 这次不问：不消耗任何配额，同一批问题下次仍会重新刷新
        closePanel();
    });
}

void DialoguePanel::attachTo(QWidget *anchor)
{
    m_anchor = anchor;
}

int DialoguePanel::optionCount() const
{
    return static_cast<int>(m_buttons.size());
}

void DialoguePanel::rebuildOptions(const QStringList &questions, const QList<bool> &enabled,
                                   const QStringList &hints)
{
    for (QPushButton *button : m_buttons) {
        m_layout->removeWidget(button);
        button->deleteLater();
    }
    m_buttons.clear();

    const int count = static_cast<int>(questions.size());
    for (int i = 0; i < count; ++i) {
        const QString text = questions.at(i);
        // 不可用项也**占位**（保持「五选一」的形状稳定），但禁用并给出原因
        auto *button = new QPushButton(text.isEmpty() ? QStringLiteral("（暂不可用）") : text, this);
        button->setMinimumWidth(kOptionMinWidth);
        const bool usable = enabled.value(i, false);
        button->setEnabled(usable);
        if (usable) {
            button->setToolTip(text);
        } else {
            const QString hint = hints.value(i, QString());
            button->setToolTip(hint.isEmpty() ? QStringLiteral("暂不可用") : hint);
        }
        connect(button, &QPushButton::clicked, this, [this, i]() { onButtonClicked(i); });
        m_layout->addWidget(button);
        m_buttons.append(button);
    }
}

void DialoguePanel::showOptions(const QStringList &questions, const QList<bool> &enabled,
                                const QStringList &hints)
{
    if (questions.isEmpty()) {
        return;
    }
    m_dismissNotified = false;
    rebuildOptions(questions, enabled, hints);

    adjustSize();
    reposition();
    show();
    raise();
    m_timeout->start(kAnswerTimeoutMs);
}

void DialoguePanel::closePanel()
{
    m_timeout->stop();
    hide();
    if (!m_dismissNotified) {
        m_dismissNotified = true;
        emit dismissed();
    }
}

void DialoguePanel::onButtonClicked(int index)
{
    m_timeout->stop();
    hide();
    m_dismissNotified = true; // 选择已生效，不再发 dismissed（避免调用方二次 cancel）
    emit chosen(index);
}

void DialoguePanel::closeEvent(QCloseEvent *event)
{
    // 用户用 Esc / 系统关闭：与超时同义（放弃本批，不消耗配额）
    m_timeout->stop();
    if (!m_dismissNotified) {
        m_dismissNotified = true;
        emit dismissed();
    }
    QDialog::closeEvent(event);
}

QString DialoguePanel::optionText(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_buttons.size())) {
        return {};
    }
    return m_buttons.at(index)->text();
}

void DialoguePanel::reposition()
{
    if (m_anchor == nullptr) {
        return;
    }
    if (!m_anchor->isVisible()) {
        closePanel();
        return;
    }

    const QPoint anchorTopLeft = m_anchor->mapToGlobal(QPoint(0, 0));
    int x = anchorTopLeft.x() + (m_anchor->width() - width()) / 2;
    int y = anchorTopLeft.y() - height() - kBubbleGapPx;

    QScreen *screen = QGuiApplication::screenAt(m_anchor->frameGeometry().center());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        const QRect area = screen->availableGeometry();
        const int margin = kBubbleScreenMarginPx;
        if (x + width() > area.right() - margin) {
            x = area.right() - margin - width();
        }
        if (x < area.left() + margin) {
            x = area.left() + margin;
        }
        if (y < area.top() + margin) {
            // 上方空间不足时改挂在立绘下方
            const QPoint anchorBottom = m_anchor->mapToGlobal(QPoint(0, m_anchor->height()));
            y = anchorBottom.y() + kBubbleGapPx;
        }
        if (y + height() > area.bottom() - margin) {
            y = area.bottom() - margin - height();
        }
    }

    move(x, y);
}

} // namespace whalepet
