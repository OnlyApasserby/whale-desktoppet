#include "model/PetStateRepo.h"

#include "model/Database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

bool PetStateRepo::load(PetStateData &out) const
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT level, exp, coins, mood, affinity, satiety, bond_level,"
                               " companion_ms, streak_days, last_active_ms, updated_ms"
                               " FROM pet_state WHERE id = 1"))) {
        qWarning() << "[PetStateRepo] load 失败:" << q.lastError().text();
        return false;
    }
    if (!q.next()) {
        return false;
    }

    PetStateData s;
    s.level = q.value(0).toInt();
    s.exp = q.value(1).toInt();
    s.coins = q.value(2).toInt();
    s.mood = q.value(3).toInt();
    s.affinity = q.value(4).toInt();
    s.satiety = q.value(5).toInt();
    s.bondLevel = q.value(6).toInt();
    s.companionMs = q.value(7).toLongLong();
    s.streakDays = q.value(8).toInt();
    s.lastActiveMs = q.value(9).toLongLong();
    s.updatedMs = q.value(10).toLongLong();
    out = s;
    return true;
}

bool PetStateRepo::save(const PetStateData &in)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO pet_state"
        "(id, level, exp, coins, mood, affinity, satiety, bond_level,"
        " companion_ms, streak_days, last_active_ms, updated_ms)"
        " VALUES(1, :level, :exp, :coins, :mood, :affinity, :satiety, :bond,"
        " :companion, :streak, :lastActive, :updated)"));
    q.bindValue(QStringLiteral(":level"), in.level);
    q.bindValue(QStringLiteral(":exp"), in.exp);
    q.bindValue(QStringLiteral(":coins"), in.coins);
    q.bindValue(QStringLiteral(":mood"), in.mood);
    q.bindValue(QStringLiteral(":affinity"), in.affinity);
    q.bindValue(QStringLiteral(":satiety"), in.satiety);
    q.bindValue(QStringLiteral(":bond"), in.bondLevel);
    q.bindValue(QStringLiteral(":companion"), in.companionMs);
    q.bindValue(QStringLiteral(":streak"), in.streakDays);
    q.bindValue(QStringLiteral(":lastActive"), in.lastActiveMs);
    q.bindValue(QStringLiteral(":updated"), in.updatedMs);

    if (q.exec()) {
        return true;
    }
    qWarning() << "[PetStateRepo] save 失败:" << q.lastError().text();
    return false;
}

bool PetStateRepo::clear()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    return q.exec(QStringLiteral("DELETE FROM pet_state WHERE id = 1"));
}

} // namespace whalepet::model
