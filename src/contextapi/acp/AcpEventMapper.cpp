#include "contextapi/acp/AcpEventMapper.h"

#include <QJsonArray>
#include <QJsonValue>

#include <initializer_list>

namespace whalepet::contextapi {

namespace {

const char *const kFieldVariant = "sessionUpdate";
const char *const kFieldText = "text";

// ACP 变体名（与 ACP v1 规范一致；本实现只映射用到的子集）
const char *const kVariantThought = "agent_thought_chunk";
const char *const kVariantMessage = "agent_message_chunk";
const char *const kVariantToolCall = "tool_call";
const char *const kVariantToolCallUpdate = "tool_call_update";
const char *const kVariantPlan = "plan";
const char *const kVariantUsage = "usage_update";

const char *const kSourceId = "acp";

bool containsAny(const QString &lower, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        if (lower.contains(QLatin1String(key))) {
            return true;
        }
    }
    return false;
}

} // namespace

QString AcpEventMapper::textOf(const QJsonObject &update)
{
    const QJsonObject content = update.value(QStringLiteral("content")).toObject();
    return content.value(QLatin1String(kFieldText)).toString();
}

QString AcpEventMapper::classifyTool(const QString &toolName)
{
    const QString lower = toolName.toLower();
    if (lower.isEmpty()) {
        return QStringLiteral("tool.other");
    }

    // 顺序即优先级：命令类最具体，编辑/读取次之，最后回退
    if (containsAny(lower, { "bash", "pwsh", "shell", "terminal", "command", "exec" })) {
        return QStringLiteral("tool.command");
    }
    if (containsAny(lower, { "edit", "write", "replace", "patch", "apply" })) {
        return QStringLiteral("tool.edit");
    }
    if (containsAny(lower, { "search", "grep", "glob", "find", "list" })) {
        return QStringLiteral("tool.search");
    }
    if (containsAny(lower, { "read", "cat", "open" })) {
        return QStringLiteral("tool.read");
    }
    if (containsAny(lower, { "web", "fetch", "http", "download" })) {
        return QStringLiteral("tool.fetch");
    }
    if (containsAny(lower, { "todo", "plan" })) {
        return QStringLiteral("tool.plan");
    }
    if (containsAny(lower, { "subagent", "task", "delegate" })) {
        return QStringLiteral("tool.subagent");
    }
    return QStringLiteral("tool.other");
}

bool AcpEventMapper::mapUpdate(const QJsonObject &update, CoreSignal &out)
{
    const QString variant = update.value(QLatin1String(kFieldVariant)).toString();
    if (variant.isEmpty()) {
        return false;
    }

    out = CoreSignal();
    out.sourceId = QLatin1String(kSourceId);
    out.kind.clear();

    if (variant == QLatin1String(kVariantThought)) {
        out.kind = QStringLiteral("agent.thought");
        out.payload.insert(QLatin1String(kFieldText), textOf(update));
        return true;
    }
    if (variant == QLatin1String(kVariantMessage)) {
        out.kind = QStringLiteral("agent.message");
        out.payload.insert(QLatin1String(kFieldText), textOf(update));
        return true;
    }
    if (variant == QLatin1String(kVariantToolCall)) {
        const QString tool = update.value(QStringLiteral("title")).toString();
        out.kind = classifyTool(tool);
        out.payload.insert(QStringLiteral("tool"), tool);
        out.payload.insert(QStringLiteral("toolCallId"),
                           update.value(QStringLiteral("toolCallId")).toString());
        out.payload.insert(QStringLiteral("status"),
                           update.value(QStringLiteral("status")).toString());
        // 实测 dsh 恒发 kind="other"，仍保留字段以便诊断（不作为分类依据）
        out.payload.insert(QStringLiteral("declaredKind"),
                           update.value(QStringLiteral("kind")).toString());
        return true;
    }
    if (variant == QLatin1String(kVariantToolCallUpdate)) {
        const QString status = update.value(QStringLiteral("status")).toString();
        out.payload.insert(QStringLiteral("toolCallId"),
                           update.value(QStringLiteral("toolCallId")).toString());
        out.payload.insert(QStringLiteral("status"), status);
        if (status == QLatin1String("completed") || status == QLatin1String("ok")) {
            out.kind = QStringLiteral("tool.done");
        } else if (status == QLatin1String("error") || status == QLatin1String("failed")) {
            out.kind = QStringLiteral("tool.error");
        } else if (status == QLatin1String("cancelled")) {
            out.kind = QStringLiteral("tool.cancelled");
        } else {
            // pending / in_progress：仍在执行
            out.kind = QStringLiteral("tool.running");
        }
        return true;
    }
    if (variant == QLatin1String(kVariantPlan)) {
        const QJsonArray entries = update.value(QStringLiteral("entries")).toArray();
        out.kind = QStringLiteral("agent.plan");
        out.payload.insert(QStringLiteral("entryCount"), entries.size());
        if (!entries.isEmpty()) {
            out.payload.insert(QStringLiteral("first"),
                               entries.first().toObject().value(QStringLiteral("content")).toString());
        }
        return true;
    }
    if (variant == QLatin1String(kVariantUsage)) {
        out.kind = QStringLiteral("agent.usage");
        out.payload.insert(QStringLiteral("used"), update.value(QStringLiteral("used")).toInt());
        out.payload.insert(QStringLiteral("size"), update.value(QStringLiteral("size")).toInt());
        return true;
    }

    // available_commands_update / current_mode_update / session_info_update 等：
    // 与「实时工作状态」无关 → 忽略（不猜测、不误判）
    out = CoreSignal();
    return false;
}

bool AcpEventMapper::mapNotification(const QJsonObject &notification, CoreSignal &out)
{
    const QJsonObject params = notification.value(QStringLiteral("params")).toObject();
    const QJsonObject update = params.value(QStringLiteral("update")).toObject();
    if (update.isEmpty()) {
        return false;
    }
    return mapUpdate(update, out);
}

} // namespace whalepet::contextapi
