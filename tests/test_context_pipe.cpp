#include <QtTest>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "contextapi/ContextApiService.h"
#include "contextapi/JsonRpcDispatcher.h"
#include "contextapi/builtin/ContextCapabilities.h"
#include "contextapi/transport/LocalPipeTransport.h"
#include "core/WorkState.h"
#include "plugin/PluginRegistry.h"

#include <algorithm>
#include <memory>

// P7.2 通道启用与 MCP 桥接（docs/ROADMAP-P7-Fin.md P7.2、docs/CONTEXT-API.md §4）。
//
// 覆盖点：
//   * 命名管道通道（QLocalServer）承载**完整 MCP 会话**：initialize / tools/list / tools/call，
//     与 HTTP / stdio 通道共用同一 dispatcher 与能力表；
//   * token 门控：未握手 / token 错误一律 kRpcErrorUnauthorized（不静默放行）；
//   * 总开关语义：start() 同时启动 HTTP + 管道，stop() 同时关闭（管道名被释放）；
//   * **桥接进程 whalepet-mcp.exe 端到端**：真实子进程 stdin/stdout(stdio)
//     ↔ 命名管道 ↔ 宿主，含 `--token` 注入与「不带 token 被拒」的反例。
//
// 说明：本测试**不依赖真实 MCP 客户端**，用 QLocalSocket / QProcess 直接扮演；
// 全部走本机回环资源（命名管道仅本机可见），可在 CI 稳定运行。

using whalepet::contextapi::ContextApiService;
using whalepet::contextapi::ContextSnapshot;
using whalepet::contextapi::IContextProvider;
using whalepet::contextapi::JsonRpcDispatcher;
using whalepet::contextapi::LocalPipeTransport;
using whalepet::plugin::PluginRegistry;

namespace {

class FakeProvider : public IContextProvider {
public:
    ContextSnapshot snapshot() const override { return m_snapshot; }

    ContextSnapshot m_snapshot;
};

// 每个用例一个互不冲突的管道名（避免并行 / 残留互相干扰）
QString uniquePipeName()
{
    static int counter = 0;
    return QStringLiteral("whalepet-test-pipe-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(++counter);
}

QJsonObject makeRequest(const QString &method, const QJsonObject &params = QJsonObject(), int id = 1)
{
    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), id);
    request.insert(QStringLiteral("method"), method);
    if (!params.isEmpty()) {
        request.insert(QStringLiteral("params"), params);
    }
    return request;
}

QJsonObject makeNotification(const QString &method)
{
    QJsonObject notification;
    notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    notification.insert(QStringLiteral("method"), method);
    return notification;
}

QByteArray frameOf(const QJsonObject &object)
{
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray frame = "Content-Length: ";
    frame.append(QByteArray::number(payload.size()));
    frame.append("\r\n\r\n");
    frame.append(payload);
    return frame;
}

// 从缓冲区取出一个 `Content-Length` 帧；返回 false 表示数据不足
bool takeFrame(QByteArray &buffer, QJsonObject &out)
{
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false;
    }
    int length = -1;
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.toLower().startsWith("content-length:")) {
            bool ok = false;
            length = line.mid(int(qstrlen("content-length:"))).trimmed().toInt(&ok);
            if (!ok) {
                return false;
            }
        }
    }
    if (length < 0) {
        return false;
    }
    const int bodyStart = headerEnd + 4;
    if (buffer.size() - bodyStart < length) {
        return false;
    }
    out = QJsonDocument::fromJson(buffer.mid(bodyStart, length)).object();
    buffer.remove(0, bodyStart + length);
    return true;
}

