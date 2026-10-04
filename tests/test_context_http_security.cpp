// SECURITY-REVIEW.md §极端边界测试建议 1 + 2：本地 Context API 的 HTTP 通道。
//
// 覆盖点（1. HTTP 跨站调用与认证 —— `LocalHttpTransport::handleRequestLine` +
//        `JsonRpcDispatcher::handleSync`）：
//   * 注册一个**有副作用**的假工具（每次调用递增计数器），据此断言「工具是否真的被触发」；
//   * 外部 `Origin` + `text/plain`（CORS 简单请求）⇒ 拒绝，计数器保持 0；
//   * 缺失 token / 错误 token ⇒ 拒绝，计数器保持 0；
//   * 可信来源（无 Origin 的本机客户端 / 同源同端口 Origin）+ 认证正确 ⇒ 允许执行；
//   * 附加不可信来源矩阵（`Origin: null` / 非回环主机 / userinfo / 异端口）；
//   * 响应**从不**带 CORS 头（预检与跨站读取必须失败）。
//
// 覆盖点（2. HTTP 请求缓冲与连接清理 —— `onReadyRead` / `stop`）：
//   * 多连接并发发送超长 / 逐字节延迟的未完成请求头 ⇒ 超限即关闭；
//   * 超大 `Content-Length`、冲突 / 非法长度头、`Transfer-Encoding`（chunked）⇒ 拒绝；
//   * 未结束的超大正文 ⇒ 不无限累积缓冲；
//   * 慢速客户端（超时窗口内不收全）⇒ 被巡检关闭；
//   * 期间重复 `stop` / `start` ⇒ 套接字与缓冲被清理，不留悬挂连接。
//
// 说明：宿主与客户端**同线程**，因此所有等待都必须泵事件循环（QTest::qWait 会
// 处理事件），否则服务端永远收不到 readyRead —— 与 tests/test_context_pipe.cpp
// 里 PipeClient 的同类约定一致（见 docs/traps-P7.md TRAP-P7-012）。

#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include "contextapi/JsonRpcDispatcher.h"
#include "contextapi/transport/LocalHttpTransport.h"
#include "plugin/Capability.h"
#include "plugin/PluginRegistry.h"

#include <memory>

using whalepet::contextapi::JsonRpcDispatcher;
using whalepet::contextapi::LocalHttpTransport;
using whalepet::plugin::PluginRegistry;

namespace {

const char *const kSideEffectCapability = "ext.test.sideEffect";
const char *const kToken = "s3cret-token";

// 有副作用的假工具：每次 invoke 递增计数器（用于断言「是否真的被触发」）
class SideEffectCapability : public whalepet::plugin::SimpleCapability {
public:
    explicit SideEffectCapability(int *counter)
        : SimpleCapability(makeDescriptor())
        , m_counter(counter)
    {
    }

protected:
    bool call(const QJsonObject &, QJsonObject &out, QJsonObject &) override
    {
        ++(*m_counter);
        out.insert(QStringLiteral("count"), *m_counter);
        return true;
    }

private:
    static whalepet::plugin::CapabilityDescriptor makeDescriptor()
    {
        whalepet::plugin::CapabilityDescriptor d;
        d.id = QString::fromLatin1(kSideEffectCapability);
        d.version = QStringLiteral("1.0");
        d.displayName = QStringLiteral("Side Effect");
        d.description = QStringLiteral("测试用：每次调用递增一个计数器");
        d.origin = whalepet::plugin::PluginOrigin::Builtin;
        d.readOnly = false; // 明确标记为「有副作用」
        return d;
    }

    int *m_counter;
};

class SideEffectPlugin : public whalepet::plugin::IPlugin {
public:
    explicit SideEffectPlugin(int *counter)
        : m_counter(counter)
    {
    }

    whalepet::plugin::PluginInfo info() const override
    {
        return { QStringLiteral("test.sideEffect"), QStringLiteral("Side Effect Test"),
                 QStringLiteral("测试用有副作用插件"), QStringLiteral("1.0"), QString() };
    }

    void registerCapabilities(whalepet::plugin::CapabilityRegistry &registry) override
    {
        registry.add(std::make_unique<SideEffectCapability>(m_counter));
    }

private:
    int *m_counter;
};

QByteArray rpcBody(const QString &method, int id = 1)
{
    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), id);
    request.insert(QStringLiteral("method"), method);
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

