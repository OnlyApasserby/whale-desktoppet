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
// 小游戏（国际象棋）：引擎路径 / 棋力档位 / 执子
const char *const kKeyChessEnginePath = "chess_engine_path";
const char *const kKeyChessDifficulty = "chess_difficulty";
const char *const kKeyChessHumanIsWhite = "chess_human_is_white";
// P7：工作状态感知与本地 Context API（docs/CONTEXT-API.md §5）
const char *const kKeyWorkAwareEnabled = "work_aware_enabled";
const char *const kKeyContextApiEnabled = "context_api_enabled";
const char *const kKeyContextApiPort = "context_api_port";
const char *const kKeyContextApiToken = "context_api_token";
// P7.5：ACP / IDE 显式信号（docs/CONTEXT-API.md §6）
const char *const kKeyAcpEnabled = "acp_enabled";
const char *const kKeyAcpSignalPath = "acp_signal_path";
// P7.6：ACP（Agent Client Protocol）客户端（docs/ACP-EVAL.md）
const char *const kKeyAcpDshPath = "acp_dsh_path";
const char *const kKeyAcpProfile = "acp_profile";
const char *const kKeyAcpWorkspace = "acp_workspace";
// EX1.4：游戏陪玩（docs/ROADMAP-ex1.md EX1.4）
const char *const kKeyGameCompanionEnabled = "game_companion_enabled";
const char *const kKeyGameProfilePath = "game_profile_path";
// P8：预设对话 + 彩云天气（docs/DIALOGUE.md、docs/SETTINGS.md §4）
const char *const kKeyDialogueEnabled = "dialogue_enabled";
const char *const kKeyWeatherKey = "weather_key";
const char *const kKeyWeatherLocation = "weather_location";

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
    s.chessEnginePath = ext.value(QLatin1String(kKeyChessEnginePath)).toString();
    s.chessDifficulty = ext.value(QLatin1String(kKeyChessDifficulty)).toInt(0);
    s.chessHumanIsWhite = ext.value(QLatin1String(kKeyChessHumanIsWhite)).toBool(true);
    // P7：感知与 Context API（缺省即默认：均关闭）
    s.workAwareEnabled = ext.value(QLatin1String(kKeyWorkAwareEnabled)).toBool(false);
    s.contextApiEnabled = ext.value(QLatin1String(kKeyContextApiEnabled)).toBool(false);
    s.contextApiPort = ext.value(QLatin1String(kKeyContextApiPort)).toInt(0);
    s.contextApiToken = ext.value(QLatin1String(kKeyContextApiToken)).toString();
    // P7.5：ACP 显式信号（缺省即默认：关闭）
    s.acpEnabled = ext.value(QLatin1String(kKeyAcpEnabled)).toBool(false);
    s.acpSignalPath = ext.value(QLatin1String(kKeyAcpSignalPath)).toString();
    // P7.6：ACP 客户端（dsh 路径为空 = 不启动子进程）
    s.acpDshPath = ext.value(QLatin1String(kKeyAcpDshPath)).toString();
    s.acpProfile = ext.value(QLatin1String(kKeyAcpProfile)).toString();
    s.acpWorkspace = ext.value(QLatin1String(kKeyAcpWorkspace)).toString();
    // EX1.4：游戏陪玩（缺省即默认：关闭 / 未指定档案）
    s.gameCompanionEnabled = ext.value(QLatin1String(kKeyGameCompanionEnabled)).toBool(false);
    s.gameProfilePath = ext.value(QLatin1String(kKeyGameProfilePath)).toString();
    // P8：预设对话（默认开）+ 彩云天气（默认空 = 不联网）
    s.dialogueEnabled = ext.value(QLatin1String(kKeyDialogueEnabled)).toBool(true);
    s.weatherKey = ext.value(QLatin1String(kKeyWeatherKey)).toString();
    s.weatherLocation = ext.value(QLatin1String(kKeyWeatherLocation)).toString();

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
    ext.insert(QLatin1String(kKeyChessEnginePath), in.chessEnginePath);
    ext.insert(QLatin1String(kKeyChessDifficulty), in.chessDifficulty);
    ext.insert(QLatin1String(kKeyChessHumanIsWhite), in.chessHumanIsWhite);
    ext.insert(QLatin1String(kKeyWorkAwareEnabled), in.workAwareEnabled);
    ext.insert(QLatin1String(kKeyContextApiEnabled), in.contextApiEnabled);
    ext.insert(QLatin1String(kKeyContextApiPort), in.contextApiPort);
    ext.insert(QLatin1String(kKeyContextApiToken), in.contextApiToken);
    ext.insert(QLatin1String(kKeyAcpEnabled), in.acpEnabled);
    ext.insert(QLatin1String(kKeyAcpSignalPath), in.acpSignalPath);
    ext.insert(QLatin1String(kKeyAcpDshPath), in.acpDshPath);
    ext.insert(QLatin1String(kKeyAcpProfile), in.acpProfile);
    ext.insert(QLatin1String(kKeyAcpWorkspace), in.acpWorkspace);
    ext.insert(QLatin1String(kKeyGameCompanionEnabled), in.gameCompanionEnabled);
    ext.insert(QLatin1String(kKeyGameProfilePath), in.gameProfilePath);
    ext.insert(QLatin1String(kKeyDialogueEnabled), in.dialogueEnabled);
    ext.insert(QLatin1String(kKeyWeatherKey), in.weatherKey);
    ext.insert(QLatin1String(kKeyWeatherLocation), in.weatherLocation);
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
