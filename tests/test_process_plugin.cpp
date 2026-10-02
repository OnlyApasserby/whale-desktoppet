#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "plugin/Capability.h"
#include "plugin/process/ProcessPluginLoader.h"

#include <functional>
#include <memory>

// P7.4 外部进程插件（MCP Client）（docs/PLUGIN-ARCHITECTURE.md §4.2、docs/ROADMAP-P7-Fin.md P7.4）。
//
// 以一个真实子进程（tests/mcp_test_server.cpp，编译为控制台程序）端到端验证：
//   * 配置校验沿用既有语义（空 id / 空 program / 非法 timeout / 重复 id）；
//   * 拉起 + initialize 握手 + tools/list 发现 → ext.<pluginId>.<tool> 能力注册（origin = Process）；
//   * tools/call 异步转发（不阻塞）与结果回投；
//   * 工具错误透传（能力自报 code）；
//   * 调用超时 → requestFailed（不静默挂起）；
//   * 子进程崩溃 → 该来源能力标记不可用、pending 调用以「不可用」回投、其它能力不受影响。

using whalepet::plugin::CapabilityDescriptor;
using whalepet::plugin::CapabilityRegistry;
using whalepet::plugin::InvokeContext;
using whalepet::plugin::PluginOrigin;
using whalepet::plugin::ProcessPluginLoader;
using whalepet::plugin::ProcessServerSpec;

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
    return predicate();
}

// 测试 server 与测试可执行文件同目录（同一构建输出目录）
QString serverProgram()
{
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList names = { QStringLiteral("mcp_test_server.exe"),
                                QStringLiteral("mcp_test_server") };
    for (const QString &name : names) {
        const QString candidate = dir + QLatin1Char('/') + name;
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return dir + QStringLiteral("/mcp_test_server.exe");
}

ProcessServerSpec makeSpec(const QString &pluginId, int timeoutMs = 3000)
{
    ProcessServerSpec spec;
    spec.pluginId = pluginId;
    spec.program = serverProgram();
    spec.timeoutMs = timeoutMs;
    return spec;
}

struct CallResult {
    bool answered = false;
    bool ok = false;
    QJsonObject result;
    int errorCode = 0;
    QString errorMessage;
};

CallResult invokeAsync(CapabilityRegistry &registry, const QString &id, const QJsonObject &params,
                       int waitMs = 5000)
{
    CallResult result;
    QJsonObject out;
    QJsonObject error;
    InvokeContext ctx([&result](const QJsonObject &response) {
        result.answered = true;
        result.ok = response.value(QStringLiteral("ok")).toBool();
        if (result.ok) {
            result.result = response.value(QStringLiteral("result")).toObject();
        } else {
            const QJsonObject value = response.value(QStringLiteral("error")).toObject();
            result.errorCode = value.value(QStringLiteral("code")).toInt();
            result.errorMessage = value.value(QStringLiteral("message")).toString();
        }
    });

    const bool done = registry.invoke(id, params, ctx, out, error);
    if (done) {
        result.answered = true;
        if (error.isEmpty()) {
            result.ok = true;
            result.result = out;
        } else {
            result.ok = false;
            result.errorCode = whalepet::plugin::rpcErrorCode(error);
            result.errorMessage = whalepet::plugin::rpcErrorMessage(error);
        }
        return result;
    }

    waitFor([&result] { return result.answered; }, waitMs);
    return result;
}

} // namespace

class ProcessPluginTest : public QObject {
    Q_OBJECT
private slots:
    void configureValidatesSpecs();
    void loaderStartsAndDiscoversCapabilities();
    void loaderForwardsToolCall();
    void loaderReportsToolError();
    void loaderTimesOutSlowCall();
    void loaderIsolatesProcessCrash();
};

void ProcessPluginTest::configureValidatesSpecs()
{
    ProcessPluginLoader loader;

    ProcessServerSpec noId = makeSpec(QString());
    ProcessServerSpec noProgram = makeSpec(QStringLiteral("p2"));
    noProgram.program.clear();
    ProcessServerSpec badTimeout = makeSpec(QStringLiteral("p3"));
    badTimeout.timeoutMs = 0;

    loader.addServer(noId);
    loader.addServer(noProgram);
    loader.addServer(badTimeout);
    loader.addServer(makeSpec(QStringLiteral("dup")));
    loader.addServer(makeSpec(QStringLiteral("dup"))); // 重复 → 非法

    QCOMPARE(loader.serverCount(), 5);
    QCOMPARE(loader.configure(), 1);
    QCOMPARE(loader.validPluginIds(), QStringList{ QStringLiteral("dup") });
}

