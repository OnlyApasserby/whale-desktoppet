#pragma once

// quests 表 CRUD —— docs/DATA-MODEL.md §3.5。
//
// 表以 quest_id 为主键、只有 3 行有效数据（slot 0~2）。「每日刷新」= 事务内整表重写，
// 天然幂等，不需要额外的「今天刷过了吗」状态位。

#include <QList>
#include <QString>

namespace whalepet::model {

class Database;

struct QuestSlot {
    QString id;        // 任务池定义 id（如 "pat-3"）
    int slot = 0;      // 0~2
    int progress = 0;
    int target = 0;
    bool done = false;
    bool claimed = false;
    QString dayKey;    // 属于哪一天（Y-M-D，与 core::dayKey 同格式）
};

class QuestRepo {
public:
    explicit QuestRepo(Database *db) : m_db(db) {}

    // 按 slot 升序返回当前 3 槽（无记录时返回空）
    QList<QuestSlot> loadAll() const;

    // 整表重写：清空后写入给定槽位（每日刷新用）
    bool replaceAll(const QList<QuestSlot> &newSlots);

    bool updateProgress(const QString &questId, int progress, bool done);

    bool markClaimed(const QString &questId);

    bool clear();

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
