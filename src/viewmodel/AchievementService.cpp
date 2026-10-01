#include "viewmodel/AchievementService.h"

#include "core/Calendar.h"
#include "core/GrowthRules.h"
#include "model/AchievementRepo.h"
#include "model/Database.h"
#include "model/DiaryRepo.h"

#include <QDateTime>

namespace whalepet::viewmodel {

namespace {

qint64 nowOrCurrent(qint64 nowMs)
{
    return nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
}

QString statKeyOf(core::AchMetric metric)
{
    const char *key = core::achStatKey(metric);
    return key == nullptr ? QString() : QString::fromLatin1(key);
}

} // namespace

AchievementService::AchievementService(model::Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_repo(std::make_unique<model::AchievementRepo>(db))
    , m_diary(std::make_unique<model::DiaryRepo>(db))
{
}

AchievementService::~AchievementService() = default;

void AchievementService::load()
{
    m_unlocked.clear();
    m_stats.clear();
    if (m_repo) {
        m_unlocked = m_repo->loadUnlocked();
    }
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    for (const core::AchievementDef &def : core::kAchievements) {
        const QString key = statKeyOf(def.metric);
        if (key.isEmpty() || m_stats.contains(key)) {
            continue;
        }
        m_stats.insert(key, m_db->meta(key, QStringLiteral("0")).toInt());
    }

    // 单日局数：按落库的自然日判断，跨天则清零（避免重启后沿用昨天的计数）
    m_miniGameDay = m_db->meta(QStringLiteral("stat.mg_day"));
    resetMiniGameDayIfNeeded(QDateTime::currentMSecsSinceEpoch());

    syncCountersToSnapshot();
    emit unlockedCountChanged(m_unlocked.size());
}

void AchievementService::reportInteraction(core::Interaction type, qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    switch (type) {
    case core::Interaction::Pat:
        bumpStat(core::AchMetric::PatCount, 1);
        break;
    case core::Interaction::Belly:
        bumpStat(core::AchMetric::BellyCount, 1);
        break;
    case core::Interaction::Tail:
        bumpStat(core::AchMetric::TailCount, 1);
        break;
    case core::Interaction::Poke:
        bumpStat(core::AchMetric::PokeCount, 1);
        break;
    case core::Interaction::Feed:
        bumpStat(core::AchMetric::FeedCount, 1);
        break;
    case core::Interaction::Praise:
        bumpStat(core::AchMetric::PraiseCount, 1);
        break;
    case core::Interaction::Triple:
        bumpStat(core::AchMetric::TripleCount, 1);
        break;
    case core::Interaction::Signin:
        break; // 签到不进计数器：连续天数 / 周签到格数已能表达
    }

    if (core::isLateNight(now)) {
        peakStat(core::AchMetric::NightInteraction, 1);
    }

    syncCountersToSnapshot();
    evaluate(now);
}

void AchievementService::reportComeback(qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    peakStat(core::AchMetric::Comeback, 1);
    syncCountersToSnapshot();
    evaluate(now);
}

void AchievementService::setProgress(int level, int bondLevel, std::int64_t companionMs,
                                     int streakDays, int weekSigninDays, int mood, int satiety,
                                     qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    m_snapshot.level = level;
    m_snapshot.bondLevel = bondLevel;
    m_snapshot.companionDays = core::companionDaysFromMs(companionMs);
    m_snapshot.streakDays = streakDays;
    m_snapshot.weekSigninDays = weekSigninDays;
    m_snapshot.moodPeak = peakStat(core::AchMetric::MoodPeak, mood);
    m_snapshot.satietyPeak = peakStat(core::AchMetric::SatietyPeak, satiety);
    syncCountersToSnapshot();
    evaluate(now);
}

void AchievementService::reportQuestCompleted(qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    bumpStat(core::AchMetric::QuestDoneTotal, 1);
    syncCountersToSnapshot();
    evaluate(now);
}

void AchievementService::reportQuestFullDay(bool fullDay, qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    if (fullDay) {
        bumpStat(core::AchMetric::QuestAllToday, 1);
        bumpStat(core::AchMetric::QuestFullStreak, 1);
    } else {
        setStat(core::AchMetric::QuestFullStreak, 0);
    }
    syncCountersToSnapshot();
    evaluate(now);
}

void AchievementService::reportMiniGame(bool won, bool expert, bool perfect, int maxChain,
                                        qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    resetMiniGameDayIfNeeded(now);

    bumpStat(core::AchMetric::MiniGamePlays, 1);
    bumpStat(core::AchMetric::MiniGamePlaysToday, 1);
    if (won) {
        bumpStat(core::AchMetric::MiniGameWins, 1);
        if (expert) {
            bumpStat(core::AchMetric::MiniGameExpertWins, 1);
        }
    }
    if (won && perfect) {
        bumpStat(core::AchMetric::MiniGamePerfect, 1);
    }
    if (maxChain > 0) {
        peakStat(core::AchMetric::MiniGameMaxChain, maxChain);
    }

    syncCountersToSnapshot();
    evaluate(now);
}

void AchievementService::resetMiniGameDayIfNeeded(qint64 nowMs)
{
    const QString today = QString::fromStdString(core::dayKey(nowMs));
    if (m_miniGameDay == today) {
        return;
    }
    m_miniGameDay = today;
    setStat(core::AchMetric::MiniGamePlaysToday, 0);
    if (m_db != nullptr && m_db->isOpen()) {
        m_db->setMeta(QStringLiteral("stat.mg_day"), today);
    }
}

QList<QString> AchievementService::evaluate(qint64 nowMs)
{
    QList<QString> newlyUnlocked;
    if (m_repo == nullptr) {
        return newlyUnlocked;
    }

    const qint64 now = nowOrCurrent(nowMs);
    for (const core::AchievementDef &def : core::kAchievements) {
        const QString id = QString::fromLatin1(def.id);
        if (m_unlocked.contains(id) || !core::achievementReached(def, m_snapshot)) {
            continue;
        }
        if (!m_repo->unlock(id, now)) {
            continue;
        }
        m_unlocked.insert(id, now);
        newlyUnlocked.append(id);

        if (m_diary) {
            m_diary->appendDaily(QStringLiteral("achievement"), QString::fromUtf8(def.name), now);
        }
        emit unlocked(id, QString::fromUtf8(def.name));
    }

    if (!newlyUnlocked.isEmpty()) {
        emit unlockedCountChanged(m_unlocked.size());
    }
    return newlyUnlocked;
}

int AchievementService::statValue(core::AchMetric metric) const
{
    const QString key = statKeyOf(metric);
    return key.isEmpty() ? 0 : m_stats.value(key, 0);
}

int AchievementService::bumpStat(core::AchMetric metric, int delta)
{
    const QString key = statKeyOf(metric);
    if (key.isEmpty()) {
        return 0;
    }
    const int next = m_stats.value(key, 0) + delta;
    m_stats.insert(key, next);
    persistStat(core::achStatKey(metric), next);
    return next;
}

int AchievementService::peakStat(core::AchMetric metric, int value)
{
    const QString key = statKeyOf(metric);
    if (key.isEmpty()) {
        return 0;
    }
    const int current = m_stats.value(key, 0);
    if (value <= current) {
        return current;
    }
    m_stats.insert(key, value);
    persistStat(core::achStatKey(metric), value);
    return value;
}

void AchievementService::setStat(core::AchMetric metric, int value)
{
    const QString key = statKeyOf(metric);
    if (key.isEmpty()) {
        return;
    }
    m_stats.insert(key, value);
    persistStat(core::achStatKey(metric), value);
}

void AchievementService::persistStat(const char *key, int value)
{
    if (key == nullptr || m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    m_db->setMeta(QString::fromLatin1(key), QString::number(value));
}

void AchievementService::syncCountersToSnapshot()
{
    m_snapshot.patCount = statValue(core::AchMetric::PatCount);
    m_snapshot.bellyCount = statValue(core::AchMetric::BellyCount);
    m_snapshot.tailCount = statValue(core::AchMetric::TailCount);
    m_snapshot.feedCount = statValue(core::AchMetric::FeedCount);
    m_snapshot.praiseCount = statValue(core::AchMetric::PraiseCount);
    m_snapshot.pokeCount = statValue(core::AchMetric::PokeCount);
    m_snapshot.tripleCount = statValue(core::AchMetric::TripleCount);
    m_snapshot.nightInteraction = statValue(core::AchMetric::NightInteraction);
    m_snapshot.comeback = statValue(core::AchMetric::Comeback);
    m_snapshot.questDoneTotal = statValue(core::AchMetric::QuestDoneTotal);
    m_snapshot.questAllToday = statValue(core::AchMetric::QuestAllToday);
    m_snapshot.questFullStreak = statValue(core::AchMetric::QuestFullStreak);
    m_snapshot.moodPeak = statValue(core::AchMetric::MoodPeak);
    m_snapshot.satietyPeak = statValue(core::AchMetric::SatietyPeak);
    m_snapshot.miniGamePlays = statValue(core::AchMetric::MiniGamePlays);
    m_snapshot.miniGameWins = statValue(core::AchMetric::MiniGameWins);
    m_snapshot.miniGameExpertWins = statValue(core::AchMetric::MiniGameExpertWins);
    m_snapshot.miniGameMaxChain = statValue(core::AchMetric::MiniGameMaxChain);
    m_snapshot.miniGamePerfect = statValue(core::AchMetric::MiniGamePerfect);
    m_snapshot.miniGamePlaysToday = statValue(core::AchMetric::MiniGamePlaysToday);
}

} // namespace whalepet::viewmodel
