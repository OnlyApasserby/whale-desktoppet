#pragma once

// 本地 HTTP 通道（docs/CONTEXT-API.md §4）：**仅绑定 127.0.0.1**，只接受 `POST /rpc`。
//
// 设计取舍（刻意的极简，不为调试通道引入额外依赖）：
//   * 用 `QTcpServer` 手写最小 HTTP/1.1（不用 `Qt6::HttpServer`，少一个 Qt 模块依赖）；
//   * 不支持 keep-alive / chunked / 压缩，每个请求一个短连接（`Connection: close`）；
//   * 同步分发（`JsonRpcDispatcher::handleSync`）：异步能力不适用于本通道（明确报错）。
//
// 访问控制：仅回环地址 + 可选 token（`X-WhalePet-Token` 头）。默认**不启动**
// （由 ContextApiService 依设置门控），因此正常运行不监听任何端口。

#include "contextapi/JsonRpcDispatcher.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace whalepet::contextapi {

class LocalHttpTransport : public QObject {
    Q_OBJECT
public:
    explicit LocalHttpTransport(JsonRpcDispatcher *dispatcher, QObject *parent = nullptr);
    ~LocalHttpTransport() override;

    // token 非空时要求 `X-WhalePet-Token` 头匹配
    void setToken(const QString &token) { m_token = token; }

    // 启动监听；port == 0 表示由系统分配。失败时 errorString() 给出原因
    bool start(quint16 port = 0);
    void stop();

    bool isListening() const;
    quint16 port() const; // 实际监听端口；未监听返回 0
    QString errorString() const { return m_error; }

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);

    void handleRequestLine(QTcpSocket *socket, const QByteArray &head, const QByteArray &body);
    void writeResponse(QTcpSocket *socket, int status, const QByteArray &reason,
                       const QByteArray &contentType, const QByteArray &body);
    void writeJsonRpcError(QTcpSocket *socket, int httpStatus, const QByteArray &reason, int code,
                           const QString &message);

    QTcpServer *m_server = nullptr;
    JsonRpcDispatcher *m_dispatcher = nullptr;
    QString m_token;
    QString m_error;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace whalepet::contextapi
