#pragma once

// 本地 HTTP 通道（docs/CONTEXT-API.md §4）：**仅绑定 127.0.0.1**，只接受 `POST /rpc`。
//
// 设计取舍（刻意的极简，不为调试通道引入额外依赖）：
//   * 用 `QTcpServer` 手写最小 HTTP/1.1（不用 `Qt6::HttpServer`，少一个 Qt 模块依赖）；
//   * 不支持 keep-alive / chunked / 压缩，每个请求一个短连接（`Connection: close`）；
//   * 同步分发（`JsonRpcDispatcher::handleSync`）：异步能力不适用于本通道（明确报错）。
//
// ── 访问控制（三层，纵深防御；修复 SECURITY-REVIEW.md #1）────────────────────────
//  1. **强制非空 token**：`start()` 在 token 为空时**直接失败**（fail closed）。
//     绝不依赖「回环绑定」当作授权——浏览器可对 `http://127.0.0.1:<port>` 发跨站请求，
//     空 token 等于对全世界的网页开放一次 JSON-RPC 工具调用。
//  2. **Content-Type 必须为 `application/json`**：这是 CORS「简单请求」的判定条件，
//     `text/plain` / `form-urlencoded` 可无预检直发；`application/json` 会触发预检，
//     而本通道**从不**回 `Access-Control-Allow-*`，浏览器因此读不到响应。
//  3. **Origin 必须同源**：带 `Origin` 且非 `http://127.0.0.1:<本端口>` / `localhost:<本端口>`
//     的请求一律 403（`Origin: null`、`file://`、userinfo、非回环主机一律不可信）。
//  4. **token 定长比较**：避免计时侧信道泄露令牌。
//
// ── 资源上限（对抗慢速 / 超大请求；SECURITY-REVIEW.md §极端边界测试建议 2）──────
//  单连接缓冲、并发连接数、单请求等待时长均有上限，超限即关闭连接，
//  避免慢速客户端长期占用描述符与内存。
//
// 默认**不启动**（由 ContextApiService 依设置门控），因此正常运行不监听任何端口。

#include "contextapi/JsonRpcDispatcher.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace whalepet::contextapi {

class LocalHttpTransport : public QObject {
    Q_OBJECT
public:
    explicit LocalHttpTransport(JsonRpcDispatcher *dispatcher, QObject *parent = nullptr);
    ~LocalHttpTransport() override;

    // token 非空时要求 `X-WhalePet-Token` 头匹配；**为空则 start() 拒绝启动**。
    void setToken(const QString &token) { m_token = token; }
    QString token() const { return m_token; }

    // 启动监听；port == 0 表示由系统分配。失败时 errorString() 给出原因。
    // token 为空 ⇒ 返回 false（不监听任何端口）。
    bool start(quint16 port = 0);
    void stop();

    bool isListening() const;
    quint16 port() const; // 实际监听端口；未监听返回 0
    QString errorString() const { return m_error; }
    int connectionCount() const { return static_cast<int>(m_buffers.size()); }

    // ---- 资源上限（public 便于单测核验契约，见 tests/test_context_http_security.cpp）----
    static constexpr int kMaxHeaderBytes = 16 * 1024; // 单请求头上限
    static constexpr int kMaxBodyBytes = 1024 * 1024; // 单请求正文上限
    static constexpr int kMaxConnections = 32;        // 并发连接上限
    static constexpr int kRequestTimeoutMs = 10000;   // 单请求最长等待（超时即断开）
    static constexpr int kSweepIntervalMs = 250;      // 超时连接的巡检周期

    // 仅供单测缩短超时（不改变任何安全语义；<= 0 视为关闭超时巡检）
    void setRequestTimeoutMs(int ms) { m_requestTimeoutMs = ms < 0 ? 0 : ms; }
    int requestTimeoutMs() const { return m_requestTimeoutMs; }

private:
    // 解析出的请求头字段（只取本通道关心的几个，其余忽略）
    struct RequestHead {
        int contentLength = -1; // -1 = 缺失
        QString contentType;
        QString origin;
        QString token;
        bool hasTransferEncoding = false;
    };

    struct ConnState {
        QByteArray buffer;
        QElapsedTimer since; // 连接建立（首个字节）时刻，用于超时清理
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void sweepConnections(); // 关闭超时未收全的连接
    void finishConnection(QTcpSocket *socket, bool abort);

    bool parseHead(const QByteArray &head, RequestHead *out, QString *error) const;

    void handleRequestLine(QTcpSocket *socket, const QByteArray &head, const QByteArray &body,
                           const RequestHead &parsed);
    void writeResponse(QTcpSocket *socket, int status, const QByteArray &reason,
                       const QByteArray &contentType, const QByteArray &body);
    void writeJsonRpcError(QTcpSocket *socket, int httpStatus, const QByteArray &reason, int code,
                           const QString &message);
    // 统一「回错误 + 关闭该连接」：超限 / 不可信来源一律走这里，绝不静默丢弃
    void reject(QTcpSocket *socket, int status, const QString &message);

    QTcpServer *m_server = nullptr;
    QTimer *m_sweeper = nullptr; // 周期清理超时连接（避免每连接一个定时器）
    JsonRpcDispatcher *m_dispatcher = nullptr;
    QString m_token;
    QString m_error;
    int m_requestTimeoutMs = kRequestTimeoutMs;
    QHash<QTcpSocket *, ConnState> m_buffers;
};

} // namespace whalepet::contextapi
