#include "view/StatusPanel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace whalepet {

StatusPanel::StatusPanel(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("鲸鱼娘 · 状态"));
    setModal(false);
    setMinimumWidth(320);

    auto *root = new QVBoxLayout(this);

    auto *growthBox = new QGroupBox(QStringLiteral("养成"), this);
    auto *form = new QFormLayout(growthBox);

    m_level = new QLabel(growthBox);
    m_bond = new QLabel(growthBox);
    m_mood = new QLabel(growthBox);
    m_affinity = new QLabel(growthBox);
    m_satiety = new QLabel(growthBox);

    form->addRow(QStringLiteral("等级"), m_level);
    form->addRow(QStringLiteral("经验"), m_expBar = new QProgressBar(growthBox));
    form->addRow(QStringLiteral("羁绊"), m_bond);
    form->addRow(QStringLiteral("心情"), m_mood);
    form->addRow(QStringLiteral("好感度"), m_affinity);
    form->addRow(QStringLiteral("饱食度"), m_satiety);

    m_expBar->setRange(0, core::kLevelStep);
    m_expBar->setTextVisible(true);

    root->addWidget(growthBox);

    auto *companionBox = new QGroupBox(QStringLiteral("陪伴"), this);
    auto *companionForm = new QFormLayout(companionBox);
    m_companion = new QLabel(companionBox);
    m_streak = new QLabel(companionBox);
    companionForm->addRow(QStringLiteral("累计时长"), m_companion);
    companionForm->addRow(QStringLiteral("连续签到"), m_streak);
    root->addWidget(companionBox);

    m_signInButton = new QPushButton(QStringLiteral("今日签到"), this);
    connect(m_signInButton, &QPushButton::clicked, this, &StatusPanel::signInRequested);
    root->addWidget(m_signInButton);

    m_storage = new QLabel(this);
    m_storage->setWordWrap(true);
    root->addWidget(m_storage);

    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    root->addWidget(close);
}

QString StatusPanel::formatDuration(qint64 ms)
{
    if (ms <= 0) {
        return QStringLiteral("0 分钟");
    }
    const qint64 totalMinutes = ms / 60000;
    const qint64 days = totalMinutes / (60 * 24);
    const qint64 hours = (totalMinutes / 60) % 24;
    const qint64 minutes = totalMinutes % 60;

    QStringList parts;
    if (days > 0) {
        parts << QStringLiteral("%1 天").arg(days);
    }
    if (hours > 0) {
        parts << QStringLiteral("%1 小时").arg(hours);
    }
    if (days == 0) {
        parts << QStringLiteral("%1 分钟").arg(minutes);
    }
    return parts.join(QLatin1Char(' '));
}

void StatusPanel::updateFrom(const model::PetStateData &state, const core::BondUnlocks &unlocks,
                             const QString &storageInfo)
{
    m_level->setText(QStringLiteral("Lv.%1").arg(state.level));

    const int inLevel = core::expInLevel(state.exp);
    const int span = core::expSpanForLevel(state.level);
    m_expBar->setRange(0, span > 0 ? span : core::kLevelStep);
    m_expBar->setValue(inLevel);
    m_expBar->setFormat(QStringLiteral("%1 / %2").arg(inLevel).arg(span));

    QStringList bondTags;
    if (unlocks.action) {
        bondTags << QStringLiteral("新动作");
    }
    if (unlocks.badge) {
        bondTags << QStringLiteral("称号");
    }
    if (unlocks.egg) {
        bondTags << QStringLiteral("彩蛋");
    }
    const QString bondSuffix =
        bondTags.isEmpty() ? QString() : QStringLiteral("（已解锁：%1）").arg(bondTags.join(QStringLiteral("、")));
    m_bond->setText(QStringLiteral("Lv.%1%2").arg(state.bondLevel).arg(bondSuffix));

    m_mood->setText(QStringLiteral("%1 / %2").arg(state.mood).arg(core::kMoodMax));
    m_affinity->setText(QStringLiteral("%1 / %2").arg(state.affinity).arg(core::kAffinityMax));
    m_satiety->setText(QStringLiteral("%1 / %2").arg(state.satiety).arg(core::kSatietyMax));

    m_companion->setText(formatDuration(state.companionMs));
    m_streak->setText(state.streakDays > 0 ? QStringLiteral("%1 天").arg(state.streakDays)
                                           : QStringLiteral("未签到"));

    m_storage->setText(storageInfo);
}

} // namespace whalepet
