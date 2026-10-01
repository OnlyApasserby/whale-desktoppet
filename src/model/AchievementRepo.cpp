#include "model/AchievementRepo.h"

#include "model/Database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

QHash<QString, qint64> AchievementRepo::loadUnlocked() const
{
    QHash<QString, qint64> out;
    if (m_db == nullptr || !m_db->isOpen()) {
        return out;
    }

    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT ach_id, unlocked_ms FROM achievements WHERE unlocked = 1"))) {
        qWarning() << "[AchievementRepo] loadUnlocked 失败:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        out.insert(q.value(0).toString(), q.value(1).toLongLong());
    }
    return out;
}

bool AchievementRepo::unlock(const QString &achId, qint64 nowMs)
{
    if (m_db == nullptr || !m_db->isOpen() || achId.isEmpty()) {
        return false;
    }

    // INSERT OR IGNORE：重复解锁不覆盖首次解锁时间（成就只点亮一次）
    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO achievements(ach_id, unlocked, unlocked_ms)"
                             " VALUES(:id, 1, :ms)"));
    q.bindValue(QStringLiteral(":id"), achId);
    q.bindValue(QStringLiteral(":ms"), nowMs);
    if (q.exec()) {
        return true;
    }
    qWarning() << "[AchievementRepo] unlock 失败:" << q.lastError().text();
    return false;
}

bool AchievementRepo::clear()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    return q.exec(QStringLiteral("DELETE FROM achievements"));
}

} // namespace whalepet::model
