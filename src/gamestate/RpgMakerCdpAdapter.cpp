#include "gamestate/RpgMakerCdpAdapter.h"

#include "gamestate/ChainSampler.h"

#include <QJsonArray>
#include <QJsonObject>

namespace whalepet::gamestate {

namespace {

RpgMakerSpecialSceneConfig configFromJson(const QJsonObject &json)
{
    RpgMakerSpecialSceneConfig config;
    const QJsonObject scene = json.value(QStringLiteral("specialScene")).toObject();
    if (scene.isEmpty()) {
        return config;
    }
    const QJsonArray names = scene.value(QStringLiteral("sceneNames")).toArray();
    for (const QJsonValue &name : names) {
        config.sceneNames.push_back(name.toString().toStdString());
    }
    if (scene.contains(QStringLiteral("coverRatio"))) {
        config.coverRatio = scene.value(QStringLiteral("coverRatio")).toDouble(config.coverRatio);
    }
    return config;
}

} // namespace

RpgMakerCdpAdapter::RpgMakerCdpAdapter() = default;
RpgMakerCdpAdapter::~RpgMakerCdpAdapter() = default;

bool RpgMakerCdpAdapter::engineMatches(const std::string &engine)
{
    return engine == "rpgmaker-mv" || engine == "rpgmaker-mz";
}

bool RpgMakerCdpAdapter::supports(const GameProfile &profile)
{
    if (!engineMatches(profile.engine)) {
        return false;
    }
    if (!profile.rpgmaker.value(QStringLiteral("wsUrl")).toString().isEmpty()) {
        return true;
    }
    return profile.rpgmaker.value(QStringLiteral("cdpPort")).toInt(0) > 0;
}

QString RpgMakerCdpAdapter::builtinProbeExpression()
{
    // 全部包在 try/catch，且**绝不写游戏状态**；缺失对象时返回空结构而非抛错。
    return QStringLiteral(R"((function(){
  try {
    var pics = [];
    if (typeof $gameScreen !== 'undefined' && $gameScreen._pictures) {
      for (var k in $gameScreen._pictures) {
        var pic = $gameScreen._pictures[k];
        if (!pic) continue;
        pics.push({
          id: pic._pictureId, n: pic._name, op: pic._opacity,
          sx: pic._scaleX, sy: pic._scaleY, x: pic._x, y: pic._y, o: pic._origin,
          bw: (pic._bitmap ? pic._bitmap.width : 0),
          bh: (pic._bitmap ? pic._bitmap.height : 0)
        });
      }
    }
    var video = false;
    try {
      if (typeof SceneManager !== 'undefined' && SceneManager._scene && SceneManager._scene.constructor) {
        video = /Video/.test(SceneManager._scene.constructor.name);
      }
    } catch (e) {}
    var msg = false, face = '';
    try {
      if (typeof $gameMessage !== 'undefined' && $gameMessage && $gameMessage.isBusy && $gameMessage.isBusy()) {
        msg = true;
        face = $gameMessage.faceName ? $gameMessage.faceName() : '';
      }
    } catch (e) {}
    var scene = '';
    try {
      if (typeof SceneManager !== 'undefined' && SceneManager._scene && SceneManager._scene.constructor) {
        scene = SceneManager._scene.constructor.name;
      }
    } catch (e) {}
    return {
      scene: scene, video: video, msg: msg, face: face,
      sw: (typeof Graphics !== 'undefined' ? Graphics.width : 0),
      sh: (typeof Graphics !== 'undefined' ? Graphics.height : 0),
      pics: pics
    };
  } catch (e) {
    return { scene: '', video: false, msg: false, face: '', sw: 0, sh: 0, pics: [] };
  }
})())");
}

bool RpgMakerCdpAdapter::connectCdp(QString *error)
{
    if (m_cdp == nullptr) {
        m_cdp = std::make_unique<CdpWebSocketClient>();
    }
    QString url = m_profile.rpgmaker.value(QStringLiteral("wsUrl")).toString();
    if (url.isEmpty()) {
        const int port = m_profile.rpgmaker.value(QStringLiteral("cdpPort")).toInt(0);
        if (!CdpWebSocketClient::discoverWebSocketUrl(static_cast<quint16>(port), &url, error)) {
            return false;
        }
    }
    return m_cdp->connectToUrl(url, error);
}