// MCP 客户端替身：在命名管道上按 Content-Length 分帧收发。
// **必须事件驱动**：宿主的 QLocalServer / QLocalSocket 只有在事件循环运行时才会收到
// readyRead 并回写响应（Windows 命名管道的读写由事件分发器驱动，阻塞式等待会死锁）。
class PipeClient : public QObject {
public:
    explicit PipeClient(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_socket, &QLocalSocket::readyRead, this,
                [this] { m_buffer.append(m_socket.readAll()); });
        connect(&m_socket, &QLocalSocket::disconnected, this, [this] { m_disconnected = true; });
    }

    bool connectTo(const QString &name, int timeoutMs = 5000)
    {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        bool connected = false;
        connect(&m_socket, &QLocalSocket::connected, &loop, [&] {
            connected = true;
            loop.quit();
        });
        connect(&m_socket, &QLocalSocket::errorOccurred, &loop, [&] { loop.quit(); });
        connect(&deadline, &QTimer::timeout, &loop, [&] { loop.quit(); });
        deadline.start(timeoutMs);
        m_socket.connectToServer(name);
        // 命名管道常在本调用内**同步**连上（connected 信号在 exec() 之前就已发出），
        // 此时若直接 exec() 会白等到超时：先查状态。
        if (m_socket.state() == QLocalSocket::ConnectedState) {
            return true;
        }
        loop.exec();
        return connected;
    }

    bool send(const QJsonObject &object)
    {
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            return false;
        }
        const QByteArray frame = frameOf(object);
        // 写缓冲由事件循环冲刷（随后的 receive() 会驱动）；此处只校验已入缓冲
        return m_socket.write(frame) == frame.size();
    }

    bool disconnected() const { return m_disconnected; }

    // 读取下一帧响应；超时 / 断开返回空对象
    QJsonObject receive(int timeoutMs = 5000)
    {
        QJsonObject frame;
        if (takeFrame(m_buffer, frame)) {
            return frame;
        }
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        auto drain = [&] {
            m_buffer.append(m_socket.readAll());
            if (takeFrame(m_buffer, frame)) {
                loop.quit();
            }
        };
        connect(&m_socket, &QLocalSocket::readyRead, &loop, drain);
        connect(&m_socket, &QLocalSocket::disconnected, &loop, [&] { loop.quit(); });
        connect(&deadline, &QTimer::timeout, &loop, [&] { loop.quit(); });
        deadline.start(timeoutMs);
        loop.exec();
        return frame;
    }

    QLocalSocket &socket() { return m_socket; }

private:
    QLocalSocket m_socket;
    QByteArray m_buffer;
    bool m_disconnected = false;
};

// 桥接进程（whalepet-mcp.exe）的 stdio 会话替身。
// 同样必须事件驱动：桥接把请求经命名管道转发给**本进程内的宿主**，宿主需要事件循环
// 才能处理请求并回写响应，因此等待桥接 stdout 期间必须跑事件循环。
class BridgeProcess : public QObject {
public:
    explicit BridgeProcess(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~BridgeProcess() override
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(3000);
        }
    }

    bool start(const QStringList &arguments)
    {
        m_process.setProgram(QString::fromUtf8(WHALEPET_MCP_EXE));
        m_process.setArguments(arguments);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        return m_process.waitForStarted(10000);
    }

    bool send(const QJsonObject &object)
    {
        const QByteArray frame = frameOf(object);
        if (m_process.write(frame) != frame.size()) {
            return false;
        }
        // QProcess 的 stdin 写入是异步的（QWindowsPipeWriter）：必须显式冲刷，
        // 否则子进程的阻塞式读会一直等不到数据。
        return m_process.waitForBytesWritten(5000);
    }

    QJsonObject receive(int timeoutMs = 10000)
    {
        QJsonObject frame;
        if (takeFrame(m_stdout, frame)) {
            return frame;
        }
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        auto drain = [&] {
            m_stdout.append(m_process.readAllStandardOutput());
            if (takeFrame(m_stdout, frame)) {
                loop.quit();
            }
        };
        connect(&m_process, &QProcess::readyReadStandardOutput, &loop, drain);
        connect(&m_process, &QProcess::finished, &loop, [&] { loop.quit(); });
        connect(&deadline, &QTimer::timeout, &loop, [&] { loop.quit(); });
        deadline.start(timeoutMs);
        loop.exec();
        if (frame.isEmpty()) {
            qWarning().noquote() << "[BridgeProcess] 未收到响应; state=" << m_process.state()
                                 << "exitCode=" << m_process.exitCode()
                                 << "stderr=" << QString::fromLocal8Bit(
                                        m_process.readAllStandardError());
        }
        return frame;
    }

    QString stderrText() { return QString::fromLocal8Bit(m_process.readAllStandardError()); }

    bool shutdown(int timeoutMs = 10000)
    {
        if (m_process.state() == QProcess::NotRunning) {
            return true;
        }
        m_process.closeWriteChannel(); // 客户端关闭 stdin ⇒ 桥接退出
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&m_process, &QProcess::finished, &loop, &QEventLoop::quit);
        connect(&deadline, &QTimer::timeout, &loop, [&] { loop.quit(); });
        deadline.start(timeoutMs);
        loop.exec();
        return m_process.state() == QProcess::NotRunning;
    }

    QProcess &process() { return m_process; }