struct HttpReply {
    int status = 0; // 0 = 未取到响应（连接被关闭 / 超时）
    QByteArray head; // 原始响应头（含状态行）
    QByteArray body;
    bool hasHeader(const QByteArray &name) const
    {
        return head.toLower().contains(name.toLower() + ":");
    }
    int errorCode() const
    {
        const QJsonObject error =
            QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
        return whalepet::plugin::rpcErrorCode(error);
    }
};

// 原始 TCP 客户端：可发任意字节（构造畸形请求）、可长连接、可观察服务端断开
class RawClient {
public:
    bool connectTo(quint16 port, int timeoutMs = 3000)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        return m_socket.waitForConnected(timeoutMs);
    }

    void send(const QByteArray &data) { m_socket.write(data); m_socket.flush(); }

    void close() { m_socket.disconnectFromHost(); }

    bool waitForDisconnect(int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            pump(1);
            if (m_socket.state() != QAbstractSocket::ConnectedState) {
                return true;
            }
        }
        return m_socket.state() != QAbstractSocket::ConnectedState;
    }

    // 读一个完整 HTTP 响应（按 Content-Length 判定结束）
    HttpReply receive(int timeoutMs = 5000)
    {
        HttpReply reply;
        QByteArray raw = m_buffer;
        const int sep = raw.indexOf("\r\n\r\n");
        if (sep < 0) {
            raw.clear();
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < timeoutMs) {
                pump(1);
                raw += m_socket.readAll();
                if (raw.contains("\r\n\r\n")) {
                    break;
                }
            }
            m_buffer.clear();
        }
        if (!raw.contains("\r\n\r\n")) {
            return reply;
        }
        const int sep2 = raw.indexOf("\r\n\r\n");
        reply.head = raw.left(sep2);
        const QList<QByteArray> lines = reply.head.split('\n');
        if (!lines.isEmpty()) {
            const QList<QByteArray> statusLine = lines.first().trimmed().split(' ');
            if (statusLine.size() >= 2) {
                reply.status = statusLine.at(1).toInt();
            }
        }
        int length = 0;
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                length = trimmed.mid(15).trimmed().toInt();
            }
        }
        QByteArray body = raw.mid(sep2 + 4);
        QElapsedTimer timer;
        timer.start();
        while (body.size() < length && timer.elapsed() < timeoutMs) {
            pump(1);
            body += m_socket.readAll();
        }
        reply.body = body.left(length);
        return reply;
    }

    void pump(int ms) { QTest::qWait(ms); }

private:
    QTcpSocket m_socket;
    QByteArray m_buffer;
};

} // namespace

class ContextHttpSecurityTest : public QObject {
    Q_OBJECT

private slots:
    void sideEffectToolStaysAtZeroForUntrustedOrUnauthenticated();
    void untrustedOriginMatrixIsRejected();
    void contentTypeMustBeJson();
    void responsesNeverCarryCorsHeaders();
    void oversizedHeaderIsClosed();
    void oversizedAndInvalidContentLengthAreRejected();
    void unterminatedBodyDoesNotAccumulate();
    void slowClientIsClosedAfterTimeout();
    void repeatedStopStartReleasesEverything();
};

