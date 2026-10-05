// 测试用最小 ACP Agent（NDJSON over stdio）。
//
// 仅供 tests/test_acp_client.cpp 作为「ACP 子进程」使用，**不是产品组件**：
// 它让 AcpClient 的「拉起 → initialize 握手 → session/new → session/prompt →
// session/update 事件 → 崩溃」整条链路可自动化验证。
//
// 命令行开关：
//   --crash-on-prompt   收到 session/prompt 立即退出（验证崩溃隔离）
//   --permission        先发 session/request_permission，等客户端应答后再产出更新
//
// 依据 ACP v1 规范（agentclientprotocol.com）实现最小子集；
// I/O 一律走标准 C stdio 二进制模式（见 docs/pitfalls/ TRAP-P7-008）。

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#  include <fcntl.h>
#  include <io.h>
#endif

namespace {

const char *const kSessionId = "sess-test-1";
const char *const kOldSessionId = "sess-old-1";
constexpr qint64 kPermissionRequestId = 999;

bool readExact(char *destination, std::size_t count)
{
    std::size_t got = 0;
    while (got < count) {
        const std::size_t read = std::fread(destination + got, 1, count - got, stdin);
        if (read == 0) {
            return false;
        }
        got += read;
    }
    return true;
}

// NDJSON：逐行读取（每条消息一行，禁止内嵌换行）
bool readLine(QByteArray &out)
{
    out.clear();
    char c = 0;
    while (true) {
        if (!readExact(&c, 1)) {
            return false;
        }
        if (c == '\n') {
            if (out.endsWith('\r')) {
                out.chop(1);
            }
            return true;
        }
        out.append(c);
    }
}

void send(const QJsonObject &message)
{
    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    std::fwrite(payload.constData(), 1, static_cast<std::size_t>(payload.size()), stdout);
    std::fflush(stdout);
}

QJsonObject resultMessage(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("result"), result);
    return message;
}

QJsonObject errorMessage(const QJsonValue &id, int code, const QString &text)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), text);
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("error"), error);
    return message;
}

QJsonObject requestMessage(qint64 id, const QString &method, const QJsonObject &params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), static_cast<double>(id));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    return message;
}

QJsonObject updateNotification(const QJsonObject &update)
{
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), QLatin1String(kSessionId));
    params.insert(QStringLiteral("update"), update);
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), QStringLiteral("session/update"));
    message.insert(QStringLiteral("params"), params);
    return message;
}

QJsonObject textChunk(const QString &variant, const QString &messageId, const QString &text)
{
    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);
    QJsonObject update;
    update.insert(QStringLiteral("sessionUpdate"), variant);
    update.insert(QStringLiteral("messageId"), messageId);
    update.insert(QStringLiteral("content"), content);
    return update;
}

