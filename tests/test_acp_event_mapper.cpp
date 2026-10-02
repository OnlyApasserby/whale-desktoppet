#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "contextapi/acp/AcpEventMapper.h"
#include "contextapi/acp/AcpSignalRules.h"

// ACP 事件映射（docs/ACP-EVAL.md §6.2）。
//
// **真实 DeepSeekHarness 测试**：夹具 `tests/fixtures/acp-real-events.json` 不是手写样例，
// 而是由真实 dsh（`@deepseek-ai/dsh-acp` 0.1.5-rc.3，`dsh --profile acp`，stdio JSON-RPC）
// 在一次真实会话（initialize → session/new → session/prompt 带工具调用）中采集的**原始报文**。
// 因此本测试同时是「协议假设是否仍然成立」的回归守卫：dsh 若改变事件形状，这里会先失败。
//
// 覆盖点：
//   * 真实报文 → `CoreSignal.kind` 序列（thought / usage / tool_call / tool_call_update / message）；
//   * 工具细分依赖 `title`（实测 dsh 的 `kind` 恒为 "other"，不可作为分类依据）；
//   * 未知变体 / 畸形输入：忽略且不崩溃（不猜测、不误判）；
//   * 事件 → 工作态的映射（`AcpSignalRules`）：thought → vibe-coding、tool.error → debugging，
//     而 `tool.done` / `agent.usage` **不映射**（不改变当前状态）。

using whalepet::contextapi::AcpEventMapper;
using whalepet::contextapi::CoreSignal;
using whalepet::contextapi::mapSignalToWorkState;
using whalepet::core::WorkState;

namespace {

QJsonArray loadRealFixture()
{
    QFile file(QString::fromUtf8(WHALEPET_ACP_FIXTURE));
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonArray();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isArray() ? doc.array() : QJsonArray();
}

// 收集夹具中全部 session/update 通知的映射结果（保持真实顺序）
QList<CoreSignal> mapAllUpdates(const QJsonArray &messages)
{
    QList<CoreSignal> mapped;
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("method")).toString() != QLatin1String("session/update")) {
            continue;
        }
        CoreSignal signal;
        if (AcpEventMapper::mapNotification(message, signal)) {
            mapped.append(signal);
        }
    }
    return mapped;
}

QStringList kindsOf(const QList<CoreSignal> &mapped)
{
    QStringList kinds;
    for (const CoreSignal &signal : mapped) {
        kinds.append(signal.kind);
    }
    return kinds;
}

bool findKind(const QList<CoreSignal> &mapped, const QString &kind, CoreSignal &out)
{
    for (const CoreSignal &signal : mapped) {
        if (signal.kind == kind) {
            out = signal;
            return true;
        }
    }
    return false;
}

} // namespace

class AcpEventMapperTest : public QObject {
    Q_OBJECT
private slots:
    void realFixtureMapsExpectedKinds();
    void realFixtureToolCallPayload();
    void classifyToolCategories();
    void unknownVariantsAreIgnored();
    void malformedInputIsSafe();
    void signalRulesMapAcpKinds();
};

void AcpEventMapperTest::realFixtureMapsExpectedKinds()
{
    const QJsonArray messages = loadRealFixture();
    QVERIFY2(messages.size() >= 7,
             "真实 ACP 夹具缺失或为空（检查 WHALEPET_ACP_FIXTURE 宏与 tests/fixtures/）");

    // 真实会话（带一次 read 工具调用）实测序列
    const QStringList expected = {
        QStringLiteral("agent.thought"),
        QStringLiteral("agent.usage"),
        QStringLiteral("tool.read"),
        QStringLiteral("tool.done"),
        QStringLiteral("agent.thought"),
        QStringLiteral("agent.message"),
        QStringLiteral("agent.usage"),
    };
    QCOMPARE(kindsOf(mapAllUpdates(messages)), expected);
}

void AcpEventMapperTest::realFixtureToolCallPayload()
{
    const QList<CoreSignal> mapped = mapAllUpdates(loadRealFixture());
    QVERIFY(!mapped.isEmpty());

    CoreSignal tool;
    QVERIFY2(findKind(mapped, QStringLiteral("tool.read"), tool),
             "真实报文中的 tool_call 应被归类为 tool.read");
    // 工具名来自 title（实测 "read"）；dsh 声明的 kind 恒为 "other"，仅作诊断保留
    QCOMPARE(tool.payload.value(QStringLiteral("tool")).toString(), QStringLiteral("read"));
    QCOMPARE(tool.payload.value(QStringLiteral("status")).toString(),
             QStringLiteral("in_progress"));
    QCOMPARE(tool.payload.value(QStringLiteral("declaredKind")).toString(),
             QStringLiteral("other"));
    QVERIFY(!tool.payload.value(QStringLiteral("toolCallId")).toString().isEmpty());
    QCOMPARE(tool.sourceId, QStringLiteral("acp"));

    CoreSignal done;
    QVERIFY2(findKind(mapped, QStringLiteral("tool.done"), done),
             "tool_call_update(status=completed) 应映射为 tool.done");
    QCOMPARE(done.payload.value(QStringLiteral("status")).toString(),
             QStringLiteral("completed"));
}

