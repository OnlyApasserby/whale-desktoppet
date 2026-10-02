#include "contextapi/transport/LocalPipeTransport.h"

#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QList>

namespace whalepet::contextapi {

LocalPipeTransport::LocalPipeTransport(JsonRpcDispatcher *dispatcher, QObject *parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
{
}

LocalPipeTransport::~LocalPipeTransport()
{
    stop();
}

bool LocalPipeTransport::start()
{
    if (isListening()) {
        m_error = QStringLiteral("本地命名管道通道已在监听");
        return false;
    }

    if (m_server == nullptr) {
        m_server = new QLocalServer(this);
        connect(m_server, &QLocalServer::newConnection,
                this, &LocalPipeTransport::onNewConnection);
    }

    // 清理上次异常退出残留的管道名（否则 listen 会因名字被占用而失败）。
    // 若确有实例正在监听，removeServer 不会误删：Qt 会先尝试连接，连接成功即视为「在用」。
    QLocalServer::removeServer(m_serverName);

    if (!m_server->listen(m_serverName)) {
        m_error = QStringLiteral("监听命名管道 %1 失败：%2")
                      .arg(m_serverName, m_server->errorString());
        qWarning() << "[LocalPipeTransport]" << m_error;
        return false;
    }

    m_error.clear();
    qInfo() << "[LocalPipeTransport] 本地 Context API 已监听命名管道" << m_serverName;
    return true;
}

void LocalPipeTransport::stop()
{
    const QList<QLocalSocket *> sockets = m_clients.keys();
    for (QLocalSocket *socket : sockets) {
        removeClient(socket);
    }
    m_clients.clear();

    if (m_server != nullptr) {
        if (m_server->isListening()) {
            m_server->close();
        }
        QLocalServer::removeServer(m_serverName);
    }
}

bool LocalPipeTransport::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

void LocalPipeTransport::onNewConnection()
{
    if (m_server == nullptr) {
        return;
    }
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        // 每条连接一个 StdioTransport：同一分帧 / 同一 MCP 映射 / 同一 token 门控
        auto *transport = new StdioTransport(m_dispatcher, this);
        transport->setToken(m_token);
        m_clients.insert(socket, transport);
        transport->bind(socket, socket);

        connect(socket, &QLocalSocket::disconnected,
                this, [this, socket] { removeClient(socket); });
        connect(socket, &QLocalSocket::errorOccurred,
                this, [this, socket](QLocalSocket::LocalSocketError) { removeClient(socket); });

        qInfo() << "[LocalPipeTransport] 桥接进程已连接，当前连接数" << m_clients.size();
    }
}

void LocalPipeTransport::removeClient(QLocalSocket *socket)
{
    if (socket == nullptr) {
        return;
    }
    auto it = m_clients.find(socket);
    if (it == m_clients.end()) {
        return; // 已经清理过（disconnected 与 errorOccurred 可能先后到达）
    }

    StdioTransport *transport = it.value();
    m_clients.erase(it);
    if (transport != nullptr) {
        transport->deleteLater();
    }

    QObject::disconnect(socket, nullptr, this, nullptr); // 避免重复回调
    socket->disconnectFromServer();
    socket->deleteLater();

    qInfo() << "[LocalPipeTransport] 桥接进程已断开，当前连接数" << m_clients.size();
}

} // namespace whalepet::contextapi
