#include "viewmodel/WeatherService.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace whalepet::viewmodel {

namespace {
const char *const kApiBase = "https://api.seniverse.com/v3/weather/now.json";
} // namespace

WeatherService::WeatherService(QObject *parent)
    : QObject(parent)
{
}

WeatherService::~WeatherService()
{
    abortInFlight();
}

void WeatherService::setConfig(const QString &key, const QString &location)
{
    const QString trimmedKey = key.trimmed();
    const QString trimmedLocation = location.trimmed();
    if (trimmedKey == m_key && trimmedLocation == m_location) {
        return; // 配置未变：保留既有缓存与退避状态
    }
    m_key = trimmedKey;
    m_location = trimmedLocation;
    // 配置变更即失效旧结果（换城市后旧天气不再代表当前地点）
    m_hasResult = false;
    m_kind = core::WeatherKind::Unknown;
    m_description.clear();
    m_placeName.clear();
    m_updatedAtMs = 0;
    m_blockedUntilMs = 0; // 新配置允许立即重试
    m_lastError.clear();
}

bool WeatherService::configured() const
{
    return !m_key.isEmpty() && !m_location.isEmpty();
}

bool WeatherService::stale() const
{
    if (!m_hasResult) {
        return true;
    }
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_updatedAtMs;
    return age > kCacheTtlMs;
}

qint64 WeatherService::backoffRemainingMs() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (m_blockedUntilMs > now) ? (m_blockedUntilMs - now) : 0;
}

void WeatherService::abortInFlight()
{
    if (m_reply != nullptr) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void WeatherService::refresh()
{
    if (!configured()) {
        return; // 未配置：完全不联网（隐私优先）
    }
    if (m_reply != nullptr) {
        return; // 已有在途请求：不排队、不重复发
    }
    if (backoffRemainingMs() > 0) {
        return; // 退避中
    }
    if (m_hasResult && !stale()) {
        return; // 结果仍新鲜
    }

    if (m_net == nullptr) {
        m_net = new QNetworkAccessManager(this);
    }

    QUrl url(QString::fromLatin1(kApiBase));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), m_key);
    query.addQueryItem(QStringLiteral("location"), m_location);
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("zh-Hans"));
    query.addQueryItem(QStringLiteral("unit"), QStringLiteral("c"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_net->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void WeatherService::onReplyFinished(QNetworkReply *reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return; // 已被 abortInFlight 接管（配置变更 / 析构）
    }
    m_reply = nullptr;
    reply->deleteLater();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        m_blockedUntilMs = now + kRetryBackoffMs; // 静默退避 60 分钟
        emit failed(m_lastError);
        return;
    }

    const QByteArray payload = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = QStringLiteral("响应不是合法 JSON");
        m_blockedUntilMs = now + kRetryBackoffMs;
        emit failed(m_lastError);
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject nowObject = root.value(QStringLiteral("now")).toObject();
    const QString weather = nowObject.value(QStringLiteral("weather")).toString();
    if (weather.isEmpty()) {
        // 彩云的错误响应形如 {"status":"...","status_code":"..."}：无 now.weather
        m_lastError = root.value(QStringLiteral("status")).toString();
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("响应缺少 now.weather");
        }
        m_blockedUntilMs = now + kRetryBackoffMs;
        emit failed(m_lastError);
        return;
    }

    m_kind = core::weatherKindFromCaiyun(weather.toStdString());
    m_description = weather;
    m_placeName = root.value(QStringLiteral("location"))
                      .toObject()
                      .value(QStringLiteral("name"))
                      .toString();
    m_hasResult = true;
    m_updatedAtMs = now;
    m_blockedUntilMs = 0;
    m_lastError.clear();
    emit updated(m_kind, m_description);
}

} // namespace whalepet::viewmodel
