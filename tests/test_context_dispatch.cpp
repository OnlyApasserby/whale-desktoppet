#include <QtTest>

#include <QAbstractSocket>
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include "contextapi/ContextApiService.h"
#include "contextapi/JsonRpcDispatcher.h"
#include "contextapi/builtin/ContextCapabilities.h"
#include "contextapi/transport/LocalHttpTransport.h"
#include "contextapi/transport/StdioTransport.h"
#include "plugin/PluginRegistry.h"

#include <algorithm>
#include <memory>

// P7 本地 Context API 骨架（docs/CONTEXT-API.md、docs/ROADMAP-P7-Fin.md P7.0）。
//
// 覆盖点：
//   * JSON-RPC 2.0 校验：缺 method / params 非对象 / 未知方法 / 通知（无 id）；
//   * 「能力别名」路由：方法名 == 能力 id ⇒ 等价 capability.invoke（新增能力无需改分发核心）；
//   * 能力不存在 / 被标记不可用时的错误码；
//   * 门控：默认不监听；start() 后仅回环可访问；stop() 后能力标记为不可用；
//   * 双通道共用同一 dispatcher：本地 HTTP（回环 + 端口 0）与 MCP stdio（内存设备对）
//     对同一请求给出逐字段一致的结果；
//   * MCP 方法映射（initialize / tools/list / tools/call）与 token 鉴权。

using whalepet::contextapi::ContextApiService;
using whalepet::contextapi::ContextSnapshot;
using whalepet::contextapi::IContextProvider;
using whalepet::contextapi::JsonRpcDispatcher;
using whalepet::contextapi::LocalHttpTransport;
using whalepet::contextapi::StdioTransport;
using whalepet::plugin::PluginRegistry;

namespace {

class FakeProvider : public IContextProvider {
public:
    ContextSnapshot snapshot() const override { return m_snapshot; }

    ContextSnapshot m_snapshot;
};

// 组装：能力插件（上下文）→ 注册表 → 分发核心。
// 注意：init() **不注册能力**——「服务是否按需注册」本身就是要被验证的行为；
// 需要能力的用例显式调用 ensureCapabilities() 或直接使用 ContextApiService。
struct Harness {
    PluginRegistry registry;
    FakeProvider provider;
    std::unique_ptr<JsonRpcDispatcher> dispatcher;

    void init()
    {
        provider.m_snapshot.workState = whalepet::core::WorkState::VibeCoding;
        provider.m_snapshot.workConfidence = 0.72;
        provider.m_snapshot.workSinceMs = 1000;
        provider.m_snapshot.envAvailable = true;
        provider.m_snapshot.appId = QStringLiteral("Code.exe");
        provider.m_snapshot.category = whalepet::core::AppCategory::Editor;
        provider.m_snapshot.petAvailable = true;
        provider.m_snapshot.level = 7;
        provider.m_snapshot.samples = 9;
        provider.m_snapshot.interactions = 3;
        provider.m_snapshot.workStateChanges = 2;
    }

    bool ensureCapabilities()
    {
        if (registry.capabilities().contains(QStringLiteral("context.snapshot"))) {
            return true;
        }
        return registry.add(whalepet::contextapi::makeContextCapabilitiesPlugin(&provider));
    }

    JsonRpcDispatcher *ensureDispatcher()
    {
        ensureCapabilities();
        if (!dispatcher) {
            dispatcher = std::make_unique<JsonRpcDispatcher>(&registry.capabilities());
        }
        return dispatcher.get();
    }

    QJsonObject call(const QJsonObject &request) { return ensureDispatcher()->handleSync(request); }
};

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

QByteArray makeStdioFrame(const QJsonObject &request)
{
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QByteArray frame = "Content-Length: ";
    frame.append(QByteArray::number(payload.size()));
    frame.append("\r\n\r\n");
    frame.append(payload);
    return frame;
}

bool frameComplete(const QByteArray &raw)
{
    const int sep = raw.indexOf("\r\n\r\n");
    if (sep < 0) {
        return false;
    }
    int length = -1;
    const QList<QByteArray> lines = raw.left(sep).split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            length = trimmed.mid(15).trimmed().toInt();
        }
    }
    return length >= 0 && raw.size() - (sep + 4) >= length;
}

