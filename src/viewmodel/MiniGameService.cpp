#include "viewmodel/MiniGameService.h"

#include "core/GrowthRules.h"
#include "model/Database.h"

#include <QDateTime>
#include <QDebug>

namespace whalepet::viewmodel {

namespace {

const QString kKeyRewardDay = QStringLiteral("game.reward_day");
const QString kKeyRewardPlays = QStringLiteral("game.reward_plays_today");
const char *const kKeyBestPrefix = "game.best_ms_";

} // namespace

MiniGameService::MiniGameService(model::Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

QString MiniGameService::bestKey(int presetIndex)
{
    const int bucket = (presetIndex >= 0 && presetIndex < kPresetBuckets) ? presetIndex : 0;
    return QString::fromLatin1(kKeyBestPrefix) + QString::number(bucket);
}

void MiniGameService::persistMeta(const QString &key, const QString &value)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    m_db->setMeta(key, value);
}

qint64 MiniGameService::bestMs(int presetIndex) const
{
    const int bucket = (presetIndex >= 0 && presetIndex < kPresetBuckets) ? presetIndex : 0;
    return m_bestMs[bucket];
}

void MiniGameService::load()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    m_rewardDay = m_db->meta(kKeyRewardDay);
    m_rewardPlaysToday = m_db->meta(kKeyRewardPlays, QStringLiteral("0")).toInt();
    for (int i = 0; i < kPresetBuckets; ++i) {
        m_bestMs[i] = m_db->meta(bestKey(i), QStringLiteral("0")).toLongLong();
    }

    // 跨天：今日奖励局数清零（个人最快是长期纪录，保留）
    refreshDay(QDateTime::currentMSecsSinceEpoch());
}

void MiniGameService::refreshDay(qint64 nowMs)
{
    const QString today = QString::fromStdString(core::dayKey(nowMs));
    if (m_rewardDay == today) {
        return;
    }
    m_rewardDay = today;
    m_rewardPlaysToday = 0;
    persistMeta(kKeyRewardDay, m_rewardDay);
    persistMeta(kKeyRewardPlays, QStringLiteral("0"));
}

MiniGameReward MiniGameService::settle(const core::MineSummary &summary, int presetIndex,
                                      qint64 elapsedMs, qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : QDateTime::currentMSecsSinceEpoch();
    refreshDay(now);

    MiniGameReward reward;
    reward.grade = core::mineGrade(summary);

    // 个人最快：仅「通关」且用时有意义时比较（照搬 settleGame 的 best 刷新逻辑）
    const int bucket = (presetIndex >= 0 && presetIndex < kPresetBuckets) ? presetIndex : 0;
    if (summary.won && elapsedMs > 0 && (m_bestMs[bucket] <= 0 || elapsedMs < m_bestMs[bucket])) {
        m_bestMs[bucket] = elapsedMs;
        reward.newRecord = true;
        persistMeta(bestKey(bucket), QString::number(elapsedMs));
    }
    reward.bestMs = m_bestMs[bucket];

    // 每日上限：局数照记，超出只计分不发奖（与参考项目「每日 3 局」一致）
    ++m_rewardPlaysToday;
    persistMeta(kKeyRewardPlays, QString::number(m_rewardPlaysToday));
    reward.rewardsUsedToday = m_rewardPlaysToday;
    if (m_rewardPlaysToday > core::kGameRewardsPerDay) {
        return reward; // rewarded == false
    }

    reward.rewarded = true;
    switch (reward.grade) {
    case core::MineGrade::Win:
        reward.mood = core::kGameWinMood;
        reward.affinity = core::kGameWinAffinity;
        break;
    case core::MineGrade::Draw:
        reward.mood = core::kGameDrawMood;
        reward.affinity = core::kGameDrawAffinity;
        break;
    case core::MineGrade::Lose:
        reward.mood = core::kGameLoseMood;
        break;
    }
    if (reward.newRecord) {
        reward.affinity += core::kGameHighScoreAffinity;
    }
    return reward;
}

} // namespace whalepet::viewmodel