// 一次「思考 → 读文件 → 完成 → 回复」的工具回合（与真实 dsh 的事件形状一致）
void emitWorkTurn()
{
    send(updateNotification(textChunk(QStringLiteral("agent_thought_chunk"),
                                      QStringLiteral("msg-1"),
                                      QStringLiteral("我先读一下文件。"))));

    QJsonObject toolCall;
    toolCall.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call"));
    toolCall.insert(QStringLiteral("toolCallId"), QStringLiteral("call-1"));
    toolCall.insert(QStringLiteral("title"), QStringLiteral("read"));
    toolCall.insert(QStringLiteral("kind"), QStringLiteral("other"));
    toolCall.insert(QStringLiteral("status"), QStringLiteral("in_progress"));
    send(updateNotification(toolCall));

    QJsonObject toolUpdate;
    toolUpdate.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update"));
    toolUpdate.insert(QStringLiteral("toolCallId"), QStringLiteral("call-1"));
    toolUpdate.insert(QStringLiteral("status"), QStringLiteral("completed"));
    send(updateNotification(toolUpdate));

    send(updateNotification(textChunk(QStringLiteral("agent_message_chunk"),
                                      QStringLiteral("msg-2"), QStringLiteral("读完了。"))));
}

} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    bool crashOnPrompt = false;
    bool askPermission = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument = argv[i];
        if (argument == "--crash-on-prompt") {
            crashOnPrompt = true;
        } else if (argument == "--permission") {
            askPermission = true;
        }
    }

    std::fprintf(stderr, "[acp_test_agent] started\n");
    std::fflush(stderr);

    QJsonValue promptRequestId;
    bool waitingPermission = false;

    while (true) {
        QByteArray line;
        if (!readLine(line)) {
            break; // 父进程关闭 stdin
        }
        if (line.trimmed().isEmpty()) {
            continue;
        }

        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonValue id = message.value(QStringLiteral("id"));

        // 客户端对权限请求的**应答**（无 method、有 id）
        if (method.isEmpty() && !id.isNull()) {
            const qint64 responseId = static_cast<qint64>(id.toDouble());
            if (waitingPermission && responseId == kPermissionRequestId) {
                waitingPermission = false;
                const QString outcome = message.value(QStringLiteral("result"))
                                            .toObject()
                                            .value(QStringLiteral("outcome"))
                                            .toObject()
                                            .value(QStringLiteral("outcome"))
                                            .toString();
                if (outcome == QLatin1String("selected")) {
                    emitWorkTurn();
                    QJsonObject result;
                    result.insert(QStringLiteral("stopReason"), QStringLiteral("end_turn"));
                    send(resultMessage(promptRequestId, result));
                } else {
                    QJsonObject result;
                    result.insert(QStringLiteral("stopReason"), QStringLiteral("refusal"));
                    send(resultMessage(promptRequestId, result));
                }
            }
            continue;
        }

        if (method == QLatin1String("initialize")) {
            QJsonObject agentInfo;
            agentInfo.insert(QStringLiteral("name"), QStringLiteral("acp-test-agent"));
            agentInfo.insert(QStringLiteral("version"), QStringLiteral("1.0"));
            QJsonObject sessionCaps;
            sessionCaps.insert(QStringLiteral("list"), QJsonObject());
            sessionCaps.insert(QStringLiteral("resume"), QJsonObject());
            QJsonObject capabilities;
            capabilities.insert(QStringLiteral("sessionCapabilities"), sessionCaps);
            QJsonObject result;
            result.insert(QStringLiteral("protocolVersion"), 1);
            result.insert(QStringLiteral("agentInfo"), agentInfo);
            result.insert(QStringLiteral("agentCapabilities"), capabilities);
            send(resultMessage(id, result));
        } else if (method == QLatin1String("session/new")) {
            QJsonObject result;
            result.insert(QStringLiteral("sessionId"), QLatin1String(kSessionId));
            send(resultMessage(id, result));
        } else if (method == QLatin1String("session/list")) {
            QJsonObject entry;
            entry.insert(QStringLiteral("sessionId"), QLatin1String(kOldSessionId));
            QJsonObject result;
            result.insert(QStringLiteral("sessions"), QJsonArray{ entry });
            send(resultMessage(id, result));
        } else if (method == QLatin1String("session/resume")) {
            send(resultMessage(id, QJsonObject()));
        } else if (method == QLatin1String("session/prompt")) {
            promptRequestId = id;
            if (crashOnPrompt) {
                std::fflush(stdout);
                return 9; // 模拟 Agent 崩溃
            }
            if (askPermission) {
                QJsonObject option;
                option.insert(QStringLiteral("optionId"), QStringLiteral("allow-once"));
                option.insert(QStringLiteral("kind"), QStringLiteral("allow_once"));
                QJsonObject toolCall;
                toolCall.insert(QStringLiteral("title"), QStringLiteral("read"));
                QJsonObject params;
                params.insert(QStringLiteral("sessionId"), QLatin1String(kSessionId));
                params.insert(QStringLiteral("toolCall"), toolCall);
                params.insert(QStringLiteral("options"), QJsonArray{ option });
                waitingPermission = true;
                send(requestMessage(kPermissionRequestId,
                                    QStringLiteral("session/request_permission"), params));
                continue; // 等客户端应答
            }
            emitWorkTurn();
            QJsonObject done;
            done.insert(QStringLiteral("stopReason"), QStringLiteral("end_turn"));
            send(resultMessage(promptRequestId, done));
        } else if (method == QLatin1String("session/cancel")) {
            // 通知：不响应
        } else if (!id.isNull()) {
            send(errorMessage(id, -32601, QStringLiteral("未知方法")));
        }
    }

    return 0;
}
