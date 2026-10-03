#include "gamestate/CdpWebSocketClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

namespace whalepet::gamestate {

struct CdpWebSocketClient::Impl {
    QWebSocket socket;
    QEventLoop *loop = nullptr;
    bool connected = false;
    int nextId = 1;
    int pendingId = -1;
    bool hasResponse = false;
    QJsonValue result;
    QString errorText;
    QString lastError;
};

CdpWebSocketClient::CdpWebSocketClient(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    QObject::connect(&m_impl->socket, &QWebSocket::connected, &m_impl->socket, [this]() {
        m_impl->connected = true;
        if (m_impl->loop != nullptr) {
            m_impl->loop->quit();
        }
    });
    QObject::connect(&m_impl->socket, &QWebSocket::disconnected, &m_impl->socket, [this]() {
        m_impl->connected = false;
        if (m_impl->loop != nullptr) {
            m_impl->loop->quit();
        }
    });
    QObject::connect(&m_impl->socket, &QWebSocket::errorOccurred, &m_impl->socket,
                     [this](QAbstractSocket::SocketError) {
                         m_impl->lastError = m_impl->socket.errorString();
                         if (m_impl->loop != nullptr) {
                             m_impl->loop->quit();
                         }
                     });
    QObject::connect(&m_impl->socket, &QWebSocket::textMessageReceived, &m_impl->socket,
                     [this](const QString &message) {
                         const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
                         if (obj.value(QStringLiteral("id")).toInt(-1) != m_impl->pendingId) {
                             return; // 事件通知（非本请求）忽略
                         }
                         m_impl->hasResponse = true;
                         m_impl->errorText.clear();
                         m_impl->result = QJsonValue();
                         if (obj.contains(QStringLiteral("error"))) {
                             m_impl->errorText =
                                 obj.value(QStringLiteral("error"))
                                     .toObject()
                                     .value(QStringLiteral("message"))
                                     .toString();
                         } else {
                             const QJsonObject resultObj =
                                 obj.value(QStringLiteral("result")).toObject();
                             if (resultObj.contains(QStringLiteral("exceptionDetails"))) {
                                 m_impl->errorText = QStringLiteral("求值异常（exceptionDetails）");
                             } else {
                                 m_impl->result =
                                     resultObj.value(QStringLiteral("result")).toObject().value(
                                         QStringLiteral("value"));
                             }
                         }
                         if (m_impl->loop != nullptr) {
                             m_impl->loop->quit();
                         }
                     });
}

CdpWebSocketClient::~CdpWebSocketClient() = default;

bool CdpWebSocketClient::discoverWebSocketUrl(quint16 port, QString *url, QString *error,
                                              int timeoutMs)
{
    QNetworkAccessManager manager;
    const QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:%1/json").arg(port)));
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (error != nullptr) {
            *error = QStringLiteral("CDP /json 发现超时（端口 %1 未响应）").arg(port);
        }
        return false;
    }
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    reply->deleteLater();
    if (netError != QNetworkReply::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP /json 发现失败（端口 %1 未监听调试端口？）").arg(port);
        }
        return false;
    }

    const QJsonArray targets = QJsonDocument::fromJson(payload).array();
    QString fallback;
    for (const QJsonValue &value : targets) {
        const QJsonObject target = value.toObject();
        const QString wsUrl = target.value(QStringLiteral("webSocketDebuggerUrl")).toString();
        if (wsUrl.isEmpty()) {
            continue;
        }
        if (target.value(QStringLiteral("type")).toString() == QStringLiteral("page")) {
            if (url != nullptr) {
                *url = wsUrl;
            }
            return true;
        }
        if (fallback.isEmpty()) {
            fallback = wsUrl;
        }
    }
    if (!fallback.isEmpty()) {
        if (url != nullptr) {
            *url = fallback;
        }
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("CDP /json 未发现可用的页面目标");
    }
    return false;
}

bool CdpWebSocketClient::connectToUrl(const QString &url, QString *error, int timeoutMs)
{
    close();
    m_impl->pendingId = -1;
    m_impl->hasResponse = false;
    m_impl->errorText.clear();
    m_impl->lastError.clear();

    QEventLoop loop;
    m_impl->loop = &loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    m_impl->socket.open(QUrl(url));
    timer.start(timeoutMs);
    loop.exec();
    m_impl->loop = nullptr;

    if (!m_impl->socket.isValid() || m_impl->socket.state() != QAbstractSocket::ConnectedState) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP WebSocket 连接失败：%1")
                         .arg(m_impl->lastError.isEmpty() ? QStringLiteral("超时或地址无效")
                                                          : m_impl->lastError);
        }
        m_impl->socket.close();
        return false;
    }
    m_impl->connected = true;
    return true;
}

bool CdpWebSocketClient::connected() const
{
    return m_impl->connected && m_impl->socket.state() == QAbstractSocket::ConnectedState;
}

void CdpWebSocketClient::close()
{
    m_impl->connected = false;
    m_impl->socket.close();
}

QString CdpWebSocketClient::lastError() const
{
    return m_impl->lastError;
}

bool CdpWebSocketClient::evaluate(const QString &expression, QJsonValue *value, QString *error,
                                  int timeoutMs)
{
    if (!connected()) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP 未连接");
        }
        return false;
    }

    const int id = m_impl->nextId++;
    QJsonObject params;
    params.insert(QStringLiteral("expression"), expression);
    params.insert(QStringLiteral("returnByValue"), true);
    params.insert(QStringLiteral("awaitPromise"), false);
    params.insert(QStringLiteral("silent"), true);
    QJsonObject message;
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("method"), QStringLiteral("Runtime.evaluate"));
    message.insert(QStringLiteral("params"), params);

    m_impl->pendingId = id;
    m_impl->hasResponse = false;
    m_impl->result = QJsonValue();
    m_impl->errorText.clear();

    QEventLoop loop;
    m_impl->loop = &loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    m_impl->socket.sendTextMessage(
        QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    timer.start(timeoutMs);
    loop.exec();
    m_impl->loop = nullptr;
    m_impl->pendingId = -1;

    if (!m_impl->hasResponse) {
        m_impl->lastError = QStringLiteral("CDP 求值超时（引擎无响应）");
        if (error != nullptr) {
            *error = m_impl->lastError;
        }
        return false;
    }
    if (!m_impl->errorText.isEmpty()) {
        m_impl->lastError = m_impl->errorText;
        if (error != nullptr) {
            *error = m_impl->lastError;
        }
        return false;
    }
    if (value != nullptr) {
        *value = m_impl->result;
    }
    return true;
}

} // namespace whalepet::gamestate
