#pragma once

// P4 内容层：成就判定与解锁 —— docs/ROADMAP-P4.md 任务 1、docs/GAMEPLAY.md §3。
//
// 设计要点：
//   * 39 项定义在 core/Achievements.h（零 Qt），本类只做「统计 → 判定 → 落库 → 广播」；
//   * 判定统一为「指标 >= 阈值」，任何计数器/状态变化后调用 evaluate() 即可，代价是
//     39 次整数比较（不做轮询，只在事件上触发）；
//   * 计数器（摸头次数、投喂次数、任务累计…）持久化在 **meta 表的 `stat.*` 键**，
//     不动 pet_state 表结构、不引入 schema 迁移（见 docs/ROADMAP-P4.md §设计补充）；
//   * 任务相关的 3 个统计（quest_done / quest_allday / quest_fullstreak）由 QuestService
//     通过 reportQuestCompleted() / reportQuestFullDay() 上报，保证同一份计数只有一个写入方。

#include "core/Achievements.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace whalepet::model {
class AchievementRepo;
class Database;
class DiaryRepo;
} // namespace whalepet::model

namespace whalepet::core {
enum class Interaction;
} // namespace whalepet::core

namespace whalepet::viewmodel {

class AchievementService : public QObject {
    Q_OBJECT

public:
    explicit AchievementService(model::Database *db, QObject *parent = nullptr);
    ~AchievementService() override;

    // 读取已解锁集合与 meta 计数器（每次启动调用一次）
    void load();

    // 一次交互（摸头/摸肚子/尾巴/戳/投喂/夸夸/三连击）→ 对应计数器 +1，并顺带判定
    void reportInteraction(core::Interaction type, qint64 nowMs = 0);

    // 「离开 2 小时后回来」（由 PetWindow 在启动时判定后上报）
    void reportComeback(qint64 nowMs = 0);

    // 养成状态快照推送（等级/羁绊/陪伴/连续签到/周签到天数/心情/饱食），并判定
    void setProgress(int level, int bondLevel, std::int64_t companionMs, int streakDays,
                     int weekSigninDays, int mood, int satiety, qint64 nowMs = 0);

    // 任务统计上报（唯一写入方，见文件头注释）
    void reportQuestCompleted(qint64 nowMs = 0);
    void reportQuestFullDay(bool fullDay, qint64 nowMs = 0);

    // 立即判定，返回本次新解锁的成就 id（无新解锁时返回空）
    QList<QString> evaluate(qint64 nowMs = 0);

    bool isUnlocked(const QString &id) const { return m_unlocked.contains(id); }
    int unlockedCount() const { return m_unlocked.size(); }
    const core::AchievementSnapshot &snapshot() const { return m_snapshot; }
    const QHash<QString, qint64> &unlockedMap() const { return m_unlocked; }

signals:
    void unlocked(const QString &id, const QString &name);
    void unlockedCountChanged(int count);

private:
    int bumpStat(core::AchMetric metric, int delta);
    int peakStat(core::AchMetric metric, int value);
    void setStat(core::AchMetric metric, int value);
    int statValue(core::AchMetric metric) const;
    void persistStat(const char *key, int value);
    void syncCountersToSnapshot();

    model::Database *m_db = nullptr;
    std::unique_ptr<model::AchievementRepo> m_repo;
    std::unique_ptr<model::DiaryRepo> m_diary;

    core::AchievementSnapshot m_snapshot;
    QHash<QString, qint64> m_unlocked;
    QHash<QString, int> m_stats; // meta 键（`stat.*`）→ 值
};

} // namespace whalepet::viewmodel
