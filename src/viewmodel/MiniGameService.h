#pragma once

// 小游戏结算服务 —— 养成数值奖励 + 每日上限 + 个人最快纪录。
//
// 本服务**与具体玩法完全解耦**：输入是通用的 core::MiniGameResult（见 core/MiniGameTypes.h），
// 因此任何按插件规范接入的小游戏都自动获得同一套奖励 / 上限 / 纪录逻辑。
//
// 照搬参考项目（referances/dsh-whale-musume）：
//   * 奖励数值：whale-moe-core.js applyGrowth 的 game-win / game-draw / game-lose / high-score
//     （:563-566），数值常量集中在 core/GrowthRules.h；
//   * 每日上限：GAME.REWARDS_PER_DAY = 3 与 gameRewardAllowed（:251-258、:359-364）——
//     每日**最多 3 局**计入养成奖励，超出只计分不发奖（所有小游戏共用同一份每日额度）；
//   * 结算流程：dsh-whale-moe.js settleGame（:1056-1106）——先记局数、再判「刷新纪录」、
//     最后按档位发奖，且「刷新纪录」同样受每日上限约束。
//
// 边界：本类只做「档位判定 → 上限判定 → 纪录判定 → 产出增量」，
// **不直接改养成状态**——增量由调用方（PetWindow）交给 GrowthService::grantReward，
// 从而复用既有的夹取 / 升级 / 落盘链路。元数据落在 meta 表（`game.*`），不改表结构。

#include "core/GrowthRules.h" // kGameRewardsPerDay（每日上限）
#include "core/MiniGameTypes.h"

#include <QObject>
#include <QString>

namespace whalepet {
namespace model {
class Database;
} // namespace model

namespace viewmodel {

// 一局结算产出
struct MiniGameReward {
    core::GameGrade grade = core::GameGrade::Lose;
    bool rewarded = false;    // 本局是否在每日上限内（计入养成奖励）
    bool newRecord = false;   // 是否刷新该游戏该难度的个人最快
    int mood = 0;             // 应发放的心情增量（未发放时为 0）
    int affinity = 0;         // 应发放的好感增量（未发放时为 0）
    int rewardsUsedToday = 0; // 今日计入奖励的局数（含本局）
    qint64 bestMs = 0;        // 该游戏该难度当前个人最快（0 表示暂无纪录）
};

class MiniGameService : public QObject {
    Q_OBJECT
public:
    explicit MiniGameService(model::Database *db, QObject *parent = nullptr);

    // 从 meta 读取「今日已用局数」；跨天则清零（个人最快是长期纪录，保留）
    void load();

    // 结算一局：通用结果 → 档位 → 每日上限 → 个人最快，返回应发放的养成增量与展示信息。
    // nowMs <= 0 表示取当前时间。
    MiniGameReward settle(const core::MiniGameResult &result, qint64 nowMs = 0);

    int rewardsUsedToday() const { return m_rewardPlaysToday; }
    static int rewardLimit() { return core::kGameRewardsPerDay; }

    // 个人最快（按「游戏 + 难度」分桶；0 表示暂无纪录）
    qint64 bestMs(const QString &gameId, const QString &difficultyId) const;

    // 旧版本键迁移（升级不丢纪录）：legacySuffix 对应 meta `game.best_ms_<legacySuffix>`。
    // 仅当新键无值时继承旧值。
    void adoptLegacyBest(const QString &gameId, const QString &difficultyId,
                         const QString &legacySuffix);

private:
    void refreshDay(qint64 nowMs);
    static QString bestKey(const QString &gameId, const QString &difficultyId);
    void persistMeta(const QString &key, const QString &value);

    model::Database *m_db = nullptr;
    QString m_rewardDay;        // meta `game.reward_day`
    int m_rewardPlaysToday = 0; // meta `game.reward_plays_today`
};

} // namespace viewmodel
} // namespace whalepet