private:
    QProcess m_process;
    QByteArray m_stdout;
};

struct ServerHarness {
    PluginRegistry registry;
    FakeProvider provider;
    std::unique_ptr<JsonRpcDispatcher> dispatcher;
    std::unique_ptr<LocalPipeTransport> pipe;

    void init()
    {
        provider.m_snapshot.workState = whalepet::core::WorkState::Coding;
        provider.m_snapshot.appId = QStringLiteral("Code.exe");
        provider.m_snapshot.petAvailable = true;
        provider.m_snapshot.level = 7;
        registry.add(whalepet::contextapi::makeContextCapabilitiesPlugin(&provider));
        dispatcher = std::make_unique<JsonRpcDispatcher>(&registry.capabilities());
        pipe = std::make_unique<LocalPipeTransport>(dispatcher.get());
    }

    bool listen(const QString &name)
    {
        pipe->setServerName(name);
        return pipe->start();
    }
};

} // namespace

class ContextPipeTest : public QObject {
    Q_OBJECT

private slots:
    void namedPipeServesFullMcpSession();
    void tokenGatingRejectsUntilHandshake();
    void stopReleasesPipeName();
    void serviceToggleStartsAndStopsBothChannels();
    void serviceStartIsIdempotent();
    void bridgeProcessRelaysFullMcpSession();
    void bridgeInjectsTokenFromCommandLine();
};

void ContextPipeTest::namedPipeServesFullMcpSession()
{
    const QString name = uniquePipeName();
    ServerHarness server;
    server.init();
    QVERIFY(server.listen(name));
    QVERIFY(server.pipe->isListening());

    PipeClient client;
    QVERIFY2(client.connectTo(name), "命名管道必须可被本机客户端连接");
    QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 1, 5000);

    // initialize（无 token 配置 ⇒ 无需握手前置）
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    QVERIFY(client.send(makeRequest(QStringLiteral("initialize"), initParams, 1)));
    const QJsonObject initResponse = client.receive();
    QCOMPARE(initResponse.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(initResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("serverInfo"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("whalepet"));

    // tools/list ← capabilities.list（与 HTTP / stdio 同源）
    QVERIFY(client.send(makeRequest(QStringLiteral("tools/list"), QJsonObject(), 2)));
    const QJsonArray tools = client.receive()
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("tools"))
                                 .toArray();
    QStringList names;
    for (const QJsonValue &value : tools) {
        names.append(value.toObject().value(QStringLiteral("name")).toString());
    }
    QStringList expected = whalepet::contextapi::contextCapabilityIds();
    std::sort(names.begin(), names.end());
    std::sort(expected.begin(), expected.end());
    QCOMPARE(names, expected);
    QVERIFY(names.contains(QStringLiteral("context.snapshot")));

    // tools/call ← capability.invoke
    QJsonObject callParams;
    callParams.insert(QStringLiteral("name"), QStringLiteral("context.snapshot"));
    callParams.insert(QStringLiteral("arguments"), QJsonObject());
    QVERIFY(client.send(makeRequest(QStringLiteral("tools/call"), callParams, 3)));
    const QJsonObject snapshot = client.receive().value(QStringLiteral("result")).toObject();
    QCOMPARE(snapshot.value(QStringLiteral("work"))
                 .toObject()
                 .value(QStringLiteral("state"))
                 .toString(),
             QStringLiteral("coding"));

    // 通知（无 id）不产生响应：紧接着的请求必须拿到自己的响应（不错位）
    QVERIFY(client.send(makeNotification(QStringLiteral("notifications/initialized"))));
    QVERIFY(client.send(makeRequest(QStringLiteral("tools/call"), callParams, 4)));
    const QJsonObject afterNotification = client.receive();
    QCOMPARE(afterNotification.value(QStringLiteral("id")).toInt(), 4);
    QVERIFY(afterNotification.value(QStringLiteral("result"))
                .toObject()
                .contains(QStringLiteral("apiVersion")));
}

