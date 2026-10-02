#include <QtTest>

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "contextapi/acp/AcpAgentBridge.h"
#include "contextapi/acp/AcpSignalRules.h"
#include "contextapi/acp/AcpSignalSource.h"
#include "core/WorkState.h"
#include "viewmodel/AcpSignalService.h"
#include "viewmodel/WorkStateService.h"

#include <cmath>

// P7.5 ACP / IDE Agent 集成（docs/CONTEXT-API.md §6、docs/ROADMAP-P7.md P7.5）。
//
// 覆盖预留接口 ISignalSource / IAgentBridge 的**具体实现**：
//   * AcpSignalSource：JSONL 增量读取 / 顺序 / 非法行忽略 / 未换行尾部 / 截断重置 / setFilePath；
//   * AcpAgentBridge：会话生命周期（幂等）/ push 门控 / 事件落盘；
//   * AcpSignalRules：kind 映射 / payload 显式指定与覆盖 / 未识别不映射；
//   * AcpSignalService：轮询 → 映射 → 广播覆盖性工作态；
//   * WorkStateService：显式信号窗口内覆盖推断、过期回到推断、clearExternalState。

using whalepet::contextapi::AcpAgentBridge;
using whalepet::contextapi::AcpSignalSource;
using whalepet::contextapi::CoreSignal;
using whalepet::contextapi::mapSignalToWorkState;
using whalepet::core::WorkState;
using whalepet::viewmodel::AcpSignalService;
using whalepet::viewmodel::WorkStateService;

namespace {

QString writeLines(const QString &path, const QList<QByteArray> &lines, bool append = false)
{
    QFile file(path);
    const QIODevice::OpenMode mode =
        append ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly;
    if (!file.open(mode)) {
        return QString();
    }
    for (const QByteArray &line : lines) {
        file.write(line);
    }
    file.close();
    return path;
}

} // namespace

class AcpTest : public QObject {
    Q_OBJECT
private slots:
    void signalSourceReadsJsonlInOrder();
    void signalSourceIgnoresInvalidLines();
    void signalSourceHandlesPartialLineAndTruncation();
    void agentBridgeSessionLifecycle();
    void agentBridgeWritesEventLog();
    void signalRulesMapKinds();
    void signalRulesHonourExplicitPayload();
    void signalServiceEmitsOverrides();
    void workStateServiceOverrideWinsUntilExpiry();
};

void AcpTest::signalSourceReadsJsonlInOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/signals.jsonl");

    AcpSignalSource source(QStringLiteral("acp"), path);
    QVERIFY(!source.available());
    CoreSignal signal;
    QVERIFY2(!source.poll(1000, signal), "文件不存在时不得产出信号");

    writeLines(path, { "{\"kind\":\"agent.turn\",\"atMs\":100}\n",
                       "{\"sourceId\":\"vscode\",\"kind\":\"edit.burst\","
                       "\"payload\":{\"x\":1},\"atMs\":200}\n" });
    QVERIFY(source.available());

    QVERIFY(source.poll(1000, signal));
    QCOMPARE(signal.kind, QStringLiteral("agent.turn"));
    QCOMPARE(signal.sourceId, QStringLiteral("acp")); // 缺省用注入 id
    QCOMPARE(signal.atMs, qint64(100));

    QVERIFY(source.poll(1000, signal));
    QCOMPARE(signal.kind, QStringLiteral("edit.burst"));
    QCOMPARE(signal.sourceId, QStringLiteral("vscode"));
    QCOMPARE(signal.payload.value(QStringLiteral("x")).toInt(), 1);
    QCOMPARE(signal.atMs, qint64(200));

    QVERIFY2(!source.poll(1000, signal), "无新行时不得产出假信号");
    QCOMPARE(source.consumedCount(), qint64(2));
}

void AcpTest::signalSourceIgnoresInvalidLines()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/signals.jsonl");
    writeLines(path, { "this is not json\n", "{\"noKind\":true}\n", "{\"kind\":\"idle\"}\n" });

    AcpSignalSource source(QStringLiteral("acp"), path);
    CoreSignal signal;
    QVERIFY(source.poll(1000, signal));
    QCOMPARE(signal.kind, QStringLiteral("idle"));
    QVERIFY2(!source.poll(1000, signal), "非法行不得产出信号");
    QCOMPARE(source.ignoredLineCount(), qint64(2));
    QCOMPARE(source.consumedCount(), qint64(1));
}

void AcpTest::signalSourceHandlesPartialLineAndTruncation()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/signals.jsonl");

    // 无换行结尾：视为「尚未写完」，不消费
    writeLines(path, { "{\"kind\":\"idle\"}" });
    AcpSignalSource source(QStringLiteral("acp"), path);
    CoreSignal signal;
    QVERIFY2(!source.poll(1000, signal), "未换行结尾的行不得被消费");

    // 补上换行 → 可消费
    writeLines(path, { "\n" }, true);
    QVERIFY(source.poll(1000, signal));
    QCOMPARE(signal.kind, QStringLiteral("idle"));

    // 文件被截断（重写为更短内容）→ 从头部重新读取
    writeLines(path, { "{\"kind\":\"afk\"}\n" });
    QVERIFY(source.poll(1000, signal));
    QCOMPARE(signal.kind, QStringLiteral("afk"));

    // setFilePath：切换后旧进度与队列被重置
    const QString other = dir.path() + QStringLiteral("/other.jsonl");
    writeLines(other, { "{\"kind\":\"agent.turn\"}\n" });
    source.setFilePath(other);
    QCOMPARE(source.filePath(), other);
    QVERIFY(source.poll(1000, signal));
    QCOMPARE(signal.kind, QStringLiteral("agent.turn"));
}

