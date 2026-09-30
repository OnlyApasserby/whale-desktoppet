#include "model/Schema.h"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

namespace {

bool exec(QSqlDatabase &db, const QString &sql, QString *errorOut)
{
    QSqlQuery q(db);
    if (q.exec(sql)) {
        return true;
    }
    if (errorOut != nullptr) {
        *errorOut = q.lastError().text() + QStringLiteral(" | SQL: ") + sql;
    }
    return false;
}

} // namespace

bool Schema::ensureMeta(QSqlDatabase &db)
{
    return exec(db,
                QStringLiteral("CREATE TABLE IF NOT EXISTS meta ("
                               "  key TEXT PRIMARY KEY,"
                               "  value TEXT"
                               ")"),
                nullptr);
}

QStringList Schema::statementsV1()
{
    return {
        // 3.2 pet_state（单例，id 恒为 1）
        QStringLiteral("CREATE TABLE IF NOT EXISTS pet_state ("
                       "  id INTEGER PRIMARY KEY,"
                       "  level INTEGER NOT NULL DEFAULT 1,"
                       "  exp INTEGER NOT NULL DEFAULT 0,"
                       "  coins INTEGER NOT NULL DEFAULT 0,"
                       "  mood INTEGER NOT NULL DEFAULT 70,"
                       "  affinity INTEGER NOT NULL DEFAULT 0,"
                       "  satiety INTEGER NOT NULL DEFAULT 80,"
                       "  bond_level INTEGER NOT NULL DEFAULT 1,"
                       "  companion_ms INTEGER NOT NULL DEFAULT 0,"
                       "  streak_days INTEGER NOT NULL DEFAULT 0,"
                       "  last_active_ms INTEGER NOT NULL DEFAULT 0,"
                       "  updated_ms INTEGER NOT NULL DEFAULT 0"
                       ")"),
        // 3.3 settings（单例，id 恒为 1；pos_x/pos_y 可空表示「未设置」）
        QStringLiteral("CREATE TABLE IF NOT EXISTS settings ("
                       "  id INTEGER PRIMARY KEY,"
                       "  pos_x INTEGER,"
                       "  pos_y INTEGER,"
                       "  pose_size INTEGER NOT NULL DEFAULT 200,"
                       "  bubble_enabled INTEGER NOT NULL DEFAULT 1,"
                       "  particles_enabled INTEGER NOT NULL DEFAULT 1,"
                       "  keyword_aware INTEGER NOT NULL DEFAULT 0,"
                       "  minigame_enabled INTEGER NOT NULL DEFAULT 0,"
                       "  json_ext TEXT"
                       ")"),
        // 3.4 achievements（P4 使用，v1 即建表，避免 P4 追加迁移）
        QStringLiteral("CREATE TABLE IF NOT EXISTS achievements ("
                       "  ach_id TEXT PRIMARY KEY,"
                       "  unlocked INTEGER NOT NULL DEFAULT 0,"
                       "  unlocked_ms INTEGER NOT NULL DEFAULT 0"
                       ")"),
        // 3.5 quests（每日任务）
        QStringLiteral("CREATE TABLE IF NOT EXISTS quests ("
                       "  quest_id TEXT PRIMARY KEY,"
                       "  slot INTEGER NOT NULL DEFAULT 0,"
                       "  progress INTEGER NOT NULL DEFAULT 0,"
                       "  target INTEGER NOT NULL DEFAULT 0,"
                       "  done INTEGER NOT NULL DEFAULT 0,"
                       "  claimed INTEGER NOT NULL DEFAULT 0,"
                       "  day_key TEXT"
                       ")"),
        // 3.6 signin（周签到）
        QStringLiteral("CREATE TABLE IF NOT EXISTS signin ("
                       "  day_index INTEGER PRIMARY KEY,"
                       "  week_key TEXT,"
                       "  signed INTEGER NOT NULL DEFAULT 0,"
                       "  reward_claimed INTEGER NOT NULL DEFAULT 0"
                       ")"),
        // 3.7 bond_diary（成长日记）
        QStringLiteral("CREATE TABLE IF NOT EXISTS bond_diary ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  kind TEXT,"
                       "  detail TEXT,"
                       "  ts_ms INTEGER NOT NULL DEFAULT 0"
                       ")"),
        // 3.8 chat_history（可选，用于最近发言去重）
        QStringLiteral("CREATE TABLE IF NOT EXISTS chat_history ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  line_key TEXT,"
                       "  text TEXT,"
                       "  ts_ms INTEGER NOT NULL DEFAULT 0"
                       ")"),
    };
}

int Schema::readVersion(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'"))) {
        return 0;
    }
    if (!q.next()) {
        return 0;
    }
    bool ok = false;
    const int v = q.value(0).toString().toInt(&ok);
    return ok ? v : 0;
}

bool Schema::writeVersion(QSqlDatabase &db, int version)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES('schema_version', :v)"
                             " ON CONFLICT(key) DO UPDATE SET value = :v"));
    q.bindValue(QStringLiteral(":v"), QString::number(version));
    if (q.exec()) {
        return true;
    }
    // 老驱动不支持 UPSERT：退回「删后插」
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM meta WHERE key = 'schema_version'"));
    if (!del.exec()) {
        qWarning() << "[Schema] writeVersion delete 失败:" << del.lastError().text();
        return false;
    }
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO meta(key, value) VALUES('schema_version', :v)"));
    ins.bindValue(QStringLiteral(":v"), QString::number(version));
    if (!ins.exec()) {
        qWarning() << "[Schema] writeVersion insert 失败:" << ins.lastError().text();
        return false;
    }
    return true;
}

bool Schema::migrate(QSqlDatabase &db)
{
    if (!ensureMeta(db)) {
        qWarning() << "[Schema] 创建 meta 表失败";
        return false;
    }

    const int from = readVersion(db);
    if (from > kVersion) {
        // 库比程序新：不破坏、不降级写入，仅告警（可能被旧版本程序打开）
        qWarning() << "[Schema] 库版本" << from << "高于程序支持的" << kVersion
                   << "，跳过迁移（只读兼容）";
        return true;
    }

    QString error;
    for (const QString &sql : statementsV1()) {
        if (!exec(db, sql, &error)) {
            qWarning() << "[Schema] 建表失败:" << error;
            return false;
        }
    }

    if (from != kVersion) {
        if (!writeVersion(db, kVersion)) {
            qWarning() << "[Schema] 写入 schema_version 失败";
            return false;
        }
        qInfo() << "[Schema] schema 迁移完成:" << from << "->" << kVersion;
    }
    return true;
}

} // namespace whalepet::model