void ContextHttpSecurityTest::sideEffectToolStaysAtZeroForUntrustedOrUnauthenticated()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();
    QVERIFY(port > 0);

    const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));

    // 负例 1：外部 Origin + text/plain（浏览器 CORS「简单请求」，无需预检即可直发本机端口）
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                             "Origin: https://evil.example.com\r\n"
                             "Content-Type: text/plain;charset=UTF-8\r\n"
                             "X-WhalePet-Token: ";
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        // Content-Type 门在 Origin 门之前 ⇒ 415；无论命中哪一道门都必须拒绝
        QCOMPARE(reply.status, 415);
        QCOMPARE(reply.errorCode(), whalepet::plugin::kRpcErrorInvalidRequest);
        QCOMPARE(calls, 0); // 关键断言：副作用工具一次都没被触发
    }

    // 负例 2：可信 Content-Type + 正确 token，但带外部 Origin（跨站）
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                             "Origin: https://evil.example.com\r\n"
                             "Content-Type: application/json\r\n"
                             "X-WhalePet-Token: ";
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        QCOMPARE(reply.status, 403);
        QCOMPARE(reply.errorCode(), whalepet::plugin::kRpcErrorUnauthorized);
        QCOMPARE(calls, 0);
    }

    // 负例 3：缺失 token
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                             "Content-Type: application/json\r\nContent-Length: ";
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        QCOMPARE(reply.status, 401);
        QCOMPARE(reply.errorCode(), whalepet::plugin::kRpcErrorUnauthorized);
        QCOMPARE(calls, 0);
    }

    // 负例 4：错误 token
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                             "Content-Type: application/json\r\n"
                             "X-WhalePet-Token: wrong\r\nContent-Length: ";
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        QCOMPARE(reply.status, 401);
        QCOMPARE(calls, 0);
    }

    // 负例 5：token 只差一个字符（前缀相同 ⇒ 定长比较必须真的逐字节比）
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                             "Content-Type: application/json\r\n"
                             "X-WhalePet-Token: s3cret-toke\r\nContent-Length: ";
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        QCOMPARE(client.receive().status, 401);
        QCOMPARE(calls, 0);
    }

    // 正例：本机客户端（不带 Origin）+ 认证正确 ⇒ 允许执行
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                             "Content-Type: application/json\r\n"
                             "X-WhalePet-Token: ";
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        QCOMPARE(reply.status, 200);
        QCOMPARE(calls, 1); // 副作用工具被触发
        QCOMPARE(QJsonDocument::fromJson(reply.body)
                     .object()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("count"))
                     .toInt(),
                 1);
    }

    // 正例：同源同端口 Origin（浏览器从本机其它页面访问）⇒ 允许
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://127.0.0.1:";
        request.append(QByteArray::number(port));
        request.append("\r\nContent-Type: application/json\r\n"
                       "X-WhalePet-Token: ");
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        QCOMPARE(client.receive().status, 200);
        QCOMPARE(calls, 2);
    }

    transport.stop();
}

void ContextHttpSecurityTest::untrustedOriginMatrixIsRejected()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();
    const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));

    // 逐一验证：这些 Origin 一律视为不可信（403），且工具一次都不被触发
    const QStringList untrusted = {
        QStringLiteral("null"),                                  // file:// / 隐私降级
        QStringLiteral("http://evil.example.com"),               // 外部主机
        QStringLiteral("https://evil.example.com:443"),          // 外部主机（https）
        QStringLiteral("http://localhost.evil.com"),            // 后缀混淆
        QStringLiteral("http://user@127.0.0.1:%1").arg(port),    // userinfo 伪装
        QStringLiteral("http://127.0.0.2:%1").arg(port),          // 非本机回环
        QStringLiteral("http://[::1]:%1").arg(port + 1),         // 异端口（IPv6 回环）
        QStringLiteral("file:///C:/evil.html"),                   // 非 http(s)
        QStringLiteral("http://127.0.0.1"),                      // 缺端口
    };
    for (const QString &origin : untrusted) {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: ";
        request.append(origin.toUtf8());
        request.append("\r\nContent-Type: application/json\r\n"
                       "X-WhalePet-Token: ");
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        QVERIFY2(reply.status == 403,
                 qPrintable(QStringLiteral("Origin=%1 期望 403，实际 %2")
                                .arg(origin)
                                .arg(reply.status)));
        QCOMPARE(calls, 0);
    }

    // 对照：IPv6 回环 + 正确端口 ⇒ 视为可信来源（仍需 token）
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://[::1]:";
        request.append(QByteArray::number(port));
        request.append("\r\nContent-Type: application/json\r\n"
                       "X-WhalePet-Token: ");
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        QCOMPARE(client.receive().status, 200);
        QCOMPARE(calls, 1);
    }

    transport.stop();
}

void ContextHttpSecurityTest::contentTypeMustBeJson()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();
    const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));

    // 允许的写法：application/json 及其带 charset / 大小写变体
    const QStringList allowed = { QStringLiteral("application/json"),
                                  QStringLiteral("application/json; charset=utf-8"),
                                  QStringLiteral("Application/JSON") };
    for (const QString &type : allowed) {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: ";
        request.append(type.toUtf8());
        request.append("\r\nX-WhalePet-Token: ");
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        QCOMPARE(client.receive().status, 200);
    }
    QCOMPARE(calls, allowed.size());

    // 拒绝的写法：缺失 / text/plain / 表单编码 / 其它 json 变体（均 415）
    const QStringList rejected = { QString(), QStringLiteral("text/plain"),
                                   QStringLiteral("application/x-www-form-urlencoded"),
                                   QStringLiteral("text/json"),
                                   QStringLiteral("application/json-patch+json") };
    for (const QString &type : rejected) {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        if (!type.isEmpty()) {
            request.append("Content-Type: ");
            request.append(type.toUtf8());
            request.append("\r\n");
        }
        request.append("X-WhalePet-Token: ");
        request.append(kToken);
        request.append("\r\nContent-Length: ");
        request.append(QByteArray::number(body.size()));
        request.append("\r\n\r\n");
        request.append(body);
        client.send(request);
        const HttpReply reply = client.receive();
        QVERIFY2(reply.status == 415,
                 qPrintable(QStringLiteral("Content-Type=%1 期望 415，实际 %2")
                                .arg(type.isEmpty() ? QStringLiteral("<缺失>") : type)
                                .arg(reply.status)));
    }
    // 关键断言：非法 Content-Type 一律不触发副作用
    QCOMPARE(calls, allowed.size());

    transport.stop();
}

