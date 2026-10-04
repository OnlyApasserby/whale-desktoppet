#include "contextapi/transport/LocalHttpTransport.h"

#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace whalepet::contextapi {

namespace {

const char *const kHeaderSeparator = "\r\n\r\n";
const char *const kTokenHeader = "x-whalepet-token";
const char *const kContentTypeHeader = "content-type";
const char *const kContentLengthHeader = "content-length";
const char *const kOriginHeader = "origin";
const char *const kTransferEncodingHeader = "transfer-encoding";
const char *const kRpcPath = "/rpc";
const char *const kJsonContentType = "application/json";

QByteArray statusReason(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 415:
        return "Unsupported Media Type";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    default:
        return "Error";
    }
}

// HTTP 状态 → JSON-RPC 错误码：复用 plugin/Capability.h 的既有常量（禁止另写字面量）
int errorCodeForStatus(int status)
{
    switch (status) {
    case 401: // token 缺失或不匹配
    case 403: // 不可信来源（Origin 校验失败）
        return plugin::kRpcErrorUnauthorized;
    case 500:
        return plugin::kRpcErrorInternal;
    default:
        return plugin::kRpcErrorInvalidRequest;
    }
}

// 定长比较（不因首个不同字节就短路），避免计时侧信道泄露令牌
bool secureEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff = static_cast<unsigned char>(diff | static_cast<unsigned char>(a.at(i) ^ b.at(i)));
    }
    return diff == 0;
}

// 媒体类型归一：取 ';' 之前的部分并去空白、小写（`Application/JSON; charset=utf-8` 亦通过）
QString mediaTypeOf(const QString &contentType)
{
    const QString value = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    return value;
}

// 来源可信性：带 Origin 时必须是**同源同端口**的回环地址。
//   * 不带 Origin（curl / 本地工具 / 我们的 MCP 桥接）⇒ 放行（仍需 token）；
//   * `Origin: null`（file://、sandbox iframe、隐私策略降级）⇒ 不可信；
//   * userinfo、任何非回环主机 ⇒ 不可信。
bool isTrustedOrigin(const QString &origin, quint16 port)
{
    if (origin.isEmpty()) {
        return true;
    }
    const QUrl url(origin);
    if (!url.isValid() || origin.contains(QLatin1String("://")) == false) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return false;
    }
    if (!url.userInfo().isEmpty()) {
        return false; // userinfo 视为伪装
    }
    const QString host = url.host().toLower();
    const bool loopback = host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost")
        || host == QStringLiteral("::1") || host == QStringLiteral("[::1]");
    if (!loopback) {
        return false;
    }
    return url.port() == port;
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

    // 【安全】fail closed：没有令牌就根本不监听。
    // 浏览器可对 127.0.0.1:<port> 发起跨站请求（「回环绑定」不是授权机制），
    // 空令牌会让任意网页触发本机 JSON-RPC 工具调用（SECURITY-REVIEW.md #1）。
    if (m_token.trimmed().isEmpty()) {
        m_error = QStringLiteral("未配置访问令牌：本地 HTTP 通道拒绝启动"
                                 "（浏览器可向本机端口发起跨站请求，空令牌等于无认证）");
        qWarning() << "[LocalHttpTransport]" << m_error;
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

    if (m_sweeper == nullptr) {
        m_sweeper = new QTimer(this);
        m_sweeper->setInterval(kSweepIntervalMs);
        connect(m_sweeper, &QTimer::timeout, this, &LocalHttpTransport::sweepConnections);
    }
    // 超时巡检被显式关闭（<= 0）时不启动
    m_sweeper->stop();
    if (m_requestTimeoutMs > 0) {
        m_sweeper->start();
    }

    m_error.clear();
    qInfo() << "[LocalHttpTransport] 本地 Context API 已监听 127.0.0.1:" << m_server->serverPort();
    return true;
}

