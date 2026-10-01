#include "viewmodel/MiniGameService.h"

#include "core/GrowthRules.h"
#include "model/Database.h"

#include <QDateTime>
#include <QDebug>

namespace whalepet::viewmodel {

namespace {

const QString kKeyRewardDay = QStringLiteral("game.reward_day");
const QString kKeyRewardPlays = QStringLiteral("game.reward_plays_today");
// 个人最快键前缀：新键 game.best_ms_<gameId>/<difficultyId>，
// 旧版键（v0.2.0）为 game.best_ms_<MinePreset 整数>，迁移时复用同一前缀。
const QString kKeyBestPrefix = QStringLiteral("game.best_ms_");

} // namespace

MiniGameService::MiniGameService(model::Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

QString MiniGameService::bestKey(const QString &gameId, const QString &difficultyId)
{
    // 形如 game.best_ms_minesweeper/beginner；difficultyId 为空时退化为 gameId
    return kKeyBestPrefix + gameId
           + (difficultyId.isEmpty() ? QString() : QLatin1Char('/') + difficultyId);
}

void MiniGameService::persistMeta(const QString &key, const QString &value)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    m_db->setMeta(key, value);
}

qint64 MiniGameService::bestMs(const QString &gameId, const QString &difficultyId) const
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return 0;
    }
    return m_db->meta(bestKey(gameId, difficultyId), QStringLiteral("0")).toLongLong();
}

void MiniGameService::load()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    m_rewardDay = m_db->meta(kKeyRewardDay);
    m_rewardPlaysToday = m_db->meta(kKeyRewardPlays, QStringLiteral("0")).toInt();

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

void MiniGameService::adoptLegacyBest(const QString &gameId, const QString &difficultyId,
                                      const QString &legacySuffix)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    const QString key = bestKey(gameId, difficultyId);
    if (m_db->meta(key, QStringLiteral("0")).toLongLong() > 0) {
        return; // 新键已有值，无需迁移
    }
    const qint64 legacy =
        m_db->meta(kKeyBestPrefix + legacySuffix, QStringLiteral("0")).toLongLong();
    if (legacy > 0) {
        persistMeta(key, QString::number(legacy));
        qInfo() << "[MiniGameService] 迁移旧版个人最快纪录:" << key << "=" << legacy;
    }
}

MiniGameReward MiniGameService::settle(const core::MiniGameResult &result, qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : QDateTime::currentMSecsSinceEpoch();
    refreshDay(now);

    const QString gameId = QString::fromStdString(result.gameId);
    const QString difficultyId = QString::fromStdString(result.difficultyId);

    MiniGameReward reward;
    reward.grade = core::gameGrade(result);

    // 个人最快：仅「通关」且用时有意义时比较（照搬 settleGame 的 best 刷新逻辑）
    qint64 best = bestMs(gameId, difficultyId);
    if (result.won && result.elapsedMs > 0 && (best <= 0 || result.elapsedMs < best)) {
        best = result.elapsedMs;
        reward.newRecord = true;
        persistMeta(bestKey(gameId, difficultyId), QString::number(best));
    }
    reward.bestMs = best;

    // 每日上限：局数照记，超出只计分不发奖（与参考项目「每日 3 局」一致，所有小游戏共用）
    ++m_rewardPlaysToday;
    persistMeta(kKeyRewardPlays, QString::number(m_rewardPlaysToday));
    reward.rewardsUsedToday = m_rewardPlaysToday;
    if (m_rewardPlaysToday > core::kGameRewardsPerDay) {
        return reward; // rewarded == false
    }

    reward.rewarded = true;
    switch (reward.grade) {
    case core::GameGrade::Win:
        reward.mood = core::kGameWinMood;
        reward.affinity = core::kGameWinAffinity;
        break;
    case core::GameGrade::Draw:
        reward.mood = core::kGameDrawMood;
        reward.affinity = core::kGameDrawAffinity;
        break;
    case core::GameGrade::Lose:
        reward.mood = core::kGameLoseMood;
        break;
    }
    if (reward.newRecord) {
        reward.affinity += core::kGameHighScoreAffinity;
    }
    return reward;
}

} // namespace whalepet::viewmodel
