#include "model/SigninRepo.h"

#include "model/Database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

namespace {
constexpr int kSigninDayCount = 7;
}

QList<SigninRow> SigninRepo::loadAll() const
{
    QList<SigninRow> out;
    if (m_db == nullptr || !m_db->isOpen()) {
        return out;
    }

    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT day_index, week_key, signed, reward_claimed"
                               " FROM signin ORDER BY day_index ASC"))) {
        qWarning() << "[SigninRepo] loadAll 失败:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        SigninRow r;
        r.dayIndex = q.value(0).toInt();
        r.weekKey = q.value(1).toString();
        r.signedDay = q.value(2).toInt() != 0;
        r.rewardMask = q.value(3).toInt();
        out.append(r);
    }
    return out;
}

bool SigninRepo::resetWeek(const QString &weekKey)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    const bool ownTx = m_db->beginTransaction();
    QSqlQuery del(m_db->db());
    if (!del.exec(QStringLiteral("DELETE FROM signin"))) {
        qWarning() << "[SigninRepo] resetWeek 清空失败:" << del.lastError().text();
        if (ownTx) {
            m_db->rollback();
        }
        return false;
    }

    for (int i = 0; i < kSigninDayCount; ++i) {
        QSqlQuery ins(m_db->db());
        ins.prepare(QStringLiteral("INSERT INTO signin(day_index, week_key, signed, reward_claimed)"
                                  " VALUES(:day, :week, 0, 0)"));
        ins.bindValue(QStringLiteral(":day"), i);
        ins.bindValue(QStringLiteral(":week"), weekKey);
        if (!ins.exec()) {
            qWarning() << "[SigninRepo] resetWeek 写入失败:" << ins.lastError().text();
            if (ownTx) {
                m_db->rollback();
            }
            return false;
        }
    }

    return ownTx ? m_db->commit() : true;
}

bool SigninRepo::saveRow(const SigninRow &row)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO signin(day_index, week_key, signed, reward_claimed)"
        " VALUES(:day, :week, :signed, :mask)"));
    q.bindValue(QStringLiteral(":day"), row.dayIndex);
    q.bindValue(QStringLiteral(":week"), row.weekKey);
    q.bindValue(QStringLiteral(":signed"), row.signedDay ? 1 : 0);
    q.bindValue(QStringLiteral(":mask"), row.rewardMask);
    if (q.exec()) {
        return true;
    }
    qWarning() << "[SigninRepo] saveRow 失败:" << q.lastError().text();
    return false;
}

bool SigninRepo::markReward(int dayIndex, int rewardBit)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    // 位或累加；已置位则影响 0 行（幂等）
    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("UPDATE signin SET reward_claimed = reward_claimed | :bit"
                             " WHERE day_index = :day AND (reward_claimed & :bit) = 0"));
    q.bindValue(QStringLiteral(":bit"), rewardBit);
    q.bindValue(QStringLiteral(":day"), dayIndex);
    if (q.exec()) {
        return q.numRowsAffected() > 0;
    }
    qWarning() << "[SigninRepo] markReward 失败:" << q.lastError().text();
    return false;
}

bool SigninRepo::clear()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    return q.exec(QStringLiteral("DELETE FROM signin"));
}

} // namespace whalepet::model