// 用内存输出设备驱动 stdio 通道（不依赖控制台 / 真实管道）
QJsonObject runStdio(StdioTransport &transport, const QJsonObject &request)
{
    QBuffer output;
    output.open(QIODevice::WriteOnly);
    transport.bind(nullptr, &output);
    transport.feed(makeStdioFrame(request));

    const QByteArray raw = output.data();
    if (!frameComplete(raw)) {
        return QJsonObject();
    }
    const int sep = raw.indexOf("\r\n\r\n");
    int length = 0;
    const QList<QByteArray> lines = raw.left(sep).split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            length = trimmed.mid(15).trimmed().toInt();
        }
    }
    return QJsonDocument::fromJson(raw.mid(sep + 4, length)).object();
}

struct HttpResult {
    int status = 0;
    QByteArray body;
};

HttpResult httpPost(quint16 port, const QByteArray &json, const QByteArray &token)
{
    HttpResult result;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(3000)) {
        return result;
    }

    QByteArray request;
    request.append("POST /rpc HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                   "Content-Type: application/json\r\nContent-Length: ");
    request.append(QByteArray::number(json.size()));
    request.append("\r\nConnection: close\r\n");
    if (!token.isEmpty()) {
        request.append("X-WhalePet-Token: ");
        request.append(token);
        request.append("\r\n");
    }
    request.append("\r\n");
    request.append(json);
    socket.write(request);
    socket.waitForBytesWritten(3000);

    QByteArray raw;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        // 服务器与本测试同线程：必须让事件循环跑起来才能处理 newConnection / readyRead
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (socket.bytesAvailable() > 0) {
            raw.append(socket.readAll());
            const int sep = raw.indexOf("\r\n\r\n");
            if (sep >= 0) {
                const QList<QByteArray> lines = raw.left(sep).split('\n');
                int length = -1;
                for (const QByteArray &line : lines) {
                    const QByteArray trimmed = line.trimmed();
                    if (trimmed.toLower().startsWith("content-length:")) {
                        length = trimmed.mid(15).trimmed().toInt();
                    }
                }
                if (length >= 0 && raw.size() - (sep + 4) >= length) {
                    break;
                }
            }
        }
        QTest::qWait(5);
    }

    const int sep = raw.indexOf("\r\n\r\n");
    if (sep < 0) {
        return result;
    }
    const QList<QByteArray> lines = raw.left(sep).split('\n');
    if (!lines.isEmpty()) {
        const QList<QByteArray> statusLine = lines.first().trimmed().split(' ');
        if (statusLine.size() >= 2) {
            result.status = statusLine.at(1).toInt();
        }
    }
    result.body = raw.mid(sep + 4);
    return result;
}

} // namespace

class ContextDispatchTest : public QObject {
    Q_OBJECT
private slots:
    void pingAndCapabilitiesList();
    void invalidRequestsAreRejected();
    void notificationsProduceNoResponse();
    void contextAliasRoutesToCapability();
    void capabilityInvokeReportsMissingAndUnavailable();
    void serviceIsOffByDefaultAndGatedOnDemand();
    void httpChannelRefusesToStartWithoutToken();
    void httpChannelAnswersJsonRpc();
    void httpChannelEnforcesToken();
    void stdioChannelFramesAndMapsMcp();
};