void LocalHttpTransport::stop()
{
    if (m_sweeper != nullptr) {
        m_sweeper->stop();
    }
    // 断开本对象到各 socket 的连接（避免 disconnected lambda 二次处理），再统一回收
    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        it.key()->disconnect(this);
        it.key()->disconnectFromHost();
        it.key()->deleteLater();
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
        if (m_buffers.size() >= kMaxConnections) {
            // 并发连接上限：直接丢弃，不为超限连接分配任何缓冲
            qWarning() << "[LocalHttpTransport] 并发连接数达上限（" << kMaxConnections
                       << "），拒绝新连接";
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ConnState state;
        state.since.start();
        m_buffers.insert(socket, state);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void LocalHttpTransport::sweepConnections()
{
    if (m_requestTimeoutMs <= 0) {
        return;
    }
    // 慢速客户端（逐字节发未收全的请求头 / 永不结束的正文）不得长期占用资源
    QList<QTcpSocket *> expired;
    for (auto it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it) {
        if (it.value().since.elapsed() > m_requestTimeoutMs) {
            expired.append(it.key());
        }
    }
    for (QTcpSocket *socket : expired) {
        qWarning() << "[LocalHttpTransport] 请求超时（>" << m_requestTimeoutMs
                   << "ms），关闭连接";
        finishConnection(socket, true);
    }
}

void LocalHttpTransport::finishConnection(QTcpSocket *socket, bool abort)
{
    if (socket == nullptr) {
        return;
    }
    // 先摘掉本对象的连接：disconnected lambda 不再二次处理
    socket->disconnect(this);
    m_buffers.remove(socket);
    if (abort) {
        socket->abort();
    } else {
        socket->disconnectFromHost();
    }
    socket->deleteLater();
}

bool LocalHttpTransport::parseHead(const QByteArray &head, RequestHead *out, QString *error) const
{
    const QList<QByteArray> lines = head.split('\n');
    for (int i = 1; i < lines.size(); ++i) { // lines[0] 是请求行
        const QByteArray trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const int colon = trimmed.indexOf(':');
        if (colon <= 0) {
            if (error != nullptr) {
                *error = QStringLiteral("请求头行缺少冒号：%1")
                             .arg(QString::fromLatin1(trimmed.left(64)));
            }
            return false;
        }
        const QByteArray name = trimmed.left(colon).trimmed().toLower();
        const QString value = QString::fromLatin1(trimmed.mid(colon + 1).trimmed());
        if (name == QLatin1String(kContentLengthHeader)) {
            bool ok = false;
            const qlonglong parsed = value.toLongLong(&ok);
            if (!ok || parsed < 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("Content-Length 非法：%1").arg(value);
                }
                return false;
            }
            if (out->contentLength >= 0 && out->contentLength != parsed) {
                // 请求走私（request smuggling）的经典入口：两个不一致的长度
                if (error != nullptr) {
                    *error = QStringLiteral("冲突的 Content-Length");
                }
                return false;
            }
            out->contentLength = static_cast<int>(parsed);
        } else if (name == QLatin1String(kContentTypeHeader)) {
            out->contentType = value;
        } else if (name == QLatin1String(kTokenHeader)) {
            out->token = value;
        } else if (name == QLatin1String(kOriginHeader)) {
            out->origin = value;
        } else if (name == QLatin1String(kTransferEncodingHeader)) {
            out->hasTransferEncoding = true;
        }
    }
    return true;
}

void LocalHttpTransport::onReadyRead(QTcpSocket *socket)
{
    auto it = m_buffers.find(socket);
    if (it == m_buffers.end()) {
        return;
    }
    it.value().buffer.append(socket->readAll());

    const int headerEnd = it.value().buffer.indexOf(kHeaderSeparator);
    if (headerEnd < 0) {
        // 头部未收全：超过上限即视为慢速/恶意客户端，关连接（不无限累积缓冲）
        if (it.value().buffer.size() > kMaxHeaderBytes) {
            reject(socket, 431, QStringLiteral("请求头超过上限（%1 字节）").arg(kMaxHeaderBytes));
        }
        return;
    }
    if (headerEnd > kMaxHeaderBytes) {
        reject(socket, 431, QStringLiteral("请求头超过上限（%1 字节）").arg(kMaxHeaderBytes));
        return;
    }

    const QByteArray head = it.value().buffer.left(headerEnd);
    RequestHead parsed;
    QString parseError;
    if (!parseHead(head, &parsed, &parseError)) {
        reject(socket, 400, parseError);
        return;
    }
    if (parsed.hasTransferEncoding) {
        // 本通道刻意不支持 chunked（docs/CONTEXT-API.md §4）
        reject(socket, 501, QStringLiteral("不支持 Transfer-Encoding（仅 Content-Length 定长正文）"));
        return;
    }
    if (parsed.contentLength < 0) {
        reject(socket, 411, QStringLiteral("缺少 Content-Length"));
        return;
    }
    if (parsed.contentLength > kMaxBodyBytes) {
        reject(socket, 413,
               QStringLiteral("请求正文超过上限（%1 > %2 字节）").arg(parsed.contentLength)
                   .arg(kMaxBodyBytes));
        return;
    }

    const int bodyStart = headerEnd + static_cast<int>(qstrlen(kHeaderSeparator));
    if (it.value().buffer.size() - bodyStart < parsed.contentLength) {
        // 正文未收全：只允许缓冲「头 + 声明长度」，超出即说明客户端在撒谎或发超长正文
        if (it.value().buffer.size() > bodyStart + parsed.contentLength) {
            reject(socket, 400, QStringLiteral("正文长度与 Content-Length 不符"));
        }
        return; // 正文未收全
    }

    const QByteArray body = it.value().buffer.mid(bodyStart, parsed.contentLength);
    it.value().buffer.clear();
    // 令牌在 head 解析阶段已取出，这里并入 body 之后由 handleRequestLine 统一校验
    handleRequestLine(socket, head, body, parsed);
}

void LocalHttpTransport::handleRequestLine(QTcpSocket *socket, const QByteArray &head,
                                           const QByteArray &body, const RequestHead &parsed)
{
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        reject(socket, 400, QStringLiteral("空请求"));
        return;
    }

    const QByteArray requestLine = lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 3) {
        reject(socket, 400, QStringLiteral("请求行不合法"));
        return;
    }
    const QByteArray method = parts.at(0);
    const QByteArray path = parts.at(1);

    if (method != "POST") {
        reject(socket, 405, QStringLiteral("仅支持 POST"));
        return;
    }
    if (path != kRpcPath) {
        reject(socket, 404, QStringLiteral("仅支持 POST /rpc"));
        return;
    }

    // ---- 安全门 1：Content-Type 必须是 application/json ----
    // text/plain / form-urlencoded 属 CORS「简单请求」，网页可无预检直发本机端口；
    // application/json 会触发预检，而本通道从不回 Access-Control-Allow-*。
    const QString mediaType = mediaTypeOf(parsed.contentType);
    if (mediaType != QLatin1String(kJsonContentType)) {
        reject(socket, 415,
               QStringLiteral("Content-Type 必须为 application/json（实际：%1）")
                   .arg(parsed.contentType.isEmpty() ? QStringLiteral("<缺失>") : parsed.contentType));
        return;
    }

    // ---- 安全门 2：Origin 必须同源同端口（防跨站；含 DNS rebinding 的 userinfo 伪装）----
    if (!isTrustedOrigin(parsed.origin, port())) {
        reject(socket, 403,
               QStringLiteral("不接受跨来源请求（Origin：%1）")
                   .arg(parsed.origin.isEmpty() ? QStringLiteral("<缺失>") : parsed.origin));
        return;
    }

    // ---- 安全门 3：token 必须匹配（空令牌 = 拒绝一切，绝不放行）----
    if (m_token.trimmed().isEmpty()
        || !secureEquals(parsed.token.toUtf8(), m_token.trimmed().toUtf8())) {
        reject(socket, 401, QStringLiteral("token 缺失或不匹配"));
        return;
    }

    if (m_dispatcher == nullptr) {
        reject(socket, 500, QStringLiteral("分发核心不可用"));
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        reject(socket, 400, QStringLiteral("请求体不是合法 JSON 对象"));
        return;
    }

    const QJsonObject response = m_dispatcher->handleSync(doc.object());
    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    writeResponse(socket, 200, statusReason(200), kJsonContentType, payload);
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
    // 本通道从不回 CORS 头：预检与跨站读取都必须失败（安全门 1/2 的组成部分）
    response.append("\r\nX-Content-Type-Options: nosniff");
    response.append("\r\nCache-Control: no-store");
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
    writeResponse(socket, httpStatus, reason, QByteArray(kJsonContentType),
                  QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void LocalHttpTransport::reject(QTcpSocket *socket, int status, const QString &message)
{
    writeJsonRpcError(socket, status, statusReason(status), errorCodeForStatus(status), message);
    finishConnection(socket, false); // 错误已下发给客户端，随后关闭该连接
}

} // namespace whalepet::contextapi
