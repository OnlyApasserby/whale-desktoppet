#include "model/DiaryRepo.h"

#include "model/Database.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>
#include <QVariant>

namespace whalepet::model {

namespace {

// 本地自然日区间 [lo, hi)，用于「同一天同类只记一条」的判定
// （QDateTime(QDate, QTime) 默认即本地时区，与 core::dayKey 的本地日口径一致）
bool localDayRange(qint64 tsMs, qint64 *loOut, qint64 *hiOut)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(tsMs);
    const QDateTime start(dt.date(), QTime(0, 0));
    if (!start.isValid()) {
        return false;
    }
    *loOut = start.toMSecsSinceEpoch();
    *hiOut = start.addDays(1).toMSecsSinceEpoch();
    return true;
}

} // namespace

bool DiaryRepo::append(const QString &kind, const QString &detail, qint64 tsMs)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("INSERT INTO bond_diary(kind, detail, ts_ms) VALUES(:kind, :detail, :ts)"));
    q.bindValue(QStringLiteral(":kind"), kind);
    q.bindValue(QStringLiteral(":detail"), detail);
    q.bindValue(QStringLiteral(":ts"), tsMs);
    if (q.exec()) {
        return true;
    }
    qWarning() << "[DiaryRepo] append 失败:" << q.lastError().text();
    return false;
}

bool DiaryRepo::appendDaily(const QString &kind, const QString &detail, qint64 tsMs)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    qint64 lo = 0;
    qint64 hi = 0;
    if (localDayRange(tsMs, &lo, &hi)) {
        QSqlQuery check(m_db->db());
        check.prepare(QStringLiteral("SELECT COUNT(*) FROM bond_diary"
                                     " WHERE kind = :kind AND detail = :detail"
                                     " AND ts_ms >= :lo AND ts_ms < :hi"));
        check.bindValue(QStringLiteral(":kind"), kind);
        check.bindValue(QStringLiteral(":detail"), detail);
        check.bindValue(QStringLiteral(":lo"), lo);
        check.bindValue(QStringLiteral(":hi"), hi);
        if (check.exec() && check.next() && check.value(0).toInt() > 0) {
            return false; // 当天已记过同类同文案
        }
    }

    return append(kind, detail, tsMs);
}

QList<DiaryEntry> DiaryRepo::loadRecent(int limit) const
{
    QList<DiaryEntry> out;
    if (m_db == nullptr || !m_db->isOpen()) {
        return out;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("SELECT id, kind, detail, ts_ms FROM bond_diary"
                             " ORDER BY id DESC LIMIT :limit"));
    q.bindValue(QStringLiteral(":limit"), limit);
    if (!q.exec()) {
        qWarning() << "[DiaryRepo] loadRecent 失败:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        DiaryEntry e;
        e.id = q.value(0).toLongLong();
        e.kind = q.value(1).toString();
        e.detail = q.value(2).toString();
        e.tsMs = q.value(3).toLongLong();
        out.append(e);
    }
    return out;
}

int DiaryRepo::count() const
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return 0;
    }
    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM bond_diary")) || !q.next()) {
        qWarning() << "[DiaryRepo] count 失败:" << q.lastError().text();
        return 0;
    }
    return q.value(0).toInt();
}

bool DiaryRepo::trimTo(int maxEntries)
{
    if (m_db == nullptr || !m_db->isOpen() || maxEntries <= 0) {
        return false;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("DELETE FROM bond_diary WHERE id NOT IN"
                             " (SELECT id FROM bond_diary ORDER BY id DESC LIMIT :n)"));
    q.bindValue(QStringLiteral(":n"), maxEntries);
    if (q.exec()) {
        return true;
    }
    qWarning() << "[DiaryRepo] trimTo 失败:" << q.lastError().text();
    return false;
}

bool DiaryRepo::clear()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    return q.exec(QStringLiteral("DELETE FROM bond_diary"));
}

} // namespace whalepet::model
