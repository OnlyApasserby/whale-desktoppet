#pragma once

// P4 内容层：每日任务 —— docs/ROADMAP-P4.md 任务 2、docs/GAMEPLAY.md §5。
//
// 规则：
//   * 每天 3 槽 = 1 条固定（今日签到） + 2 条按 dayKey 抽签（同一天重启后槽位不变）；
//   * 交互推进进度，进度达标即 done（发 questDone，供状态机播放 QuestDone 表现）；
//   * 领取只能成功一次（DB 层 `claimed` 条件更新保证幂等），奖励经 rewardGranted 交给组合根
//     落到 GrowthService（本类不直接改养成数值，避免双向依赖）；
//   * 跨天时刷新槽位，并广播「上一天是否 3 槽全领」（成就的「全勤」统计依赖它）。

#include "core/GrowthRules.h"
#include "core/Quests.h"
#include "model/QuestRepo.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace whalepet::model {
class Database;
class DiaryRepo;
} // namespace whalepet::model

namespace whalepet::viewmodel {

class QuestService : public QObject {
    Q_OBJECT

public:
    explicit QuestService(model::Database *db, QObject *parent = nullptr);
    ~QuestService() override;

    // 载入并按需刷新到「今天」
    bool load(qint64 nowMs = 0);

    // 跨天刷新（用户跨午夜仍在运行时由交互上报触发）
    void refreshForToday(qint64 nowMs = 0);

    // 注意：不能用 slots 作标识符——Qt 把 slots 定义为空宏（qobjectdefs.h），
    // 本头引入 QObject 后会把它吃掉，故命名 slotList。
    const QList<model::QuestSlot> &slotList() const { return m_slots; }
    QString dayKey() const { return m_dayKey; }

    int claimedCount() const;
    bool allClaimed() const;

    // 交互上报：推进匹配任务进度；返回是否有任何槽位发生变化
    bool reportInteraction(core::Interaction type, qint64 nowMs = 0);

    // 领取奖励（slotIndex 为 slotList() 下标）；重复领取返回 false
    bool claim(int slotIndex, qint64 nowMs = 0);

signals:
    void slotsChanged();
    void questDone(const QString &id, const QString &name);
    void rewardGranted(int mood, int affinity, const QString &name);
    void dayRolled(bool previousDayFull);

private:
    void buildTodaySlots(qint64 nowMs);
    static bool interactionMatches(core::Interaction type, core::QuestMetric metric);

    model::Database *m_db = nullptr;
    std::unique_ptr<model::QuestRepo> m_repo;
    std::unique_ptr<model::DiaryRepo> m_diary;

    QList<model::QuestSlot> m_slots;
    QString m_dayKey;
};

} // namespace whalepet::viewmodel
