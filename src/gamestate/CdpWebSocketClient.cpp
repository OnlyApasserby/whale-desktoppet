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
                         // 【边界】超大消息：立刻中止，绝不在内存里累积引擎发来的巨量数据
                         if (message.size() > kMaxMessageBytes) {
                             m_impl->lastError =
                                 QStringLiteral("CDP 消息超过上限（%1 > %2 字节）")
                                     .arg(message.size())
                                     .arg(kMaxMessageBytes);
                             m_impl->errorText = m_impl->lastError;
                             m_impl->hasResponse = true;
                             m_impl->result = QJsonValue();
                             m_impl->socket.abort();
                             if (m_impl->loop != nullptr) {
                                 m_impl->loop->quit();
                             }
                             return;
                         }
                         const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
                         if (obj.value(QStringLiteral("id")).toInt(-1) != m_impl->pendingId) {
                             return; // 事件通知 / 迟到或错误的响应 id（非本请求）一律忽略
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

bool CdpWebSocketClient::isTrustedDebuggerUrl(const QUrl &url, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!url.isValid() || url.isRelative()) {
        return fail(QStringLiteral("调试端点不是合法 URL：%1").arg(url.toString()));
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("ws") && scheme != QStringLiteral("wss")) {
        return fail(QStringLiteral("调试端点协议必须为 ws/wss：%1").arg(url.toString()));
    }
    if (!url.userInfo().isEmpty()) {
        // userinfo 是典型的地址伪装手法（ws://127.0.0.1@evil.example/）
        return fail(QStringLiteral("调试端点不得携带 userinfo：%1").arg(url.toString()));
    }
    const QString host = url.host().toLower();
    const bool loopback = host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost")
        || host == QStringLiteral("::1") || host == QStringLiteral("[::1]");
    if (!loopback) {
        // 只读求值通道**绝不**连到回环之外（否则等于主动把游戏数据发出去）
        return fail(QStringLiteral("调试端点主机必须是本机回环：%1").arg(url.toString()));
    }
    const int port = url.port();
    if (port <= 0 || port > 65535) {
        return fail(QStringLiteral("调试端点端口非法：%1").arg(url.toString()));
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool CdpWebSocketClient::discoverWebSocketUrl(quint16 port, QString *url, QString *error,
                                              int timeoutMs)
{
    QNetworkAccessManager manager;
    const QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:%1/json").arg(port)));
    QNetworkReply *reply = manager.get(request);

    // 【边界】边下边判：累积超过上限立刻中止（不 readAll() 一个大小未知的响应）
    QByteArray payload;
    bool overflow = false;
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, &payload, &overflow]() {
        if (overflow) {
            return;
        }
        payload += reply->readAll();
        if (payload.size() > kMaxDiscoveryBytes) {
            overflow = true;
            reply->abort();
        }
    });

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
    if (!overflow) {
        payload += reply->readAll();
    }
    const QNetworkReply::NetworkError netError = reply->error();
    reply->deleteLater();
    if (overflow) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP /json 响应超过上限（>%1 字节，端口 %2）")
                         .arg(kMaxDiscoveryBytes)
                         .arg(port);
        }
        return false;
    }
    if (netError != QNetworkReply::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP /json 发现失败（端口 %1 未监听调试端口？）").arg(port);
        }
        return false;
    }

    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("CDP /json 响应不是合法 JSON 数组：%1")
                         .arg(parseError.errorString());
        }
        return false;
    }
    const QJsonArray targets = doc.array();
    // 依次尝试候选目标：只有通过**白名单校验**的地址才可被采用
    QStringList rejected;
    QString fallback;
    for (const QJsonValue &value : targets) {
        const QJsonObject target = value.toObject();
        const QString wsUrl = target.value(QStringLiteral("webSocketDebuggerUrl")).toString();
        if (wsUrl.isEmpty()) {
            continue;
        }
        QString reason;
        if (!isTrustedDebuggerUrl(QUrl(wsUrl), &reason)) {
            rejected.append(wsUrl);
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
        *error = rejected.isEmpty()
            ? QStringLiteral("CDP /json 未发现可用的页面目标")
            : QStringLiteral("CDP /json 的调试端点均不可信（已拒绝 %1 个）：%2")
                  .arg(rejected.size())
                  .arg(rejected.join(QStringLiteral(", ")));
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

    // 【白名单】非可信端点（远程主机 / 非 ws / userinfo / 非法端口）连都不连
    QString reason;
    if (!isTrustedDebuggerUrl(QUrl(url), &reason)) {
        m_impl->lastError = reason;
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }

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
        // 失败路径必须复位：不得留下「看起来已连接」的悬挂状态
        m_impl->socket.close();
        m_impl->connected = false;
        m_impl->pendingId = -1;
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
    m_impl->pendingId = -1;
    m_impl->hasResponse = false;
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