void ContextHttpSecurityTest::responsesNeverCarryCorsHeaders()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();

    // 正例响应（200）也不得带任何 CORS 头：跨站页面即便能发出请求也读不到响应，
    // 且 `application/json` 的预检必然失败（本通道从不回 Access-Control-Allow-*）。
    RawClient client;
    QVERIFY(client.connectTo(port));
    const QByteArray body = rpcBody(QStringLiteral("ping"));
    QByteArray request = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                         "Content-Type: application/json\r\nX-WhalePet-Token: ";
    request.append(kToken);
    request.append("\r\nContent-Length: ");
    request.append(QByteArray::number(body.size()));
    request.append("\r\n\r\n");
    request.append(body);
    client.send(request);
    const HttpReply reply = client.receive();
    QCOMPARE(reply.status, 200);
    QVERIFY(!reply.hasHeader("Access-Control-Allow-Origin"));
    QVERIFY(!reply.hasHeader("Access-Control-Allow-Headers"));
    QVERIFY(reply.hasHeader("X-Content-Type-Options")); // nosniff

    // 错误响应同样不得带 CORS 头
    RawClient denied;
    QVERIFY(denied.connectTo(port));
    QByteArray bad = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/plain\r\n"
                     "X-WhalePet-Token: ";
    bad.append(kToken);
    bad.append("\r\nContent-Length: ");
    bad.append(QByteArray::number(body.size()));
    bad.append("\r\n\r\n");
    bad.append(body);
    denied.send(bad);
    const HttpReply deniedReply = denied.receive();
    QCOMPARE(deniedReply.status, 415);
    QVERIFY(!deniedReply.hasHeader("Access-Control-Allow-Origin"));

    transport.stop();
}

void ContextHttpSecurityTest::oversizedHeaderIsClosed()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();
    QVERIFY(port > 0);

    // 负例 A：未完成（无空行结束）的超长请求头 —— 逐块灌入，超过上限即被关闭
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray head = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Pad: ";
        // 目标：请求头部分超过 kMaxHeaderBytes
        const int pad = LocalHttpTransport::kMaxHeaderBytes + 4096;
        for (int i = 0; i < pad; ++i) {
            head.append('a');
        }
        // 故意不追加 "\r\n\r\n"：服务端永远等不到头部结束
        client.send(head);
        const HttpReply reply = client.receive();
        QCOMPARE(reply.status, 431);
        // 服务端已关闭连接
        QVERIFY(client.waitForDisconnect(5000));
    }

    // 负例 B：多连接并发（4 条）各自灌超长头部 —— 全部被关闭，缓冲不无限增长
    {
        QList<RawClient *> clients;
        for (int i = 0; i < 4; ++i) {
            auto *client = new RawClient;
            QVERIFY(client->connectTo(port));
            clients.append(client);
        }
        for (RawClient *client : clients) {
            QByteArray head = "POST /rpc HTTP/1.1\r\nX-Pad: ";
            head.append(QByteArray(LocalHttpTransport::kMaxHeaderBytes + 2048, 'b'));
            client->send(head);
        }
        for (RawClient *client : clients) {
            QCOMPARE(client->receive().status, 431);
            QVERIFY(client->waitForDisconnect(5000));
        }
        qDeleteAll(clients);
    }

    // 负例 C：逐字节延迟的未完成请求头 —— 累积超过上限后同样被关闭
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray head = "POST /rpc HTTP/1.1\r\nX-Pad: ";
        head.append(QByteArray(LocalHttpTransport::kMaxHeaderBytes + 1024, 'c'));
        for (int i = 0; i < head.size(); i += 512) {
            client.send(head.mid(i, 512));
            client.pump(1);
        }
        const HttpReply reply = client.receive();
        QCOMPARE(reply.status, 431);
        QVERIFY(client.waitForDisconnect(5000));
    }

    QCOMPARE(calls, 0); // 任何畸形请求都不得触发副作用工具
    transport.stop();
}

