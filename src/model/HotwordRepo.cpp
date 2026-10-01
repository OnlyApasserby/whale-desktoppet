#include "model/HotwordRepo.h"

#include "core/ChatRules.h"
#include "model/Database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

QString HotwordRepo::normalizeWord(const QString &raw)
{
    return raw.trimmed().toLower();
}

QVector<Hotword> HotwordRepo::loadAll() const
{
    QVector<Hotword> out;
    if (m_db == nullptr || !m_db->isOpen()) {
        return out;
    }

    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT word, keyword_id FROM hotwords ORDER BY id ASC"))) {
        qWarning() << "[HotwordRepo] loadAll 失败:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        Hotword item;
        item.word = q.value(0).toString();
        item.keywordId = q.value(1).toString();
        out.push_back(item);
    }
    return out;
}

bool HotwordRepo::upsert(const QString &word, const QString &keywordId, qint64 nowMs)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    const QString normalized = normalizeWord(word);
    if (normalized.isEmpty()) {
        qWarning() << "[HotwordRepo] 热词为空（trim 后），忽略";
        return false;
    }
    if (!core::keywordIdValid(keywordId.toStdString())) {
        qWarning() << "[HotwordRepo] 关键词 id 非法，拒绝写入:" << keywordId;
        return false;
    }

    // 已存在则「原地更新」，保留原主键 id —— 重新绑定关键词不会改变录入顺序
    // （loadAll 按 id 升序 = 优先级）。不存在才插入。
    // 不用 UPSERT / ON CONFLICT 语法，兼容老 SQLite 驱动。
    QSqlQuery upd(m_db->db());
    upd.prepare(QStringLiteral(
        "UPDATE hotwords SET keyword_id = :k, created_ms = :ms WHERE word = :w"));
    upd.bindValue(QStringLiteral(":k"), keywordId);
    upd.bindValue(QStringLiteral(":ms"), nowMs);
    upd.bindValue(QStringLiteral(":w"), normalized);
    if (!upd.exec()) {
        qWarning() << "[HotwordRepo] upsert 更新失败:" << upd.lastError().text();
        return false;
    }
    if (upd.numRowsAffected() > 0) {
        return true; // 命中既有记录，id 不变
    }

    QSqlQuery ins(m_db->db());
    ins.prepare(QStringLiteral(
        "INSERT INTO hotwords(word, keyword_id, created_ms) VALUES(:w, :k, :ms)"));
    ins.bindValue(QStringLiteral(":w"), normalized);
    ins.bindValue(QStringLiteral(":k"), keywordId);
    ins.bindValue(QStringLiteral(":ms"), nowMs);
    if (ins.exec()) {
        return true;
    }
    qWarning() << "[HotwordRepo] upsert 写入失败:" << ins.lastError().text();
    return false;
}

bool HotwordRepo::remove(const QString &word)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral("DELETE FROM hotwords WHERE word = :w"));
    q.bindValue(QStringLiteral(":w"), normalizeWord(word));
    if (q.exec()) {
        return true;
    }
    qWarning() << "[HotwordRepo] remove 失败:" << q.lastError().text();
    return false;
}

bool HotwordRepo::clear()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    return q.exec(QStringLiteral("DELETE FROM hotwords"));
}

} // namespace whalepet::model
