#include "model/SettingsRepo.h"

#include "model/Database.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
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

// json_ext 文本 → 对象。空串 / 非法 JSON / 非对象一律返回空对象（不抛错，不静默改写语义）。
QJsonObject parseExtObject(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

// P6 扩展设置项在 json_ext 中的键名（SETTINGS.md §3）
const char *const kKeyPetEnabled = "pet_enabled";
const char *const kKeyNightQuiet = "night_quiet";
const char *const kKeyDragInertia = "drag_inertia";
// 小游戏（扫雷）难度选择
const char *const kKeyMiniGamePreset = "minigame_preset";
const char *const kKeyMiniGameWidth = "minigame_custom_width";
const char *const kKeyMiniGameHeight = "minigame_custom_height";
const char *const kKeyMiniGameMines = "minigame_custom_mines";
const char *const kKeyKittenDifficulty = "kitten_difficulty";

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
    s.minigameEnabled = toBool(q.value(6), true);
    s.jsonExt = q.value(7).toString();

    // P6：从 json_ext 取扩展设置项；键缺失 / 值非法时沿用默认值（向后兼容老库）
    const QJsonObject ext = parseExtObject(s.jsonExt);
    s.petEnabled = ext.value(QLatin1String(kKeyPetEnabled)).toBool(true);
    s.nightQuiet = ext.value(QLatin1String(kKeyNightQuiet)).toBool(true);
    s.dragInertia = ext.value(QLatin1String(kKeyDragInertia)).toBool(true);
    s.minigamePreset = ext.value(QLatin1String(kKeyMiniGamePreset)).toInt(0);
    s.minigameCustomWidth = ext.value(QLatin1String(kKeyMiniGameWidth)).toInt(9);
    s.minigameCustomHeight = ext.value(QLatin1String(kKeyMiniGameHeight)).toInt(9);
    s.minigameCustomMines = ext.value(QLatin1String(kKeyMiniGameMines)).toInt(10);
    s.kittenDifficulty = ext.value(QLatin1String(kKeyKittenDifficulty)).toInt(0);

    out = s;
    return true;
}

bool SettingsRepo::save(const SettingsData &in)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return false;
    }

    // P6：把扩展设置项合并进 json_ext（保留已有未知键），再整串写回
    QJsonObject ext = parseExtObject(in.jsonExt);
    ext.insert(QLatin1String(kKeyPetEnabled), in.petEnabled);
    ext.insert(QLatin1String(kKeyNightQuiet), in.nightQuiet);
    ext.insert(QLatin1String(kKeyDragInertia), in.dragInertia);
    ext.insert(QLatin1String(kKeyMiniGamePreset), in.minigamePreset);
    ext.insert(QLatin1String(kKeyMiniGameWidth), in.minigameCustomWidth);
    ext.insert(QLatin1String(kKeyMiniGameHeight), in.minigameCustomHeight);
    ext.insert(QLatin1String(kKeyMiniGameMines), in.minigameCustomMines);
    ext.insert(QLatin1String(kKeyKittenDifficulty), in.kittenDifficulty);
    const QString extText = QString::fromUtf8(QJsonDocument(ext).toJson(QJsonDocument::Compact));

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
    q.bindValue(QStringLiteral(":ext"), extText);

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