bool RpgMakerCdpAdapter::attach(const GameProfile &profile, QString *error)
{
    if (!engineMatches(profile.engine)) {
        if (error != nullptr) {
            *error = QStringLiteral("适配器与档案引擎不匹配：期望 rpgmaker-mv/mz，档案为 %1")
                         .arg(QString::fromStdString(profile.engine));
        }
        return false;
    }
    if (!supports(profile)) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "缺少 CDP 端点：请在 profile.rpgmaker 提供 cdpPort 或 wsUrl；"
                "MV/MZ 需以 `--remote-debugging-port=<port>` 启动，否则改用桥接（profile.bridge）。");
        }
        return false;
    }

    m_profile = profile;
    m_expressions.clear();
    const QJsonObject expressions = profile.rpgmaker.value(QStringLiteral("expressions")).toObject();
    for (auto it = expressions.constBegin(); it != expressions.constEnd(); ++it) {
        m_expressions[it.key().toStdString()] = it.value().toString();
    }
    m_probeExpression = profile.rpgmaker.value(QStringLiteral("specialScene"))
                            .toObject()
                            .value(QStringLiteral("expression"))
                            .toString();
    if (m_probeExpression.isEmpty()) {
        m_probeExpression = builtinProbeExpression();
    }
    m_sceneDetector = RpgMakerSpecialSceneDetector(configFromJson(profile.rpgmaker));
    m_failures = 0;
    m_invalidated = false;

    if (!connectCdp(error)) {
        if (error != nullptr) {
            *error += QLatin1Char(' ')
                + QStringLiteral("提示：确认游戏以 `--remote-debugging-port=<port>` 启动；"
                                 "未开调试端口时请改用 profile.bridge 桥接。");
        }
        return false;
    }
    m_attached = true;
    return true;
}

void RpgMakerCdpAdapter::detach()
{
    m_attached = false;
    if (m_cdp != nullptr) {
        m_cdp->close();
    }
    m_sceneDetector.reset();
}

bool RpgMakerCdpAdapter::attached() const
{
    return m_attached && m_cdp != nullptr && m_cdp->connected();
}

void RpgMakerCdpAdapter::applyField(const std::string &name, const QJsonValue &value,
                                    core::GameSample *out)
{
    if (name == "hp") {
        out->hp = value.toDouble();
    } else if (name == "hpMax") {
        out->hpMax = value.toDouble();
    } else if (name == "gold") {
        out->gold = static_cast<long long>(value.toDouble());
    } else if (name == "level") {
        out->level = value.toInt();
    } else if (name == "posX") {
        out->posX = value.toDouble();
    } else if (name == "posY") {
        out->posY = value.toDouble();
    } else if (name == "mapName") {
        out->mapName = value.toString().toStdString();
    }
}

void RpgMakerCdpAdapter::markFailure(const QString &error)
{
    ++m_failures;
    if (m_failures >= kGameInvalidateAfterFailures) {
        m_invalidated = true;
    }
}

bool RpgMakerCdpAdapter::reconnect(QString *error)
{
    if (!attach(m_profile, error)) {
        return false;
    }
    return true;
}

bool RpgMakerCdpAdapter::read(core::GameSample *out, QString *error)
{
    if (out == nullptr) {
        return false;
    }
    out->available = false;
    if (!m_attached || m_cdp == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP 适配器尚未 attach");
        }
        return false;
    }
    if (m_invalidated) {
        if (error != nullptr) {
            *error = QStringLiteral("档案已失效（连续求值失败），请重连后重试");
        }
        return false;
    }

    int okCount = 0;
    QString lastError;
    for (const auto &entry : m_expressions) {
        QJsonValue value;
        QString err;
        if (m_cdp->evaluate(entry.second, &value, &err)) {
            applyField(entry.first, value, out);
            ++okCount;
        } else {
            lastError = err;
        }
    }

    if (!m_probeExpression.isEmpty()) {
        QJsonValue value;
        QString err;
        if (m_cdp->evaluate(m_probeExpression, &value, &err)) {
            out->specialScene = static_cast<int>(m_sceneDetector.update(value.toObject()));
        } else {
            lastError = err;
        }
    }

    if (okCount == 0) {
        out->available = false;
        markFailure(lastError);
        if (error != nullptr) {
            *error = lastError.isEmpty() ? QStringLiteral("CDP 未返回任何可用字段") : lastError;
        }
        return false;
    }

    out->available = true;
    m_failures = 0;
    return true;
}

} // namespace whalepet::gamestate
