#include "view/ContentPanel.h"

#include "core/Achievements.h"
#include "core/Quests.h"
#include "model/Database.h"
#include "model/DiaryRepo.h"
#include "viewmodel/AchievementService.h"
#include "viewmodel/QuestService.h"
#include "viewmodel/SigninService.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace whalepet {

namespace {

const char *const kWeekdayNames[core::kSigninWeekDays] = {
    "周一", "周二", "周三", "周四", "周五", "周六", "周日",
};

// 清空布局：用 deleteLater 而非 delete —— 重建常发生在某个按钮的 clicked 处理链
// 上（领取 → 服务发 slotsChanged → refreshAll → 删掉该按钮），直接 delete 会
// 删掉正在派发信号的控件（悬垂指针 / 崩溃）。见 docs/traps-P4.md。
void clearLayout(QLayout *layout)
{
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        } else if (QLayout *child = item->layout()) {
            clearLayout(child);
            delete child;
        }
        delete item;
    }
}

} // namespace

ContentPanel::ContentPanel(viewmodel::AchievementService *achievement, viewmodel::QuestService *quest,
                           viewmodel::SigninService *signin, model::Database *db, QWidget *parent)
    : QDialog(parent)
    , m_achievement(achievement)
    , m_quest(quest)
    , m_signin(signin)
    , m_db(db)
{
    setWindowTitle(QStringLiteral("鲸鱼娘 · 日常"));
    setModal(false);
    setMinimumSize(420, 480);

    // 三个页面只构建一次：embedInto（设置面板内嵌）与 showStandalone（独立窗口）二选一
    m_dailyPage = buildDailyTab();
    m_achievementPage = buildAchievementTab();
    m_diaryPage = buildDiaryTab();

    refreshAll();
}

void ContentPanel::embedInto(QTabWidget *tabs)
{
    if (tabs == nullptr || m_dailyPage == nullptr) {
        return;
    }
    if (m_ownTabs != nullptr) {
        // 已被 showStandalone 持有：一个 widget 不能有两个父，直接拒绝并告警（不静默）
        qWarning() << "[ContentPanel] 已用于独立窗口，不能再 embedInto";
        return;
    }
    tabs->addTab(m_dailyPage, QStringLiteral("日常"));
    tabs->addTab(m_achievementPage, QStringLiteral("成就墙"));
    tabs->addTab(m_diaryPage, QStringLiteral("成长日记"));
    refreshAll();
}

void ContentPanel::showStandalone()
{
    if (m_ownTabs == nullptr) {
        m_ownTabs = new QTabWidget(this);
        m_ownTabs->addTab(m_dailyPage, QStringLiteral("日常"));
        m_ownTabs->addTab(m_achievementPage, QStringLiteral("成就墙"));
        m_ownTabs->addTab(m_diaryPage, QStringLiteral("成长日记"));

        auto *root = new QVBoxLayout(this);
        root->addWidget(m_ownTabs);

        auto *close = new QPushButton(QStringLiteral("关闭"), this);
        connect(close, &QPushButton::clicked, this, &QDialog::hide);
        root->addWidget(close);
    }

    refreshAll();
    show();
    raise();
    activateWindow();
}

QWidget *ContentPanel::buildDailyTab()
{
    auto *page = new QWidget;

    auto *signinBox = new QGroupBox(QStringLiteral("本周签到"), page);
    auto *signinLayout = new QVBoxLayout(signinBox);
    m_signinSummary = new QLabel(signinBox);
    signinLayout->addWidget(m_signinSummary);

    auto *grid = new QGridLayout;
    for (int i = 0; i < core::kSigninWeekDays; ++i) {
        m_signinCells[i] = new QLabel(signinBox);
        m_signinCells[i]->setAlignment(Qt::AlignCenter);
        grid->addWidget(m_signinCells[i], 0, i);
    }
    signinLayout->addLayout(grid);

    m_signInButton = new QPushButton(QStringLiteral("今日签到"), signinBox);
    connect(m_signInButton, &QPushButton::clicked, this, &ContentPanel::signInRequested);
    signinLayout->addWidget(m_signInButton);

    auto *questBox = new QGroupBox(QStringLiteral("每日任务"), page);
    m_questLayout = new QVBoxLayout(questBox);

    auto *outer = new QVBoxLayout(page);
    outer->addWidget(signinBox);
    outer->addWidget(questBox);
    outer->addStretch();
    return page;
}

QWidget *ContentPanel::buildAchievementTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);

    m_achievementSummary = new QLabel(page);
    outer->addWidget(m_achievementSummary);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    auto *container = new QWidget;
    m_achievementLayout = new QVBoxLayout(container);
    scroll->setWidget(container);
    outer->addWidget(scroll, 1);
    return page;
}

QWidget *ContentPanel::buildDiaryTab()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    auto *container = new QWidget;
    m_diaryLayout = new QVBoxLayout(container);
    scroll->setWidget(container);
    outer->addWidget(scroll, 1);
    return page;
}

void ContentPanel::refreshAll()
{
    refreshDaily();
    refreshAchievements();
    refreshDiary();
}