void AcpTest::agentBridgeSessionLifecycle()
{
    QTemporaryDir dir;
    AcpAgentBridge bridge(dir.path() + QStringLiteral("/events.jsonl"));

    QVERIFY(bridge.sessions().isEmpty());
    QVERIFY2(!bridge.start(QString()), "空 agentId 不得建立会话");

    QVERIFY(bridge.start(QStringLiteral("vscode")));
    QVERIFY(bridge.start(QStringLiteral("vscode"))); // 幂等
    QCOMPARE(bridge.sessions().size(), 1);

    bridge.stop(QStringLiteral("vscode"));
    QVERIFY(bridge.sessions().isEmpty());
    bridge.stop(QStringLiteral("vscode")); // 幂等（不崩溃）
}

void AcpTest::agentBridgeWritesEventLog()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/events.jsonl");
    AcpAgentBridge bridge(path);

    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("context.changed"));
    QVERIFY2(!bridge.push(QStringLiteral("ghost"), event), "未建立会话不得推送");
    QCOMPARE(bridge.pushedCount(), qint64(0));

    QVERIFY(bridge.start(QStringLiteral("vscode")));
    QVERIFY(bridge.push(QStringLiteral("vscode"), event));
    QCOMPARE(bridge.pushedCount(), qint64(1));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject line = QJsonDocument::fromJson(file.readLine().trimmed()).object();
    QCOMPARE(line.value(QStringLiteral("agentId")).toString(), QStringLiteral("vscode"));
    QCOMPARE(line.value(QStringLiteral("event")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("context.changed"));
}

void AcpTest::signalRulesMapKinds()
{
    CoreSignal signal;
    signal.kind = QStringLiteral("agent.turn");
    auto mapping = mapSignalToWorkState(signal);
    QVERIFY(mapping.mapped);
    QVERIFY(mapping.state == WorkState::VibeCoding);
    QVERIFY(mapping.confidence >= 0.8);
    QVERIFY(mapping.holdMs > 0);

    signal.kind = QStringLiteral("afk");
    QVERIFY(mapSignalToWorkState(signal).state == WorkState::Afk);

    signal.kind = QStringLiteral("no.such.kind");
    QVERIFY2(!mapSignalToWorkState(signal).mapped, "未识别信号不得映射（不猜、不覆盖）");
}

void AcpTest::signalRulesHonourExplicitPayload()
{
    CoreSignal signal;
    signal.kind = QStringLiteral("no.such.kind"); // kind 未识别，但 payload.state 显式指定
    signal.payload.insert(QStringLiteral("state"), QStringLiteral("debugging"));
    auto mapping = mapSignalToWorkState(signal);
    QVERIFY(mapping.mapped);
    QVERIFY(mapping.state == WorkState::Debugging);

    signal.payload.insert(QStringLiteral("confidence"), 0.42);
    signal.payload.insert(QStringLiteral("holdMs"), 1234);
    mapping = mapSignalToWorkState(signal);
    QVERIFY(std::fabs(mapping.confidence - 0.42) < 1e-9);
    QCOMPARE(mapping.holdMs, qint64(1234));
}

void AcpTest::signalServiceEmitsOverrides()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/signals.jsonl");

    AcpSignalSource source(QStringLiteral("acp"), path);
    AcpSignalService service;
    service.setSource(&source);
    QCOMPARE(service.pollNow(), 0); // 不可用 → 无信号

    writeLines(path, { "{\"kind\":\"file.saved\"}\n", "{\"kind\":\"no.such.kind\"}\n" });

    QVector<int> states;
    QVector<qint64> holds;
    connect(&service, &AcpSignalService::workStateOverride, this,
            [&states, &holds](WorkState state, double, qint64, qint64 holdMs) {
                states.append(static_cast<int>(state));
                holds.append(holdMs);
            });

    QCOMPARE(service.pollNow(), 1); // 只有 file.saved 映射成功
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.first(), static_cast<int>(WorkState::Coding));
    QVERIFY(holds.first() > 0);
    QCOMPARE(service.signalCount(), qint64(2));
    QCOMPARE(service.overrideCount(), qint64(1));

    QCOMPARE(service.pollNow(), 0); // 已消费完
}

void AcpTest::workStateServiceOverrideWinsUntilExpiry()
{
    WorkStateService service;

    // 显式信号窗口内：直接采用，不被推断改写
    service.applyExternalState(WorkState::Meeting, 0.9, 1000, 5000);
    QVERIFY(service.hasExternalState(1000));
    QVERIFY(service.current().state == WorkState::Meeting);

    whalepet::core::EnvSample sample;
    sample.nowMs = 3000;
    sample.appId = "Code.exe";
    sample.windowTitle = "main.cpp";
    sample.hasInput = true;
    sample.inputEvents = 60;
    sample.idleMs = 0;
    sample.dwellMs = 120000;
    service.onSample(sample);
    QVERIFY2(service.current().state == WorkState::Meeting,
             "覆盖窗口内显式信号必须优先于推断");

    // 窗口过期：回到推断（Code.exe 不会被推断为 Meeting）
    sample.nowMs = 7000;
    service.onSample(sample);
    QVERIFY(!service.hasExternalState(7000));
    QVERIFY2(service.current().state != WorkState::Meeting, "窗口过期后必须回到推断");

    // clearExternalState：显式清除
    service.applyExternalState(WorkState::Afk, 0.95, 0, 60000);
    QVERIFY(service.hasExternalState());
    service.clearExternalState();
    QVERIFY(!service.hasExternalState());
}

int main(int argc, char *argv[])
{
    // 链 whalepet_view（WorkStateService / AcpSignalService 在 view 层），统一走 offscreen
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    AcpTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_acp.moc"