void ContextHttpSecurityTest::oversizedAndInvalidContentLengthAreRejected()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();
    const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));

    auto sendHead = [port, body](const QByteArray &head) {
        RawClient client;
        if (!client.connectTo(port)) {
            return HttpReply();
        }
        client.send(head);
        return client.receive();
    };

    QByteArray base = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                      "Content-Type: application/json\r\nX-WhalePet-Token: ";
    base.append(kToken);
    base.append("\r\n");

    // 超大 Content-Length（超过正文上限）⇒ 413，且不等待正文
    {
        QByteArray head = base;
        head.append("Content-Length: ");
        head.append(QByteArray::number(LocalHttpTransport::kMaxBodyBytes + 1));
        head.append("\r\n\r\n");
        QCOMPARE(sendHead(head).status, 413);
    }

    // 极大（溢出 32 位 / 64 位）Content-Length ⇒ 413（解析成功但超限）
    {
        QByteArray head = base;
        head.append("Content-Length: 99999999999999\r\n\r\n");
        QCOMPARE(sendHead(head).status, 413);
    }

    // 负数 Content-Length ⇒ 400（非法）
    {
        QByteArray head = base;
        head.append("Content-Length: -1\r\n\r\n");
        QCOMPARE(sendHead(head).status, 400);
    }

    // 非数字 Content-Length ⇒ 400
    {
        QByteArray head = base;
        head.append("Content-Length: abc\r\n\r\n");
        QCOMPARE(sendHead(head).status, 400);
    }

    // 冲突的两个 Content-Length（请求走私的经典入口）⇒ 400
    {
        QByteArray head = base;
        head.append("Content-Length: 2\r\nContent-Length: 3\r\n\r\n{}");
        QCOMPARE(sendHead(head).status, 400);
    }

    // 缺失 Content-Length（POST 定长正文要求）⇒ 411
    {
        QByteArray head = base;
        head.append("\r\n");
        head.append(body);
        QCOMPARE(sendHead(head).status, 411);
    }

    // Transfer-Encoding: chunked（本通道不支持）⇒ 501，且不等待 chunked 正文
    {
        QByteArray head = base;
        head.append("Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
        QCOMPARE(sendHead(head).status, 501);
    }

    // 畸形头部行（缺冒号）⇒ 400
    {
        QByteArray head = base;
        head.append("GarbageHeaderLine\r\nContent-Length: 2\r\n\r\n{}");
        QCOMPARE(sendHead(head).status, 400);
    }

    QCOMPARE(calls, 0);
    transport.stop();
}

void ContextHttpSecurityTest::unterminatedBodyDoesNotAccumulate()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();
    QVERIFY(port > 0);

    // 声明了一个**合法但很大**的 Content-Length，然后只发开头一点点并停住：
    // 服务端必须等待（不误解析半截正文），且缓冲不超过「头 + 声明长度」。
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        QByteArray head = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\nX-WhalePet-Token: ";
        head.append(kToken);
        head.append("\r\nContent-Length: ");
        head.append(QByteArray::number(LocalHttpTransport::kMaxBodyBytes));
        head.append("\r\n\r\n");
        head.append("{\"jsonrpc\""); // 只发 10 字节，远未收全
        client.send(head);
        client.pump(200);
        // 无响应（仍在等待正文），且副作用工具未被触发
        const HttpReply early = client.receive(300);
        QCOMPARE(early.status, 0);
        QCOMPARE(calls, 0);
        // 连接仍然存在（未误判为超限而关闭）
        QVERIFY(transport.connectionCount() >= 1);
        client.close();
    }

    // 补齐正文后正常放行：证明「等待」不是「丢弃」
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));
        QByteArray head = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\nX-WhalePet-Token: ";
        head.append(kToken);
        head.append("\r\nContent-Length: ");
        head.append(QByteArray::number(body.size()));
        head.append("\r\n\r\n");
        client.send(head);
        client.pump(100);
        QCOMPARE(client.receive(300).status, 0); // 头已收全、正文未到 ⇒ 仍等待
        client.send(body);
        QCOMPARE(client.receive().status, 200);
        QCOMPARE(calls, 1);
    }

    transport.stop();
}

