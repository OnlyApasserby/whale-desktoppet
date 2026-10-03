#include "gamestate/RpgMakerBridgeAdapter.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

namespace whalepet::gamestate {

namespace {

bool readFileSnapshot(const QString &path, const QString &format, QString *json, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("无法打开桥接状态文件：%1").arg(path);
        }
        return false;
    }
    const QByteArray payload = file.readAll();
    if (format == QStringLiteral("jsonl")) {
        const QList<QByteArray> lines = payload.split('\n');
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            const QByteArray trimmed = it->trimmed();
            if (!trimmed.isEmpty()) {
                if (json != nullptr) {
                    *json = QString::fromUtf8(trimmed);
                }
                return true;
            }
        }
        if (error != nullptr) {
            *error = QStringLiteral("桥接 jsonl 文件没有非空行：%1").arg(path);
        }
        return false;
    }
    if (json != nullptr) {
        *json = QString::fromUtf8(payload);
    }
    return true;
}

bool readSocketSnapshot(const QString &endpoint, QString *json, QString *error)
{
    const QStringList parts = endpoint.split(QLatin1Char(':'));
    bool ok = false;
    const quint16 port = parts.size() == 2 ? parts.at(1).toUShort(&ok) : 0;
    if (!ok) {
        if (error != nullptr) {
            *error = QStringLiteral("桥接 socket 端点格式应为 host:port，实际：%1").arg(endpoint);
        }
        return false;
    }
    QTcpSocket socket;
    socket.connectToHost(parts.at(0), port);
    if (!socket.waitForConnected(1500)) {
        if (error != nullptr) {
            *error = QStringLiteral("桥接 socket 连接失败：%1").arg(endpoint);
        }
        return false;
    }
    QByteArray buffer;
    while (!buffer.contains('\n') && socket.waitForReadyRead(1500)) {
        buffer += socket.readAll();
    }
    const int newline = buffer.indexOf('\n');
    if (newline >= 0) {
        buffer = buffer.left(newline);
    }
    if (buffer.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("桥接 socket 无数据：%1").arg(endpoint);
        }
        return false;
    }
    if (json != nullptr) {
        *json = QString::fromUtf8(buffer);
    }
    return true;
}

} // namespace

RpgMakerBridgeAdapter::RpgMakerBridgeAdapter() = default;
RpgMakerBridgeAdapter::~RpgMakerBridgeAdapter() = default;

bool RpgMakerBridgeAdapter::engineMatches(const std::string &engine)
{
    return engine == "rpgmaker-rgss" || engine == "rpgmaker-mv" || engine == "rpgmaker-mz";
}

bool RpgMakerBridgeAdapter::supports(const GameProfile &profile)
{
    if (!engineMatches(profile.engine) || profile.bridge.isEmpty()) {
        return false;
    }
    const QString kind = profile.bridge.value(QStringLiteral("kind")).toString();
    return kind == QStringLiteral("file") || kind == QStringLiteral("socket");
}

void RpgMakerBridgeAdapter::setSnapshotProvider(SnapshotProvider provider)
{
    m_provider = std::move(provider);
}

bool RpgMakerBridgeAdapter::attach(const GameProfile &profile, QString *error)
{
    if (!engineMatches(profile.engine)) {
        if (error != nullptr) {
            *error = QStringLiteral("适配器与档案引擎不匹配：期望 rpgmaker-*，档案为 %1")
                         .arg(QString::fromStdString(profile.engine));
        }
        return false;
    }
    if (!supports(profile)) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "RPG Maker 桥接需要 profile.bridge（kind=file|socket、path、format）。"
                "RGSS 不经内存读取，须由用户侧**只读**脚本输出状态快照。");
        }
        return false;
    }

    m_profile = profile;
    const QString kind = m_profile.bridge.value(QStringLiteral("kind")).toString();
    const QString path = m_profile.bridge.value(QStringLiteral("path")).toString();
    const QString format = m_profile.bridge.value(QStringLiteral("format"))
                               .toString(QStringLiteral("json"));

    m_applyProbe = m_profile.bridge.value(QStringLiteral("probe")).toBool(false);

    if (m_provider == nullptr) {
        if (kind == QStringLiteral("file")) {
            m_provider = [path, format](QString *json, QString *error) {
                return readFileSnapshot(path, format, json, error);
            };
        } else {
            m_provider = [path](QString *json, QString *error) {
                return readSocketSnapshot(path, json, error);
            };
        }
    }
    m_attached = true;
    return true;
}

void RpgMakerBridgeAdapter::detach()
{
    m_attached = false;
    m_sceneDetector.reset();
}

bool RpgMakerBridgeAdapter::attached() const
{
    return m_attached;
}

bool RpgMakerBridgeAdapter::readSnapshot(QString *json, QString *error) const
{
    if (m_provider == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("桥接快照来源未配置");
        }
        return false;
    }
    return m_provider(json, error);
}

bool RpgMakerBridgeAdapter::read(core::GameSample *out, QString *error)
{
    if (!m_attached || m_provider == nullptr) {
        if (out != nullptr) {
            out->available = false;
        }
        if (error != nullptr) {
            *error = QStringLiteral("桥接适配器尚未 attach");
        }
        return false;
    }
    if (out == nullptr) {
        return false;
    }

    QString json;
    if (!readSnapshot(&json, error)) {
        out->available = false;
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        out->available = false;
        if (error != nullptr) {
            *error = QStringLiteral("桥接快照 JSON 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject obj = doc.object();
    out->available = obj.value(QStringLiteral("available")).toBool(true);
    if (obj.contains(QStringLiteral("hp"))) {
        out->hp = obj.value(QStringLiteral("hp")).toDouble();
    }
    if (obj.contains(QStringLiteral("hpMax"))) {
        out->hpMax = obj.value(QStringLiteral("hpMax")).toDouble();
    }
    if (obj.contains(QStringLiteral("gold"))) {
        out->gold = static_cast<long long>(obj.value(QStringLiteral("gold")).toDouble());
    }
    if (obj.contains(QStringLiteral("level"))) {
        out->level = obj.value(QStringLiteral("level")).toInt();
    }
    if (obj.contains(QStringLiteral("posX"))) {
        out->posX = obj.value(QStringLiteral("posX")).toDouble();
    }
    if (obj.contains(QStringLiteral("posY"))) {
        out->posY = obj.value(QStringLiteral("posY")).toDouble();
    }
    if (obj.contains(QStringLiteral("mapName"))) {
        out->mapName = obj.value(QStringLiteral("mapName")).toString().toStdString();
    }

    int specialScene = obj.value(QStringLiteral("specialScene")).toInt(0);
    if (m_applyProbe && obj.contains(QStringLiteral("probe"))) {
        specialScene = static_cast<int>(
            m_sceneDetector.update(obj.value(QStringLiteral("probe")).toObject()));
    }
    out->specialScene = specialScene;
    return true;
}

} // namespace whalepet::gamestate