void ContextDispatchTest::pingAndCapabilitiesList()
{
    Harness harness;
    harness.init();

    const QJsonObject pong = harness.call(makeRequest(QStringLiteral("ping")));
    QCOMPARE(pong.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
    QCOMPARE(pong.value(QStringLiteral("id")).toInt(), 1);
    QVERIFY(pong.value(QStringLiteral("result")).toObject().value(QStringLiteral("pong")).toBool());

    const QJsonObject list = harness.call(makeRequest(QStringLiteral("capabilities.list")));
    const QJsonArray capabilities =
        list.value(QStringLiteral("result")).toObject().value(QStringLiteral("capabilities")).toArray();
    QStringList ids;
    for (const QJsonValue &value : capabilities) {
        const QJsonObject descriptor = value.toObject();
        ids.append(descriptor.value(QStringLiteral("id")).toString());
        QVERIFY(descriptor.value(QStringLiteral("available")).toBool());
        QVERIFY(descriptor.contains(QStringLiteral("inputSchema")));
    }
    // 上下文能力 id 即 JSON-RPC 方法名（「能力别名」路由的前提）
    QStringList expected = whalepet::contextapi::contextCapabilityIds();
    std::sort(ids.begin(), ids.end());
    std::sort(expected.begin(), expected.end());
    QCOMPARE(ids, expected);
}

void ContextDispatchTest::invalidRequestsAreRejected()
{
    Harness harness;
    harness.init();

    // 缺 method
    QJsonObject noMethod;
    noMethod.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    noMethod.insert(QStringLiteral("id"), 5);
    const QJsonObject r1 = harness.call(noMethod);
    QCOMPARE(whalepet::plugin::rpcErrorCode(r1.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorInvalidRequest);

    // 错误的 jsonrpc 版本
    QJsonObject badVersion = makeRequest(QStringLiteral("ping"));
    badVersion.insert(QStringLiteral("jsonrpc"), QStringLiteral("1.0"));
    const QJsonObject r2 = harness.call(badVersion);
    QCOMPARE(whalepet::plugin::rpcErrorCode(r2.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorInvalidRequest);

    // params 不是对象
    QJsonObject arrayParams;
    arrayParams.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    arrayParams.insert(QStringLiteral("id"), 6);
    arrayParams.insert(QStringLiteral("method"), QStringLiteral("ping"));
    arrayParams.insert(QStringLiteral("params"), QJsonArray{ 1, 2 });
    const QJsonObject r3 = harness.call(arrayParams);
    QCOMPARE(whalepet::plugin::rpcErrorCode(r3.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorInvalidParams);

    // 未知方法
    const QJsonObject r4 = harness.call(makeRequest(QStringLiteral("no.such.method")));
    QCOMPARE(whalepet::plugin::rpcErrorCode(r4.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorMethodNotFound);
}

void ContextDispatchTest::notificationsProduceNoResponse()
{
    Harness harness;
    harness.init();

    QJsonObject notification;
    notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    notification.insert(QStringLiteral("method"), QStringLiteral("ping")); // 无 id

    int responses = 0;
    harness.ensureDispatcher()->handle(notification,
                                       [&responses](const QJsonObject &) { ++responses; });
    QCOMPARE(responses, 0);
}

void ContextDispatchTest::contextAliasRoutesToCapability()
{
    Harness harness;
    harness.init();

    // 方法名 == 能力 id ⇒ 等价 capability.invoke
    const QJsonObject work = harness.call(makeRequest(QStringLiteral("context.workState")));
    const QJsonObject workResult = work.value(QStringLiteral("result")).toObject();
    QCOMPARE(workResult.value(QStringLiteral("state")).toString(), QStringLiteral("vibe-coding"));
    QVERIFY(qAbs(workResult.value(QStringLiteral("confidence")).toDouble() - 0.72) < 1e-9);
    QVERIFY(workResult.value(QStringLiteral("focus")).toBool());

    const QJsonObject env = harness.call(makeRequest(QStringLiteral("context.environment")));
    const QJsonObject envResult = env.value(QStringLiteral("result")).toObject();
    QCOMPARE(envResult.value(QStringLiteral("appId")).toString(), QStringLiteral("Code.exe"));
    QCOMPARE(envResult.value(QStringLiteral("category")).toString(), QStringLiteral("editor"));

    const QJsonObject pet = harness.call(makeRequest(QStringLiteral("pet.status")));
    QCOMPARE(pet.value(QStringLiteral("result")).toObject().value(QStringLiteral("level")).toInt(), 7);

    const QJsonObject session = harness.call(makeRequest(QStringLiteral("session.stats")));
    QCOMPARE(session.value(QStringLiteral("result")).toObject().value(QStringLiteral("samples")).toInt(),
             9);

    const QJsonObject snapshot = harness.call(makeRequest(QStringLiteral("context.snapshot")));
    const QJsonObject snapshotResult = snapshot.value(QStringLiteral("result")).toObject();
    QCOMPARE(snapshotResult.value(QStringLiteral("apiVersion")).toString(), QStringLiteral("1.0"));
    QVERIFY(snapshotResult.contains(QStringLiteral("env")));
    QVERIFY(snapshotResult.contains(QStringLiteral("work")));
    QVERIFY(snapshotResult.contains(QStringLiteral("pet")));
    QVERIFY(snapshotResult.contains(QStringLiteral("session")));
}

void ContextDispatchTest::capabilityInvokeReportsMissingAndUnavailable()
{
    Harness harness;
    harness.init();

    // 统一入口调用
    QJsonObject params;
    params.insert(QStringLiteral("id"), QStringLiteral("context.workState"));
    params.insert(QStringLiteral("params"), QJsonObject());
    const QJsonObject invoked =
        harness.call(makeRequest(QStringLiteral("capability.invoke"), params));
    QCOMPARE(invoked.value(QStringLiteral("result")).toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("vibe-coding"));

    // 缺 id
    const QJsonObject noId =
        harness.call(makeRequest(QStringLiteral("capability.invoke"), QJsonObject()));
    QCOMPARE(whalepet::plugin::rpcErrorCode(noId.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorInvalidParams);

    // 能力不存在
    QJsonObject missing;
    missing.insert(QStringLiteral("id"), QStringLiteral("no.such.capability"));
    const QJsonObject missingResult =
        harness.call(makeRequest(QStringLiteral("capability.invoke"), missing));
    QCOMPARE(whalepet::plugin::rpcErrorCode(missingResult.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorMethodNotFound);

    // 能力标记不可用（外部进程插件退出 / 通道关闭时的形态）
    harness.registry.capabilities().setAvailable(QStringLiteral("context.workState"), false);
    const QJsonObject unavailable = harness.call(makeRequest(QStringLiteral("context.workState")));
    QCOMPARE(whalepet::plugin::rpcErrorCode(unavailable.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorCapabilityUnavailable);
}

void ContextDispatchTest::serviceIsOffByDefaultAndGatedOnDemand()
{
    // 本用例**刻意不用 Harness**：Harness 会预注册能力，而这里要验证的正是
    // 「ContextApiService 自己按需注册 / 关闭时不注册」这一门控行为。
    FakeProvider provider;
    provider.m_snapshot.workState = whalepet::core::WorkState::Coding;
    PluginRegistry registry;

    ContextApiService service(&registry, &provider);
    // 默认关闭：不监听任何端口、不注册上下文能力
    QVERIFY(!service.running());
    QCOMPARE(service.httpPort(), quint16(0));
    QVERIFY(!registry.capabilities().contains(QStringLiteral("context.snapshot")));

    // 【安全】未配置令牌 ⇒ HTTP 通道 fail closed（不监听任何端口），但命名管道照常启动
    // （管道浏览器不可达且受 Windows ACL 保护）。见 SECURITY-REVIEW.md #1。
    QVERIFY2(service.start(), qPrintable(service.errorString()));
    QVERIFY(service.running());
    QVERIFY(service.pipeListening());
    QCOMPARE(service.httpPort(), quint16(0));
    QVERIFY(registry.capabilities().contains(QStringLiteral("context.snapshot")));

    service.stop();

    // 配置令牌后：HTTP 与管道同时监听（总开关原子性）
    service.setToken(QStringLiteral("gate-token"));
    QVERIFY2(service.start(), qPrintable(service.errorString()));
    QVERIFY(service.running());
    QVERIFY(service.httpPort() > 0);
    QVERIFY(service.pipeListening());
    QVERIFY(registry.capabilities().isAvailable(QStringLiteral("context.snapshot")));

    const QJsonObject ping = service.handleRequest(makeRequest(QStringLiteral("ping"), QJsonObject(), 1));
    QVERIFY(ping.value(QStringLiteral("result")).toObject().value(QStringLiteral("pong")).toBool());
    QCOMPARE(ping.value(QStringLiteral("id")).toInt(), 1);

    service.stop();
    QVERIFY(!service.running());
    QCOMPARE(service.httpPort(), quint16(0));
    // 通道关闭 → 能力如实标记为不可用（能力无法从注册表移除）
    QVERIFY(!registry.capabilities().isAvailable(QStringLiteral("context.snapshot")));
    const QJsonObject afterStop =
        service.handleRequest(makeRequest(QStringLiteral("context.snapshot")));
    QCOMPARE(whalepet::plugin::rpcErrorCode(afterStop.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorCapabilityUnavailable);
}

void ContextDispatchTest::httpChannelRefusesToStartWithoutToken()
{
    // SECURITY-REVIEW.md #1 的核心回归守卫：**空 token 不得监听端口**。
    // 浏览器可向 127.0.0.1:<port> 发跨站请求，回环绑定不是授权机制。
    Harness harness;
    harness.init();
    JsonRpcDispatcher *dispatcher = harness.ensureDispatcher();

    LocalHttpTransport transport(dispatcher);
    QVERIFY(!transport.start(0));                      // 空 token ⇒ 拒绝启动
    QVERIFY(!transport.isListening());
    QCOMPARE(transport.port(), quint16(0));
    QVERIFY(!transport.errorString().isEmpty());
    QCOMPARE(transport.connectionCount(), 0);

    // 仅空白的令牌同样视为「未配置」
    transport.setToken(QStringLiteral("   \t "));
    QVERIFY(!transport.start(0));
    QVERIFY(!transport.isListening());

    // 配置非空令牌后可正常监听
    transport.setToken(QStringLiteral("tok-123"));
    QVERIFY2(transport.start(0), qPrintable(transport.errorString()));
    QVERIFY(transport.isListening());
    QVERIFY(transport.port() > 0);
    QVERIFY(transport.errorString().isEmpty());

    // 运行期清空令牌必须立即收回端口（不得留下无认证的监听）
    transport.setToken(QString());
    transport.stop();
    QVERIFY(!transport.isListening());
    QCOMPARE(transport.connectionCount(), 0);
}

void ContextDispatchTest::httpChannelAnswersJsonRpc()
{
    Harness harness;
    harness.init();

    ContextApiService service(&harness.registry, &harness.provider);
    service.setToken(QStringLiteral("gate-token")); // HTTP 通道要求非空令牌（fail closed）
    QVERIFY2(service.start(), qPrintable(service.errorString()));
    const quint16 port = service.httpPort();
    QVERIFY(port > 0);

    const QJsonObject request = makeRequest(QStringLiteral("context.workState"), QJsonObject(), 11);
    const HttpResult result = httpPost(port, QJsonDocument(request).toJson(QJsonDocument::Compact),
                                       QByteArray("gate-token"));
    QCOMPARE(result.status, 200);
    const QJsonObject response = QJsonDocument::fromJson(result.body).object();
    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 11);
    QCOMPARE(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("vibe-coding"));

    // 与直接调用分发核心的结果一致（双通道共用同一 dispatcher 与能力表）
    const QJsonObject local = harness.call(request);
    QCOMPARE(QJsonDocument(response).toJson(QJsonDocument::Compact),
             QJsonDocument(local).toJson(QJsonDocument::Compact));

    service.stop();
}

void ContextDispatchTest::httpChannelEnforcesToken()
{
    Harness harness;
    harness.init();

    ContextApiService service(&harness.registry, &harness.provider);
    service.setToken(QStringLiteral("s3cret"));
    QVERIFY2(service.start(), qPrintable(service.errorString()));
    const quint16 port = service.httpPort();

    const QJsonObject request = makeRequest(QStringLiteral("ping"), QJsonObject(), 3);
    const QByteArray body = QJsonDocument(request).toJson(QJsonDocument::Compact);

    const HttpResult unauthorized = httpPost(port, body, QByteArray());
    QCOMPARE(unauthorized.status, 401);
    QCOMPARE(whalepet::plugin::rpcErrorCode(
                 QJsonDocument::fromJson(unauthorized.body).object()
                     .value(QStringLiteral("error"))
                     .toObject()),
             whalepet::plugin::kRpcErrorUnauthorized);

    const HttpResult authorized = httpPost(port, body, QByteArray("s3cret"));
    QCOMPARE(authorized.status, 200);
    QVERIFY(QJsonDocument::fromJson(authorized.body)
                .object()
                .value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("pong"))
                .toBool());

    service.stop();
}

void ContextDispatchTest::stdioChannelFramesAndMapsMcp()
{
    Harness harness;
    harness.init();

    // 必须先经 ensureDispatcher() 让能力注册并创建分发核心（init 只装配假数据）
    StdioTransport transport(harness.ensureDispatcher());
    transport.setToken(QStringLiteral("tok"));
    QVERIFY(!transport.authenticated());

    // 未握手前：除 initialize 外的请求一律拒绝（不静默放行）
    const QJsonObject beforeHandshake =
        runStdio(transport, makeRequest(QStringLiteral("context.workState"), QJsonObject(), 1));
    QCOMPARE(whalepet::plugin::rpcErrorCode(beforeHandshake.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorUnauthorized);

    // 错误的 token
    QJsonObject badInitParams;
    badInitParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    badInitParams.insert(QStringLiteral("token"), QStringLiteral("wrong"));
    const QJsonObject badInit =
        runStdio(transport, makeRequest(QStringLiteral("initialize"), badInitParams, 2));
    QCOMPARE(whalepet::plugin::rpcErrorCode(badInit.value(QStringLiteral("error")).toObject()),
             whalepet::plugin::kRpcErrorUnauthorized);
    QVERIFY(!transport.authenticated());

    // 正确握手
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
    initParams.insert(QStringLiteral("token"), QStringLiteral("tok"));
    const QJsonObject init =
        runStdio(transport, makeRequest(QStringLiteral("initialize"), initParams, 3));
    QVERIFY(transport.authenticated());
    const QJsonObject initResult = init.value(QStringLiteral("result")).toObject();
    QCOMPARE(initResult.value(QStringLiteral("protocolVersion")).toString(),
             QStringLiteral("2026-01-01"));
    QCOMPARE(initResult.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("whalepet"));
    QVERIFY(initResult.contains(QStringLiteral("capabilities")));

    // tools/list ← capabilities.list
    const QJsonObject tools = runStdio(transport, makeRequest(QStringLiteral("tools/list"), QJsonObject(), 4));
    const QJsonArray toolList = tools.value(QStringLiteral("result")).toObject()
                                    .value(QStringLiteral("tools")).toArray();
    QStringList toolNames;
    for (const QJsonValue &value : toolList) {
        toolNames.append(value.toObject().value(QStringLiteral("name")).toString());
    }
    QStringList expectedTools = whalepet::contextapi::contextCapabilityIds();
    std::sort(toolNames.begin(), toolNames.end());
    std::sort(expectedTools.begin(), expectedTools.end());
    QCOMPARE(toolNames, expectedTools);

    // tools/call { name, arguments } → capability.invoke
    QJsonObject callParams;
    callParams.insert(QStringLiteral("name"), QStringLiteral("pet.status"));
    callParams.insert(QStringLiteral("arguments"), QJsonObject());
    const QJsonObject called =
        runStdio(transport, makeRequest(QStringLiteral("tools/call"), callParams, 5));
    QCOMPARE(called.value(QStringLiteral("result")).toObject().value(QStringLiteral("level")).toInt(), 7);

    // 普通 JSON-RPC 方法直通 dispatcher；通知（无 id）不产生响应
    const QJsonObject viaStdio = runStdio(transport, makeRequest(QStringLiteral("ping"), QJsonObject(), 6));
    QVERIFY(viaStdio.value(QStringLiteral("result")).toObject().value(QStringLiteral("pong")).toBool());

    QJsonObject notification;
    notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    notification.insert(QStringLiteral("method"), QStringLiteral("notifications/initialized"));
    const QJsonObject ignored = runStdio(transport, notification);
    QVERIFY2(ignored.isEmpty(), "MCP 通知不得产生响应帧");

    QCOMPARE(transport.framesIn(), qint64(7));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ContextDispatchTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_context_dispatch.moc"
