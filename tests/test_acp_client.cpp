#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include "contextapi/acp/AcpClient.h"

#include <functional>

// P7.6 ACP 客户端（docs/ACP-EVAL.md、docs/ROADMAP-P7.md P7.6）。
//
// 以一个真实子进程（tests/acp_test_agent.cpp）端到端验证：
//   * 启动 + initialize 握手（agentInfo / protocolVersion / 能力）；
//   * session/new 建会话、session/list + session/resume 接管既有会话；
//   * session/prompt 异步驱动，session/update 经 AcpEventMapper 映射为 CoreSignal；
//   * Agent 请求权限时自动应答（可关闭）；
//   * Agent 崩溃 → 隔离（pending 提示结束 + 不阻塞、不崩溃）。
//
// `realDshSmokeOrSkip` 用例在设置环境变量 `WHALEPET_ACP_REAL_DSH=<dsh>/lib/bin.js` 时，
// 会用**真实 DeepSeek Harness**（`dsh --profile acp`）跑一遍；未设置则跳过（CI 友好）。

using whalepet::contextapi::AcpClient;
using whalepet::contextapi::CoreSignal;

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 15000)
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

// 测试 Agent 与测试可执行文件同目录（同一构建输出目录）
QString agentProgram()
{
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList names = { QStringLiteral("acp_test_agent.exe"),
                                QStringLiteral("acp_test_agent") };
    for (const QString &name : names) {
        const QString candidate = dir + QLatin1Char('/') + name;
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return dir + QStringLiteral("/acp_test_agent.exe");
}

} // namespace

class AcpClientTest : public QObject {
    Q_OBJECT
private slots:
    void handshakeAndSession();
    void promptEmitsMappedSignals();
    void listAndResume();
    void permissionAutoApproved();
    void crashIsIsolated();
    void realDshSmokeOrSkip();
};

void AcpClientTest::handshakeAndSession()
{
    AcpClient client;
    client.setProgram(agentProgram());
    client.setTimeoutMs(10000);

    QVERIFY2(client.start(), qPrintable(client.lastError()));
    QVERIFY(client.running());
    QCOMPARE(client.agentName(), QStringLiteral("acp-test-agent"));
    QCOMPARE(client.protocolVersion(), 1);
    QVERIFY(client.supportsSessionList());
    QVERIFY(client.supportsSessionResume());

    QVERIFY2(client.newSession(QDir::tempPath()), qPrintable(client.lastError()));
    QCOMPARE(client.sessionId(), QStringLiteral("sess-test-1"));
    QVERIFY(client.haveSession());

    client.stop();
    QVERIFY(!client.running());
}

void AcpClientTest::promptEmitsMappedSignals()
{
    AcpClient client;
    client.setProgram(agentProgram());
    QVERIFY2(client.start(), qPrintable(client.lastError()));
    QVERIFY(client.newSession(QDir::tempPath()));

    QStringList kinds;
    QString settled;
    connect(&client, &AcpClient::signalMapped, this,
            [&kinds](const CoreSignal &signal) { kinds.append(signal.kind); });
    connect(&client, &AcpClient::promptSettled, this,
            [&settled](const QString &reason) { settled = reason; });

    QVERIFY(client.prompt(QStringLiteral("读一下文件")));
    QVERIFY2(waitFor([&settled] { return !settled.isEmpty(); }), "prompt 未结束");
    QCOMPARE(settled, QStringLiteral("end_turn"));

    const QStringList expected = { QStringLiteral("agent.thought"), QStringLiteral("tool.read"),
                                   QStringLiteral("tool.done"), QStringLiteral("agent.message") };
    QCOMPARE(kinds, expected);
    QCOMPARE(client.updateCount(), qint64(4));
    QCOMPARE(client.signalCount(), qint64(4));

    client.stop();
}

void AcpClientTest::listAndResume()
{
    AcpClient client;
    client.setProgram(agentProgram());
    QVERIFY2(client.start(), qPrintable(client.lastError()));

    const QStringList ids = client.listSessionIds();
    QCOMPARE(ids, QStringList{ QStringLiteral("sess-old-1") });

    QVERIFY2(client.resumeSession(QStringLiteral("sess-old-1"), QDir::tempPath()),
             qPrintable(client.lastError()));
    QCOMPARE(client.sessionId(), QStringLiteral("sess-old-1"));

    client.stop();
}