void ProcessPluginTest::loaderStartsAndDiscoversCapabilities()
{
    CapabilityRegistry registry;
    ProcessPluginLoader loader;
    loader.addServer(makeSpec(QStringLiteral("t")));

    QCOMPARE(loader.start(registry), 1);
    QVERIFY(loader.running());
    QCOMPARE(loader.sessionCount(), 1);

    QVERIFY(registry.contains(QStringLiteral("ext.t.echo")));
    QVERIFY(registry.contains(QStringLiteral("ext.t.sleep")));
    QVERIFY(registry.contains(QStringLiteral("ext.t.fail")));
    QVERIFY(registry.contains(QStringLiteral("ext.t.crash")));

    const CapabilityDescriptor descriptor =
        registry.find(QStringLiteral("ext.t.echo"))->descriptor();
    QCOMPARE(descriptor.origin, PluginOrigin::Process);
    QCOMPARE(descriptor.displayName, QStringLiteral("echo"));
    QVERIFY(registry.isAvailable(QStringLiteral("ext.t.echo")));

    loader.stop();
    QVERIFY(!loader.running());
    // 停止后该来源能力标记为不可用（能力无法从注册表移除）
    QVERIFY(!registry.isAvailable(QStringLiteral("ext.t.echo")));
}

void ProcessPluginTest::loaderForwardsToolCall()
{
    CapabilityRegistry registry;
    ProcessPluginLoader loader;
    loader.addServer(makeSpec(QStringLiteral("t")));
    QCOMPARE(loader.start(registry), 1);

    QJsonObject params;
    params.insert(QStringLiteral("value"), 42);
    const CallResult result = invokeAsync(registry, QStringLiteral("ext.t.echo"), params);
    QVERIFY2(result.answered, "异步调用必须回投");
    QVERIFY2(result.ok, qPrintable(result.errorMessage));

    const QJsonArray content = result.result.value(QStringLiteral("content")).toArray();
    QVERIFY(!content.isEmpty());
    const QString text = content.first().toObject().value(QStringLiteral("text")).toString();
    QVERIFY2(text.contains(QStringLiteral("42")), qPrintable(text));

    loader.stop();
}

void ProcessPluginTest::loaderReportsToolError()
{
    CapabilityRegistry registry;
    ProcessPluginLoader loader;
    loader.addServer(makeSpec(QStringLiteral("t")));
    QCOMPARE(loader.start(registry), 1);

    const CallResult result = invokeAsync(registry, QStringLiteral("ext.t.fail"), QJsonObject());
    QVERIFY(result.answered);
    QVERIFY2(!result.ok, "工具报错必须以失败回投");
    QCOMPARE(result.errorCode, -32001); // 能力自报的错误码原样透传

    loader.stop();
}

void ProcessPluginTest::loaderTimesOutSlowCall()
{
    CapabilityRegistry registry;
    ProcessPluginLoader loader;
    loader.addServer(makeSpec(QStringLiteral("slow"), 300)); // 超时 300ms
    QCOMPARE(loader.start(registry), 1);

    QJsonObject params;
    params.insert(QStringLiteral("ms"), 1500); // 子进程需 1.5s 才响应
    const CallResult result =
        invokeAsync(registry, QStringLiteral("ext.slow.sleep"), params, 4000);
    QVERIFY(result.answered);
    QVERIFY2(!result.ok, "超时必须回投失败（不得静默挂起）");
    QCOMPARE(result.errorCode, whalepet::plugin::kRpcErrorCapabilityFailed);
    QVERIFY(result.errorMessage.contains(QStringLiteral("超时")));

    loader.stop();
}

void ProcessPluginTest::loaderIsolatesProcessCrash()
{
    CapabilityRegistry registry;
    ProcessPluginLoader loader;
    loader.addServer(makeSpec(QStringLiteral("crashy")));
    QCOMPARE(loader.start(registry), 1);

    QString exitedPlugin;
    bool availabilityWentFalse = false;
    connect(&loader, &ProcessPluginLoader::sessionExited, this,
            [&exitedPlugin](const QString &pluginId) { exitedPlugin = pluginId; });
    connect(&loader, &ProcessPluginLoader::capabilityAvailabilityChanged, this,
            [&availabilityWentFalse](const QString &, bool available) {
                if (!available) {
                    availabilityWentFalse = true;
                }
            });

    // 触发子进程崩溃：pending 调用必须回投「能力不可用」，而不是挂起或带崩主进程
    const CallResult result =
        invokeAsync(registry, QStringLiteral("ext.crashy.crash"), QJsonObject(), 5000);
    QVERIFY(result.answered);
    QVERIFY2(!result.ok, "崩溃后 pending 调用必须以失败回投");
    QCOMPARE(result.errorCode, whalepet::plugin::kRpcErrorCapabilityUnavailable);

    QVERIFY(waitFor([&exitedPlugin] { return exitedPlugin == QStringLiteral("crashy"); }));
    QVERIFY(availabilityWentFalse);
    QVERIFY(!registry.isAvailable(QStringLiteral("ext.crashy.echo")));
    QVERIFY(!loader.running());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ProcessPluginTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_process_plugin.moc"