void ContentPanel::refreshDaily()
{
    if (m_signin != nullptr && m_signinSummary != nullptr) {
        const int signedDays = m_signin->signedCount();
        const bool todaySigned = m_signin->isTodaySigned();
        m_signinSummary->setText(
            QStringLiteral("本周已签 %1 / %2 天").arg(signedDays).arg(core::kSigninWeekDays));

        m_signInButton->setEnabled(!todaySigned);
        m_signInButton->setText(todaySigned ? QStringLiteral("今日已签到")
                                            : QStringLiteral("今日签到"));

        for (int i = 0; i < core::kSigninWeekDays; ++i) {
            bool signedDay = false;
            for (const model::SigninRow &row : m_signin->rows()) {
                if (row.dayIndex == i) {
                    signedDay = row.signedDay;
                    break;
                }
            }
            const bool isToday = (i == m_signin->todayIndex());
            m_signinCells[i]->setText(QStringLiteral("%1\n%2")
                                          .arg(QString::fromUtf8(kWeekdayNames[i]),
                                               signedDay ? QStringLiteral("✓") : QStringLiteral("·")));
            // 今天或已签的格子保持正常字色，其余灰显（disabled）
            m_signinCells[i]->setEnabled(isToday || signedDay);
        }
    }

    if (m_questLayout == nullptr) {
        return;
    }
    clearLayout(m_questLayout);

    if (m_quest == nullptr) {
        m_questLayout->addWidget(new QLabel(QStringLiteral("任务不可用")));
        return;
    }

    const QList<model::QuestSlot> &questSlots = m_quest->slotList();
    if (questSlots.isEmpty()) {
        m_questLayout->addWidget(new QLabel(QStringLiteral("今日暂无任务")));
        return;
    }

    for (int i = 0; i < questSlots.size(); ++i) {
        const model::QuestSlot &slot = questSlots[i];
        const core::QuestDef *def = core::findQuest(slot.id.toUtf8().constData());
        const QString name = (def != nullptr) ? QString::fromUtf8(def->name) : slot.id;

        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto *label = new QLabel(
            QStringLiteral("%1（%2/%3）").arg(name).arg(slot.progress).arg(slot.target), row);
        rowLayout->addWidget(label, 1);

        if (slot.claimed) {
            rowLayout->addWidget(new QLabel(QStringLiteral("已领取"), row));
        } else {
            auto *claim = new QPushButton(slot.done ? QStringLiteral("领取")
                                                    : QStringLiteral("进行中"),
                                          row);
            claim->setEnabled(slot.done);
            connect(claim, &QPushButton::clicked, this,
                    [this, i] { emit questClaimRequested(i); });
            rowLayout->addWidget(claim);
        }

        m_questLayout->addWidget(row);
    }
}

void ContentPanel::refreshAchievements()
{
    if (m_achievementLayout == nullptr) {
        return;
    }
    clearLayout(m_achievementLayout);

    if (m_achievement == nullptr) {
        m_achievementLayout->addWidget(new QLabel(QStringLiteral("成就不可用")));
        return;
    }

    m_achievementSummary->setText(QStringLiteral("已解锁 %1 / %2")
                                      .arg(m_achievement->unlockedCount())
                                      .arg(core::kAchievementCount));

    const core::AchCategory categories[] = {
        core::AchCategory::Interaction, core::AchCategory::Companion, core::AchCategory::Growth,
        core::AchCategory::Quest,       core::AchCategory::MiniGame,
    };

    for (core::AchCategory category : categories) {
        auto *box = new QGroupBox(
            QString::fromUtf8(core::kAchCategoryNames[static_cast<int>(category)]));
        auto *grid = new QGridLayout(box);

        int index = 0;
        for (const core::AchievementDef &def : core::kAchievements) {
            if (def.category != category) {
                continue;
            }
            const bool unlocked = m_achievement->isUnlocked(QString::fromLatin1(def.id));

            auto *item = new QLabel(box);
            QString text = QStringLiteral("%1 %2")
                               .arg(QString::fromUtf8(def.icon), QString::fromUtf8(def.name));
            if (!unlocked) {
                text += QStringLiteral("（未解锁）");
            }
            item->setText(text);
            item->setToolTip(QString::fromUtf8(def.desc));
            item->setEnabled(unlocked); // 未解锁 → disabled 灰显

            grid->addWidget(item, index / 2, index % 2);
            ++index;
        }

        m_achievementLayout->addWidget(box);
    }
    m_achievementLayout->addStretch();
}

void ContentPanel::refreshDiary()
{
    if (m_diaryLayout == nullptr) {
        return;
    }
    clearLayout(m_diaryLayout);

    if (m_db == nullptr || !m_db->isOpen()) {
        m_diaryLayout->addWidget(new QLabel(QStringLiteral("存储不可用，无法读取日记")));
        return;
    }

    model::DiaryRepo repo(m_db);
    const QList<model::DiaryEntry> entries = repo.loadRecent(12); // GAMEPLAY §6：展示最近 12 条
    if (entries.isEmpty()) {
        m_diaryLayout->addWidget(new QLabel(QStringLiteral("还没有记录，去互动吧")));
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const model::DiaryEntry &entry : entries) {
        auto *line = new QLabel(QStringLiteral("[%1] %2")
                                    .arg(formatRelative(entry.tsMs, now), entry.detail));
        line->setWordWrap(true);
        m_diaryLayout->addWidget(line);
    }
    m_diaryLayout->addStretch();
}

QString ContentPanel::formatRelative(qint64 tsMs, qint64 nowMs)
{
    if (tsMs <= 0) {
        return QStringLiteral("—");
    }
    qint64 diff = nowMs - tsMs;
    if (diff < 0) {
        diff = 0;
    }

    const qint64 minutes = diff / 60000;
    if (minutes < 1) {
        return QStringLiteral("刚刚");
    }
    if (minutes < 60) {
        return QStringLiteral("%1 分钟前").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    if (hours < 24) {
        return QStringLiteral("%1 小时前").arg(hours);
    }
    const qint64 days = hours / 24;
    if (days < 30) {
        return QStringLiteral("%1 天前").arg(days);
    }
    return QDateTime::fromMSecsSinceEpoch(tsMs).toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace whalepet