void AcpClientTest::permissionAutoApproved()
{
    AcpClient client;
    client.setProgram(agentProgram());
    client.setArguments({ QStringLiteral("--permission") });
    client.setAutoApprovePermissions(true);
    QVERIFY2(client.start(), qPrintable(client.lastError()));
    QVERIFY(client.newSession(QDir::tempPath()));

    QString requested;
    QString settled;
    QStringList kinds;
    connect(&client, &AcpClient::permissionRequested, this,
            [&requested](const QString &title) { requested = title; });
    connect(&client, &AcpClient::promptSettled, this,
            [&settled](const QString &reason) { settled = reason; });
    connect(&client, &AcpClient::signalMapped, this,
            [&kinds](const CoreSignal &signal) { kinds.append(signal.kind); });

    QVERIFY(client.prompt(QStringLiteral("需要权限的任务")));
    QVERIFY2(waitFor([&settled] { return !settled.isEmpty(); }), "权限应答后 prompt 未结束");
    QCOMPARE(requested, QStringLiteral("read"));
    QCOMPARE(settled, QStringLiteral("end_turn"));
    QVERIFY2(kinds.contains(QStringLiteral("tool.read")), qPrintable(kinds.join(',')));

    client.stop();
}

void AcpClientTest::crashIsIsolated()
{
    AcpClient client;
    client.setProgram(agentProgram());
    client.setArguments({ QStringLiteral("--crash-on-prompt") });
    QVERIFY2(client.start(), qPrintable(client.lastError()));
    QVERIFY(client.newSession(QDir::tempPath()));

    bool exited = false;
    QString settled;
    connect(&client, &AcpClient::processExited, this,
            [&exited](int, int) { exited = true; });
    connect(&client, &AcpClient::promptSettled, this,
            [&settled](const QString &reason) { settled = reason; });

    QVERIFY(client.prompt(QStringLiteral("触发崩溃")));
    QVERIFY2(waitFor([&exited] { return exited; }), "Agent 崩溃后未上报退出");
    QVERIFY2(waitFor([&settled] { return !settled.isEmpty(); }), "崩溃后 prompt 未结束");
    QCOMPARE(settled, QStringLiteral("agent-exited"));
    QVERIFY(!client.running());
    QVERIFY(!client.haveSession());
}

void AcpClientTest::realDshSmokeOrSkip()
{
    const QString dshBin = qEnvironmentVariable("WHALEPET_ACP_REAL_DSH");
    if (dshBin.isEmpty()) {
        QSKIP("未设置 WHALEPET_ACP_REAL_DSH（<dsh>/lib/bin.js 路径），跳过真实 dsh 烟测");
    }

    AcpClient client;
    client.setProgram(QStringLiteral("node"));
    client.setArguments({ dshBin, QStringLiteral("--profile"), QStringLiteral("acp") });
    client.setTimeoutMs(60000);

    QVERIFY2(client.start(), qPrintable(client.lastError()));
    QCOMPARE(client.agentName(), QStringLiteral("deepseek-harness-acp"));
    QVERIFY(client.supportsSessionList());
    QVERIFY2(client.newSession(QDir::currentPath()), qPrintable(client.lastError()));
    QVERIFY(!client.sessionId().isEmpty());

    QString settled;
    QStringList kinds;
    connect(&client, &AcpClient::promptSettled, this,
            [&settled](const QString &reason) { settled = reason; });
    connect(&client, &AcpClient::signalMapped, this,
            [&kinds](const CoreSignal &signal) { kinds.append(signal.kind); });

    QVERIFY(client.prompt(QStringLiteral("请用一句话介绍你自己。")));
    QVERIFY2(waitFor([&settled] { return !settled.isEmpty(); }, 120000), "真实 dsh prompt 未结束");
    QCOMPARE(settled, QStringLiteral("end_turn"));
    QVERIFY2(kinds.contains(QStringLiteral("agent.message")), qPrintable(kinds.join(',')));

    client.stop();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    AcpClientTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_acp_client.moc"
