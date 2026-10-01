#pragma once

// bond_diary 表 CRUD —— docs/DATA-MODEL.md §3.7、docs/GAMEPLAY.md §6「成长日记」。
//
// 写入规则（与 docs/GAMEPLAY.md 一致）：
//   * 只保留最新 80 条（超出即裁剪）；
//   * **同一天、同 kind、同 detail 只记一条**（去重按本地自然日判断）。

#include <QList>
#include <QString>

namespace whalepet::model {

class Database;

struct DiaryEntry {
    qint64 id = 0;
    QString kind;   // achievement / quest / levelup / bond / feed / minigame
    QString detail; // 展示文案
    qint64 tsMs = 0;
};

class DiaryRepo {
public:
    explicit DiaryRepo(Database *db) : m_db(db) {}

    bool append(const QString &kind, const QString &detail, qint64 tsMs);

    // 同日同类同文案去重版本：已存在则返回 false 且不写入
    bool appendDaily(const QString &kind, const QString &detail, qint64 tsMs);

    // 倒序（最新在前）
    QList<DiaryEntry> loadRecent(int limit) const;

    int count() const;

    // 只保留最新 maxEntries 条
    bool trimTo(int maxEntries);

    bool clear();

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