void ContextPipeTest::tokenGatingRejectsUntilHandshake()
{
    const QString name = uniquePipeName();
    ServerHarness server;
    server.init();
    server.pipe->setToken(QStringLiteral("s3cret"));
    QVERIFY(server.listen(name));

    PipeClient client;
    QVERIFY(client.connectTo(name));
    QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 1, 5000);

    // 未握手：任何非 initialize 请求一律拒绝
    QVERIFY(client.send(makeRequest(QStringLiteral("context.snapshot"), QJsonObject(), 1)));
    QCOMPARE(whalepet::plugin::rpcErrorCode(client.receive().value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorUnauthorized);

    // token 错误：同样拒绝
    QJsonObject badParams;
    badParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    badParams.insert(QStringLiteral("token"), QStringLiteral("wrong"));
    QVERIFY(client.send(makeRequest(QStringLiteral("initialize"), badParams, 2)));
    QCOMPARE(whalepet::plugin::rpcErrorCode(client.receive().value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorUnauthorized);

    // 仍未被放行
    QVERIFY(client.send(makeRequest(QStringLiteral("tools/list"), QJsonObject(), 3)));
    QCOMPARE(whalepet::plugin::rpcErrorCode(client.receive().value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorUnauthorized);

    // token 正确：握手成功，随后可正常调用
    QJsonObject goodParams;
    goodParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    goodParams.insert(QStringLiteral("token"), QStringLiteral("s3cret"));
    QVERIFY(client.send(makeRequest(QStringLiteral("initialize"), goodParams, 4)));
    QVERIFY(client.receive().value(QStringLiteral("result")).toObject().contains(
        QStringLiteral("serverInfo")));

    QVERIFY(client.send(makeRequest(QStringLiteral("tools/list"), QJsonObject(), 5)));
    QVERIFY(client.receive().value(QStringLiteral("result")).toObject().contains(
        QStringLiteral("tools")));
}

void ContextPipeTest::stopReleasesPipeName()
{
    const QString name = uniquePipeName();
    ServerHarness server;
    server.init();
    QVERIFY(server.listen(name));

    PipeClient client;
    QVERIFY(client.connectTo(name));
    QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 1, 5000);

    server.pipe->stop();
    QVERIFY(!server.pipe->isListening());
    QCOMPARE(server.pipe->connectionCount(), 0);

    // 关闭后：已连接客户端被断开，新连接无法建立（管道名已释放）
    QTRY_VERIFY_WITH_TIMEOUT(client.socket().state() != QLocalSocket::ConnectedState, 5000);
    PipeClient late;
    QVERIFY(!late.connectTo(name, 1000));

    // 名字可被重新监听（异常退出后不留残留）
    QVERIFY(server.listen(name));
}

void ContextPipeTest::serviceToggleStartsAndStopsBothChannels()
{
    // 总开关语义：start() 同时监听 HTTP + 管道；stop() 同时关闭（见 ContextApiService.cpp）
    PluginRegistry registry;
    FakeProvider provider;
    provider.m_snapshot.petAvailable = true;
    provider.m_snapshot.level = 3;

    ContextApiService service(&registry, &provider);
    service.setHttpPort(0); // 系统分配，避免与真实实例冲突
    service.setPipeName(uniquePipeName());
    // HTTP 通道要求非空令牌（空令牌 fail closed，见 SECURITY-REVIEW.md #1）
    service.setToken(QStringLiteral("pipe-toggle-token"));
    QVERIFY2(service.start(), qPrintable(QStringLiteral("start 失败：%1").arg(service.errorString())));
    QVERIFY(service.running());
    QVERIFY(service.pipeListening());
    QVERIFY(service.httpPort() != 0);

    // 管道上确认能力可用（服务已配置 token ⇒ 管道同样要求 initialize 握手带上令牌）
    PipeClient client;
    QVERIFY(client.connectTo(service.pipeName()));
    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    params.insert(QStringLiteral("token"), QStringLiteral("pipe-toggle-token"));
    QVERIFY(client.send(makeRequest(QStringLiteral("initialize"), params, 1)));
    QVERIFY(!client.receive().value(QStringLiteral("result")).toObject().isEmpty());
    QJsonObject callParams;
    callParams.insert(QStringLiteral("name"), QStringLiteral("pet.status"));
    callParams.insert(QStringLiteral("arguments"), QJsonObject());
    QVERIFY(client.send(makeRequest(QStringLiteral("tools/call"), callParams, 2)));
    QCOMPARE(client.receive()
                 .value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("level"))
                 .toInt(),
             3);

    // 关闭总开关：两通道都不再监听，且上下文能力被标记不可用
    service.stop();
    QVERIFY(!service.running());
    QVERIFY(!service.pipeListening());
    QCOMPARE(service.httpPort(), quint16(0));
    QVERIFY(!registry.capabilities().isAvailable(QStringLiteral("context.snapshot")));
}

