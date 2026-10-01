#pragma once

// achievements 表 CRUD —— docs/DATA-MODEL.md §3.4。
// 只负责「哪些成就已解锁」，判定逻辑在 whalepet::core::Achievements.h 与 AchievementService。

#include <QHash>
#include <QString>

namespace whalepet::model {

class Database;

class AchievementRepo {
public:
    explicit AchievementRepo(Database *db) : m_db(db) {}

    // id -> unlocked_ms（仅返回已解锁项；空表返回空 hash）
    QHash<QString, qint64> loadUnlocked() const;

    // 幂等解锁：已存在则保留最早的 unlocked_ms（成就只解锁一次）
    bool unlock(const QString &achId, qint64 nowMs);

    bool clear();

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
