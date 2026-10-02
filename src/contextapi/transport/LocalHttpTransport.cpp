#include "contextapi/transport/LocalHttpTransport.h"

#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace whalepet::contextapi {

namespace {

const char *const kHeaderSeparator = "\r\n\r\n";
const char *const kTokenHeader = "x-whalepet-token:";
const char *const kRpcPath = "/rpc";

QByteArray statusReason(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    default:
        return "Error";
    }
}

} // namespace

LocalHttpTransport::LocalHttpTransport(JsonRpcDispatcher *dispatcher, QObject *parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
{
}

LocalHttpTransport::~LocalHttpTransport()
{
    stop();
}

bool LocalHttpTransport::start(quint16 port)
{
    if (m_server != nullptr) {
        m_error = QStringLiteral("通道已启动");
        return false;
    }

    m_server = new QTcpServer(this);
    // 只监听回环：绝不监听 0.0.0.0（docs/CONTEXT-API.md §5）
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        m_error = QStringLiteral("监听 127.0.0.1:%1 失败：%2")
                      .arg(port)
                      .arg(m_server->errorString());
        qWarning() << "[LocalHttpTransport]" << m_error;
        delete m_server;
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QTcpServer::newConnection, this, &LocalHttpTransport::onNewConnection);
    m_error.clear();
    qInfo() << "[LocalHttpTransport] 本地 Context API 已监听 127.0.0.1:" << m_server->serverPort();
    return true;
}

void LocalHttpTransport::stop()
{
    for (QTcpSocket *socket : m_buffers.keys()) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_buffers.clear();

    if (m_server != nullptr) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

bool LocalHttpTransport::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

quint16 LocalHttpTransport::port() const
{
    return isListening() ? m_server->serverPort() : 0;
}

void LocalHttpTransport::onNewConnection()
{
    if (m_server == nullptr) {
        return;
    }
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void LocalHttpTransport::onReadyRead(QTcpSocket *socket)
{
    auto it = m_buffers.find(socket);
    if (it == m_buffers.end()) {
        return;
    }
    it.value().append(socket->readAll());

    const int headerEnd = it.value().indexOf(kHeaderSeparator);
    if (headerEnd < 0) {
        return; // 头部未收全
    }

    int contentLength = 0;
    const QByteArray head = it.value().left(headerEnd);
    const QList<QByteArray> lines = head.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            bool ok = false;
            const int value = trimmed.mid(static_cast<int>(qstrlen("content-length:"))).trimmed().toInt(&ok);
            if (ok) {
                contentLength = value;
            }
        }
    }

    const int bodyStart = headerEnd + static_cast<int>(qstrlen(kHeaderSeparator));
    if (it.value().size() - bodyStart < contentLength) {
        return; // 正文未收全
    }
    const QByteArray body = it.value().mid(bodyStart, contentLength);
    it.value().clear();

    handleRequestLine(socket, head, body);
}

void LocalHttpTransport::handleRequestLine(QTcpSocket *socket, const QByteArray &head,
                                           const QByteArray &body)
{
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        writeJsonRpcError(socket, 400, statusReason(400), plugin::kRpcErrorInvalidRequest,
                          QStringLiteral("空请求"));
        return;
    }

    const QByteArray requestLine = lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 3) {
        writeJsonRpcError(socket, 400, statusReason(400), plugin::kRpcErrorInvalidRequest,
                          QStringLiteral("请求行不合法"));
        return;
    }
    const QByteArray method = parts.at(0);
    const QByteArray path = parts.at(1);

    if (method != "POST") {
        writeJsonRpcError(socket, 405, statusReason(405), plugin::kRpcErrorInvalidRequest,
                          QStringLiteral("仅支持 POST"));
        return;
    }
    if (path != kRpcPath) {
        writeJsonRpcError(socket, 404, statusReason(404), plugin::kRpcErrorMethodNotFound,
                          QStringLiteral("仅支持 POST /rpc"));
        return;
    }

    if (!m_token.isEmpty()) {
        QByteArray provided;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray trimmed = lines.at(i).trimmed();
            if (trimmed.toLower().startsWith(kTokenHeader)) {
                provided = trimmed.mid(static_cast<int>(qstrlen(kTokenHeader))).trimmed();
                break;
            }
        }
        if (provided != m_token.toUtf8()) {
            writeJsonRpcError(socket, 401, statusReason(401), plugin::kRpcErrorUnauthorized,
                              QStringLiteral("token 缺失或不匹配"));
            return;
        }
    }

    if (m_dispatcher == nullptr) {
        writeJsonRpcError(socket, 500, "Internal Server Error", plugin::kRpcErrorInternal,
                          QStringLiteral("分发核心不可用"));
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        writeJsonRpcError(socket, 400, statusReason(400), plugin::kRpcErrorParseError,
                          QStringLiteral("请求体不是合法 JSON 对象"));
        return;
    }

    const QJsonObject response = m_dispatcher->handleSync(doc.object());
    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    writeResponse(socket, 200, statusReason(200), "application/json", payload);
}

void LocalHttpTransport::writeResponse(QTcpSocket *socket, int status, const QByteArray &reason,
                                      const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(status));
    response.append(' ');
    response.append(reason);
    response.append("\r\nContent-Type: ");
    response.append(contentType);
    response.append("\r\nContent-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\nConnection: close\r\n\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void LocalHttpTransport::writeJsonRpcError(QTcpSocket *socket, int httpStatus,
                                           const QByteArray &reason, int code,
                                           const QString &message)
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), QJsonValue(QJsonValue::Null));
    response.insert(QStringLiteral("error"), plugin::makeRpcError(code, message));
    writeResponse(socket, httpStatus, reason, "application/json",
                  QJsonDocument(response).toJson(QJsonDocument::Compact));
}

} // namespace whalepet::contextapi