void ContextPipeTest::serviceStartIsIdempotent()
{
    // 回归守卫（docs/pitfalls/ex1/P-089）：组合根可能**重复**调用 start()——从持久化设置
    // 恢复时，菜单 setChecked 先触发一次 toggled，随后又显式应用一次。重复 start() 必须
    // 仍然「两通道都在监听」，绝不能因命名管道「已在监听」而回滚刚起来的 HTTP 通道。
    PluginRegistry registry;
    FakeProvider provider;
    provider.m_snapshot.petAvailable = true;

    ContextApiService service(&registry, &provider);
    service.setHttpPort(0); // 系统分配
    service.setPipeName(uniquePipeName());
    service.setToken(QStringLiteral("pipe-idempotent-token"));

    QVERIFY2(service.start(), qPrintable(service.errorString()));
    QVERIFY(service.pipeListening());
    QVERIFY(service.httpPort() != 0);

    // 第二次 start()：幂等——两通道仍在监听
    QVERIFY2(service.start(), qPrintable(service.errorString()));
    QVERIFY(service.pipeListening());
    QVERIFY(service.httpPort() != 0);

    service.stop();
    QVERIFY(!service.running());
    QVERIFY(!service.pipeListening());
    QCOMPARE(service.httpPort(), quint16(0));
}