void ContextHttpSecurityTest::slowClientIsClosedAfterTimeout()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    // 缩短超时窗口（仅影响测试节奏，不改变任何安全语义）
    transport.setRequestTimeoutMs(600);
    QCOMPARE(transport.requestTimeoutMs(), 600);
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    const quint16 port = transport.port();

    // 三条慢速连接：连上后既不发完整请求，也不断开
    QList<RawClient *> clients;
    for (int i = 0; i < 3; ++i) {
        auto *client = new RawClient;
        QVERIFY(client->connectTo(port));
        client->send("POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Pad: ");
        clients.append(client);
    }
    QTRY_VERIFY_WITH_TIMEOUT(transport.connectionCount() == 3, 3000);

    // 超时巡检必须主动关闭这些连接（不依赖客户端自觉）
    QTRY_VERIFY_WITH_TIMEOUT(transport.connectionCount() == 0, 8000);
    for (RawClient *client : clients) {
        QVERIFY2(client->waitForDisconnect(3000), "慢速客户端应被服务端超时关闭");
    }
    qDeleteAll(clients);
    QCOMPARE(calls, 0);

    // 超时后通道仍可正常服务新请求（巡检没有把通道本身弄坏）
    {
        RawClient client;
        QVERIFY(client.connectTo(port));
        const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));
        QByteArray head = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\nX-WhalePet-Token: ";
        head.append(kToken);
        head.append("\r\nContent-Length: ");
        head.append(QByteArray::number(body.size()));
        head.append("\r\n\r\n");
        head.append(body);
        client.send(head);
        QCOMPARE(client.receive().status, 200);
        QCOMPARE(calls, 1);
    }

    transport.stop();
}

void ContextHttpSecurityTest::repeatedStopStartReleasesEverything()
{
    int calls = 0;
    PluginRegistry registry;
    QVERIFY(registry.add(std::make_unique<SideEffectPlugin>(&calls)));
    JsonRpcDispatcher dispatcher(&registry.capabilities());

    LocalHttpTransport transport(&dispatcher);
    transport.setToken(QString::fromLatin1(kToken));
    const QByteArray body = rpcBody(QString::fromLatin1(kSideEffectCapability));

    for (int round = 0; round < 3; ++round) {
        QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
        const quint16 port = transport.port();
        QVERIFY(port > 0);

        // 反复重复启动 ⇒ 明确失败且不改变已监听的端口（幂等保护）
        QVERIFY(!transport.start(0));
        QVERIFY(transport.isListening());
        QCOMPARE(transport.port(), port);

        // 每轮留两条悬挂连接（一条畸形头、一条半截正文），随后 stop()
        QList<RawClient *> clients;
        for (int i = 0; i < 2; ++i) {
            auto *client = new RawClient;
            QVERIFY(client->connectTo(port));
            client->send(i == 0 ? "POST /rpc HTTP/1.1\r\nX-Pad: " : "POST /rpc HTTP/1.1\r\n");
            clients.append(client);
        }
        QTRY_VERIFY_WITH_TIMEOUT(transport.connectionCount() == 2, 3000);

        transport.stop();
        // stop() 必须回收套接字与缓冲，不留悬挂连接
        QCOMPARE(transport.connectionCount(), 0);
        QVERIFY(!transport.isListening());
        QCOMPARE(transport.port(), quint16(0));
        for (RawClient *client : clients) {
            QVERIFY(client->waitForDisconnect(3000));
        }
        qDeleteAll(clients);
    }

    // 重复 stop() 幂等
    transport.stop();
    transport.stop();
    QCOMPARE(transport.connectionCount(), 0);

    // 多轮启停后仍能正常工作（状态未被污染）
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    {
        RawClient client;
        QVERIFY(client.connectTo(transport.port()));
        QByteArray head = "POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\nX-WhalePet-Token: ";
        head.append(kToken);
        head.append("\r\nContent-Length: ");
        head.append(QByteArray::number(body.size()));
        head.append("\r\n\r\n");
        head.append(body);
        client.send(head);
        QCOMPARE(client.receive().status, 200);
        QCOMPARE(calls, 1);
    }
    transport.stop();
    QCOMPARE(calls, 1);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ContextHttpSecurityTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_context_http_security.moc"
