#include "model/SettingsRepo.h"

#include "model/Database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

namespace {

bool toBool(const QVariant &v, bool defaultValue)
{
    if (!v.isValid() || v.isNull()) {
        return defaultValue;
    }
    return v.toInt() != 0;
}

} // namespace

bool SettingsRepo::load(SettingsData &out) const
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    if (!q.exec(QStringLiteral("SELECT pos_x, pos_y, pose_size, bubble_enabled, particles_enabled,"
                               " keyword_aware, minigame_enabled, json_ext"
                               " FROM settings WHERE id = 1"))) {
        qWarning() << "[SettingsRepo] load 失败:" << q.lastError().text();
        return false;
    }
    if (!q.next()) {
        return false;
    }

    SettingsData s;
    const QVariant posX = q.value(0);
    const QVariant posY = q.value(1);
    s.hasPosition = posX.isValid() && !posX.isNull() && posY.isValid() && !posY.isNull();
    s.posX = posX.toInt();
    s.posY = posY.toInt();
    s.poseSize = q.value(2).toInt();
    s.bubbleEnabled = toBool(q.value(3), true);
    s.particlesEnabled = toBool(q.value(4), true);
    s.keywordAware = toBool(q.value(5), false);
    s.minigameEnabled = toBool(q.value(6), false);
    s.jsonExt = q.value(7).toString();
    out = s;
    return true;
}

bool SettingsRepo::save(const SettingsData &in)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    QSqlQuery q(m_db->db());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO settings"
        "(id, pos_x, pos_y, pose_size, bubble_enabled, particles_enabled,"
        " keyword_aware, minigame_enabled, json_ext)"
        " VALUES(1, :posX, :posY, :size, :bubble, :particles, :keyword, :minigame, :ext)"));
    q.bindValue(QStringLiteral(":posX"), in.hasPosition ? QVariant(in.posX) : QVariant(QMetaType(QMetaType::Int)));
    q.bindValue(QStringLiteral(":posY"), in.hasPosition ? QVariant(in.posY) : QVariant(QMetaType(QMetaType::Int)));
    q.bindValue(QStringLiteral(":size"), in.poseSize);
    q.bindValue(QStringLiteral(":bubble"), in.bubbleEnabled ? 1 : 0);
    q.bindValue(QStringLiteral(":particles"), in.particlesEnabled ? 1 : 0);
    q.bindValue(QStringLiteral(":keyword"), in.keywordAware ? 1 : 0);
    q.bindValue(QStringLiteral(":minigame"), in.minigameEnabled ? 1 : 0);
    q.bindValue(QStringLiteral(":ext"), in.jsonExt);

    if (q.exec()) {
        return true;
    }
    qWarning() << "[SettingsRepo] save 失败:" << q.lastError().text();
    return false;
}

bool SettingsRepo::clearPosition()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }
    QSqlQuery q(m_db->db());
    if (q.exec(QStringLiteral("UPDATE settings SET pos_x = NULL, pos_y = NULL WHERE id = 1"))) {
        return true;
    }
    qWarning() << "[SettingsRepo] clearPosition 失败:" << q.lastError().text();
    return false;
}

} // namespace whalepet::model
