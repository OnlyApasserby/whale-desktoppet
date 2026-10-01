#include "viewmodel/SigninService.h"

#include "core/Calendar.h"
#include "core/SigninRules.h"
#include "model/Database.h"
#include "model/DiaryRepo.h"

#include <QDateTime>

namespace whalepet::viewmodel {

namespace {

qint64 nowOrCurrent(qint64 nowMs)
{
    return nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
}

} // namespace

SigninService::SigninService(model::Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_repo(std::make_unique<model::SigninRepo>(db))
    , m_diary(std::make_unique<model::DiaryRepo>(db))
{
}

SigninService::~SigninService() = default;

bool SigninService::load(qint64 nowMs)
{
    m_rows.clear();
    m_weekKey.clear();
    if (m_db == nullptr || !m_db->isOpen() || m_repo == nullptr) {
        return false;
    }

    const qint64 now = nowOrCurrent(nowMs);
    const QString key = QString::fromStdString(core::weekKey(now));
    m_todayIndex = core::dayIndexMondayFirst(now);

    m_rows = m_repo->loadAll();
    bool sameWeek = (m_rows.size() == core::kSigninWeekDays);
    for (const model::SigninRow &row : m_rows) {
        if (row.weekKey != key) {
            sameWeek = false;
            break;
        }
    }

    if (!sameWeek) {
        m_repo->resetWeek(key);
        m_rows = m_repo->loadAll();
    }

    m_weekKey = key;
    emit weekChanged();
    emit boardChanged();
    return true;
}

void SigninService::syncWeek(qint64 nowMs)
{
    if (m_db == nullptr || !m_db->isOpen() || m_repo == nullptr) {
        return;
    }

    const qint64 now = nowOrCurrent(nowMs);
    const QString key = QString::fromStdString(core::weekKey(now));
    m_todayIndex = core::dayIndexMondayFirst(now);
    if (m_weekKey == key) {
        return;
    }

    m_repo->resetWeek(key);
    m_rows = m_repo->loadAll();
    m_weekKey = key;
    emit weekChanged();
    emit boardChanged();
}

int SigninService::signedCount() const
{
    int n = 0;
    for (const model::SigninRow &row : m_rows) {
        if (row.signedDay) {
            ++n;
        }
    }
    return n;
}

bool SigninService::isTodaySigned() const
{
    for (const model::SigninRow &row : m_rows) {
        if (row.dayIndex == m_todayIndex) {
            return row.signedDay;
        }
    }
    return false;
}

bool SigninService::markToday(qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    syncWeek(now);

    for (model::SigninRow &row : m_rows) {
        if (row.dayIndex != m_todayIndex) {
            continue;
        }
        if (row.signedDay) {
            return false;
        }

        row.signedDay = true;
        row.weekKey = m_weekKey;
        if (m_repo == nullptr || !m_repo->saveRow(row)) {
            return false;
        }

        if (m_diary != nullptr) {
            m_diary->appendDaily(QStringLiteral("signin"), QStringLiteral("今日签到"), now);
        }
        emit todaySigned(row.dayIndex, signedCount());
        emit boardChanged();
        checkMilestones(now);
        return true;
    }
    return false;
}

void SigninService::checkMilestones(qint64 nowMs)
{
    const int count = signedCount();
    for (const core::SigninMilestone &milestone : core::kSigninMilestones) {
        if (count < milestone.days) {
            continue;
        }

        bool alreadyGranted = false;
        for (const model::SigninRow &row : m_rows) {
            if ((row.rewardMask & milestone.bit) != 0) {
                alreadyGranted = true;
                break;
            }
        }
        if (alreadyGranted) {
            continue;
        }

        if (m_repo == nullptr || !m_repo->markReward(m_todayIndex, milestone.bit)) {
            continue;
        }
        for (model::SigninRow &row : m_rows) {
            if (row.dayIndex == m_todayIndex) {
                row.rewardMask |= milestone.bit;
            }
        }

        if (m_diary != nullptr) {
            m_diary->appendDaily(QStringLiteral("signin"),
                                 QStringLiteral("本周签到 %1 天奖励").arg(milestone.days), nowMs);
        }
        emit rewardGranted(milestone.mood, milestone.affinity, milestone.days);
    }
    emit boardChanged();
}

} // namespace whalepet::viewmodel