void AcpEventMapperTest::classifyToolCategories()
{
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("read")), QStringLiteral("tool.read"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("bash")), QStringLiteral("tool.command"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("pwsh")), QStringLiteral("tool.command"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("str_replace_editor")),
             QStringLiteral("tool.edit"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("fs_search")),
             QStringLiteral("tool.search"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("web_fetch")),
             QStringLiteral("tool.fetch"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("todo")), QStringLiteral("tool.plan"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("subagent")),
             QStringLiteral("tool.subagent"));
    QCOMPARE(AcpEventMapper::classifyTool(QStringLiteral("mystery_tool")),
             QStringLiteral("tool.other"));
    QCOMPARE(AcpEventMapper::classifyTool(QString()), QStringLiteral("tool.other"));
}

void AcpEventMapperTest::unknownVariantsAreIgnored()
{
    CoreSignal signal;

    // 与实时工作状态无关的变体：忽略（不猜测）
    QJsonObject unrelated;
    unrelated.insert(QStringLiteral("sessionUpdate"), QStringLiteral("current_mode_update"));
    QVERIFY(!AcpEventMapper::mapUpdate(unrelated, signal));

    QJsonObject commands;
    commands.insert(QStringLiteral("sessionUpdate"),
                    QStringLiteral("available_commands_update"));
    QVERIFY(!AcpEventMapper::mapUpdate(commands, signal));

    // 缺 sessionUpdate
    QJsonObject empty;
    QVERIFY(!AcpEventMapper::mapUpdate(empty, signal));

    // 通知缺 params.update
    QJsonObject notification;
    notification.insert(QStringLiteral("method"), QStringLiteral("session/update"));
    QVERIFY(!AcpEventMapper::mapNotification(notification, signal));

    // 非 ACP 消息（如 initialize 响应）不产生信号
    QJsonObject initResponse;
    initResponse.insert(QStringLiteral("id"), 1);
    initResponse.insert(QStringLiteral("result"), QJsonObject());
    QVERIFY(!AcpEventMapper::mapNotification(initResponse, signal));
}

void AcpEventMapperTest::malformedInputIsSafe()
{
    // thought 缺 content：仍映射，文本为空（不崩溃、不伪造内容）
    QJsonObject thought;
    thought.insert(QStringLiteral("sessionUpdate"), QStringLiteral("agent_thought_chunk"));
    CoreSignal signal;
    QVERIFY(AcpEventMapper::mapUpdate(thought, signal));
    QCOMPARE(signal.kind, QStringLiteral("agent.thought"));
    QCOMPARE(signal.payload.value(QStringLiteral("text")).toString(), QString());

    // tool_call 缺 title：归为 tool.other（可观测，不崩溃）
    QJsonObject toolCall;
    toolCall.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call"));
    QVERIFY(AcpEventMapper::mapUpdate(toolCall, signal));
    QCOMPARE(signal.kind, QStringLiteral("tool.other"));

    // tool_call_update 的状态分支
    QList<QStringList> cases;
    cases.append(QStringList{ QStringLiteral("pending"), QStringLiteral("tool.running") });
    cases.append(QStringList{ QStringLiteral("in_progress"), QStringLiteral("tool.running") });
    cases.append(QStringList{ QStringLiteral("cancelled"), QStringLiteral("tool.cancelled") });
    cases.append(QStringList{ QStringLiteral("error"), QStringLiteral("tool.error") });
    cases.append(QStringList{ QStringLiteral("completed"), QStringLiteral("tool.done") });
    for (const QStringList &item : cases) {
        QJsonObject update;
        update.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update"));
        update.insert(QStringLiteral("status"), item.at(0));
        QVERIFY(AcpEventMapper::mapUpdate(update, signal));
        QCOMPARE(signal.kind, item.at(1));
    }
}

void AcpEventMapperTest::signalRulesMapAcpKinds()
{
    CoreSignal signal;

    signal.kind = QStringLiteral("agent.thought");
    QVERIFY(mapSignalToWorkState(signal).mapped);
    QVERIFY(mapSignalToWorkState(signal).state == WorkState::VibeCoding);

    signal.kind = QStringLiteral("tool.edit");
    QVERIFY(mapSignalToWorkState(signal).mapped);
    QVERIFY(mapSignalToWorkState(signal).state == WorkState::Coding);

    signal.kind = QStringLiteral("tool.command");
    QVERIFY(mapSignalToWorkState(signal).mapped);
    QVERIFY(mapSignalToWorkState(signal).state == WorkState::Debugging);

    signal.kind = QStringLiteral("tool.error");
    QVERIFY(mapSignalToWorkState(signal).mapped);
    QVERIFY(mapSignalToWorkState(signal).state == WorkState::Debugging);

    // tool.done / tool.cancelled / agent.usage 不改变当前工作状态
    signal.kind = QStringLiteral("tool.done");
    QVERIFY2(!mapSignalToWorkState(signal).mapped, "tool.done 不应改写工作态");
    signal.kind = QStringLiteral("tool.cancelled");
    QVERIFY2(!mapSignalToWorkState(signal).mapped, "tool.cancelled 不应改写工作态");
    signal.kind = QStringLiteral("agent.usage");
    QVERIFY2(!mapSignalToWorkState(signal).mapped, "agent.usage 不应改写工作态");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    AcpEventMapperTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_acp_event_mapper.moc"
