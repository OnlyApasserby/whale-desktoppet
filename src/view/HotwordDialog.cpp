#include "view/HotwordDialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace whalepet {

namespace {
constexpr int kWordRole = Qt::UserRole; // QListWidgetItem 里存热词原文
}

HotwordDialog::HotwordDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("热词录入"));
    setModal(false); // 与状态/内容面板一致：非模态，不打断互动

    auto *layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        QStringLiteral("输入文本 → 「立即触发」试一次；选好关键词 → 「录入」长期生效。"), this));

    auto *row = new QHBoxLayout;
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("例如：加班到崩 / 我的天 / 明天上线"));
    row->addWidget(m_input, 1);
    layout->addLayout(row);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(QStringLiteral("对应梗："), this));
    m_keywords = new QComboBox(this);
    row2->addWidget(m_keywords, 1);
    m_save = new QPushButton(QStringLiteral("录入热词"), this);
    row2->addWidget(m_save);
    m_trigger = new QPushButton(QStringLiteral("立即触发"), this);
    row2->addWidget(m_trigger);
    layout->addLayout(row2);

    layout->addWidget(new QLabel(QStringLiteral("已录入热词（越靠前优先级越高）："), this));
    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    auto *row3 = new QHBoxLayout;
    m_remove = new QPushButton(QStringLiteral("删除选中"), this);
    row3->addWidget(m_remove);
    row3->addStretch(1);
    layout->addLayout(row3);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setText(QStringLiteral("提示：热词优先于内置触发词；匹配为「包含即命中」。"));
    layout->addWidget(m_hint);

    connect(m_trigger, &QPushButton::clicked, this, &HotwordDialog::onTrigger);
    connect(m_save, &QPushButton::clicked, this, &HotwordDialog::onSave);
    connect(m_remove, &QPushButton::clicked, this, &HotwordDialog::onRemove);
    connect(m_input, &QLineEdit::returnPressed, this, &HotwordDialog::onTrigger);
}

void HotwordDialog::setKeywordChoices(const QStringList &keywordIds)
{
    m_keywords->clear();
    m_keywords->addItems(keywordIds);
}

void HotwordDialog::setHotwords(const QVector<model::Hotword> &items)
{
    m_list->clear();
    for (const model::Hotword &item : items) {
        auto *row = new QListWidgetItem(
            QStringLiteral("%1  →  %2").arg(item.word, item.keywordId), m_list);
        row->setData(kWordRole, item.word);
    }
}

void HotwordDialog::showHint(const QString &text, bool isError)
{
    m_hint->setText(isError ? QStringLiteral("失败：%1").arg(text) : text);
}

QString HotwordDialog::inputText() const
{
    return m_input != nullptr ? m_input->text() : QString();
}

void HotwordDialog::onTrigger()
{
    const QString text = inputText().trimmed();
    if (text.isEmpty()) {
        showHint(QStringLiteral("请先输入要触发的文本"), true);
        return;
    }
    emit triggerRequested(text);
}

void HotwordDialog::onSave()
{
    const QString text = inputText().trimmed();
    if (text.isEmpty()) {
        showHint(QStringLiteral("请先输入热词内容"), true);
        return;
    }
    if (m_keywords->currentIndex() < 0) {
        showHint(QStringLiteral("请选择对应的梗（关键词）"), true);
        return;
    }
    emit saveRequested(text, m_keywords->currentText());
}

void HotwordDialog::onRemove()
{
    QListWidgetItem *row = m_list->currentItem();
    if (row == nullptr) {
        showHint(QStringLiteral("请先在列表里选中一条热词"), true);
        return;
    }
    emit removeRequested(row->data(kWordRole).toString());
}

} // namespace whalepet