void ContextPipeTest::bridgeProcessRelaysFullMcpSession()
{
    const QString name = uniquePipeName();
    ServerHarness server;
    server.init();
    QVERIFY(server.listen(name));

    BridgeProcess bridge;
    QVERIFY2(bridge.start({ QStringLiteral("--pipe"), name }), "whalepet-mcp.exe 必须能启动");
    QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 1, 10000);

    // initialize
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    QVERIFY(bridge.send(makeRequest(QStringLiteral("initialize"), initParams, 1)));
    const QJsonObject init = bridge.receive();
    QCOMPARE(init.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(init.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("serverInfo"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("whalepet"));

    // tools/list
    QVERIFY(bridge.send(makeRequest(QStringLiteral("tools/list"), QJsonObject(), 2)));
    const QJsonArray tools = bridge.receive()
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("tools"))
                                 .toArray();
    QStringList names;
    for (const QJsonValue &value : tools) {
        names.append(value.toObject().value(QStringLiteral("name")).toString());
    }
    QVERIFY(names.contains(QStringLiteral("context.snapshot")));

    // 通知（无 id）不等待响应 → 紧随其后的请求不得错位拿到上一帧
    QVERIFY(bridge.send(makeNotification(QStringLiteral("notifications/initialized"))));
    QJsonObject callParams;
    callParams.insert(QStringLiteral("name"), QStringLiteral("context.snapshot"));
    callParams.insert(QStringLiteral("arguments"), QJsonObject());
    QVERIFY(bridge.send(makeRequest(QStringLiteral("tools/call"), callParams, 3)));
    const QJsonObject called = bridge.receive();
    QCOMPARE(called.value(QStringLiteral("id")).toInt(), 3);
    QCOMPARE(called.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("work"))
                 .toObject()
                 .value(QStringLiteral("state"))
                 .toString(),
             QStringLiteral("coding"));

    QVERIFY2(bridge.shutdown(), qPrintable(QStringLiteral("桥接进程未按预期退出：%1")
                                               .arg(QString::fromLocal8Bit(
                                                   bridge.process().readAllStandardError()))));
    QCOMPARE(bridge.process().exitCode(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 0, 5000);
}

void ContextPipeTest::bridgeInjectsTokenFromCommandLine()
{
    const QString name = uniquePipeName();
    ServerHarness server;
    server.init();
    server.pipe->setToken(QStringLiteral("s3cret"));
    QVERIFY(server.listen(name));

    // 反例：未带 --token ⇒ 宿主以 kRpcErrorUnauthorized 拒绝（不静默放行）
    {
        BridgeProcess bridge;
        QVERIFY(bridge.start({ QStringLiteral("--pipe"), name }));
        QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 1, 10000);
        QJsonObject params;
        params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
        QVERIFY(bridge.send(makeRequest(QStringLiteral("initialize"), params, 1)));
        QCOMPARE(whalepet::plugin::rpcErrorCode(
                     bridge.receive().value(QStringLiteral("error")).toObject()),
                 whalepet::plugin::kRpcErrorUnauthorized);
        QVERIFY(bridge.shutdown());
        QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 0, 5000);
    }

    // 正例：--token 由桥接注入 initialize.params.token ⇒ 全链路可用
    {
        BridgeProcess bridge;
        QVERIFY(bridge.start({ QStringLiteral("--pipe"), name, QStringLiteral("--token"),
                               QStringLiteral("s3cret") }));
        QTRY_VERIFY_WITH_TIMEOUT(server.pipe->connectionCount() == 1, 10000);
        QJsonObject params;
        params.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
        QVERIFY(bridge.send(makeRequest(QStringLiteral("initialize"), params, 1)));
        QVERIFY(bridge.receive().value(QStringLiteral("result")).toObject().contains(
            QStringLiteral("serverInfo")));

        QJsonObject callParams;
        callParams.insert(QStringLiteral("name"), QStringLiteral("pet.status"));
        callParams.insert(QStringLiteral("arguments"), QJsonObject());
        QVERIFY(bridge.send(makeRequest(QStringLiteral("tools/call"), callParams, 2)));
        QCOMPARE(bridge.receive()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("level"))
                     .toInt(),
                 7);
        QVERIFY(bridge.shutdown());
    }
}

QTEST_MAIN(ContextPipeTest)
#include "test_context_pipe.moc"
