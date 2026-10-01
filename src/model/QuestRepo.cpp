#include "model/QuestRepo.h"

#include "model/Database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

QList<QuestSlot> QuestRepo::loadAll() const
{
    QList<QuestSlot> out;
    if (m_db == nullptr || !m_db->isOpen()) {
        return out;
    }

    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT quest_id, slot, progress, target, done, claimed, day_key"
                               " FROM quests ORDER BY slot ASC"))) {
        qWarning() << "[QuestRepo] loadAll 失败:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        QuestSlot s;
        s.id = q.value(0).toString();
        s.slot = q.value(1).toInt();
        s.progress = q.value(2).toInt();
        s.target = q.value(3).toInt();
        s.done = q.value(4).toInt() != 0;
        s.claimed = q.value(5).toInt() != 0;
        s.dayKey = q.value(6).toString();
        out.append(s);
    }
    return out;
}

bool QuestRepo::replaceAll(const QList<QuestSlot> &newSlots)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    const bool ownTx = m_db->beginTransaction();
    QSqlQuery del(m_db->db());
    if (!del.exec(QStringLiteral("DELETE FROM quests"))) {
        qWarning() << "[QuestRepo] replaceAll 清空失败:" << del.lastError().text();
        if (ownTx) {
            m_db->rollback();
        }
        return false;
    }

    for (const QuestSlot &s : newSlots) {
        QSqlQuery ins(m_db->db());
        ins.prepare(QStringLiteral(
            "INSERT INTO quests(quest_id, slot, progress, target, done, claimed, day_key)"
            " VALUES(:id, :slot, :progress, :target, :done, :claimed, :day)"));
        ins.bindValue(QStringLiteral(":id"), s.id);
        ins.bindValue(QStringLiteral(":slot"), s.slot);
        ins.bindValue(QStringLiteral(":progress"), s.progress);
        ins.bindValue(QStringLiteral(":target"), s.target);
        ins.bindValue(QStringLiteral(":done"), s.done ? 1 : 0);
        ins.bindValue(QStringLiteral(":claimed"), s.claimed ? 1 : 0);
        ins.bindValue(QStringLiteral(":day"), s.dayKey);
        if (!ins.exec()) {
            qWarning() << "[QuestRepo] replaceAll 写入失败:" << ins.lastError().text();
            if (ownTx) {
                m_db->rollback();
            }
            return false;
        }
    }

    return ownTx ? m_db->commit() : true;
}

bool QuestRepo::updateProgress(const QString &questId, int progress, bool done)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("UPDATE quests SET progress = :progress, done = :done"
                             " WHERE quest_id = :id"));
    q.bindValue(QStringLiteral(":progress"), progress);
    q.bindValue(QStringLiteral(":done"), done ? 1 : 0);
    q.bindValue(QStringLiteral(":id"), questId);
    if (q.exec()) {
        return true;
    }
    qWarning() << "[QuestRepo] updateProgress 失败:" << q.lastError().text();
    return false;
}

bool QuestRepo::markClaimed(const QString &questId)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    // claimed 只在 done 且尚未领取时成立。注意：SQLite 的 numRowsAffected 会把
    // 「WHERE 匹配但 SET 值未变化」的行也算作已改动，因此仅靠 done = 1 无法识别
    // 重复领取；必须把 claimed = 0 一并写进 WHERE，第二次调用才会真正返回 false。
    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral(
        "UPDATE quests SET claimed = 1 WHERE quest_id = :id AND done = 1 AND claimed = 0"));
    q.bindValue(QStringLiteral(":id"), questId);
    if (q.exec()) {
        return q.numRowsAffected() > 0;
    }
    qWarning() << "[QuestRepo] markClaimed 失败:" << q.lastError().text();
    return false;
}

bool QuestRepo::clear()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    return q.exec(QStringLiteral("DELETE FROM quests"));
}

} // namespace whalepet::model
