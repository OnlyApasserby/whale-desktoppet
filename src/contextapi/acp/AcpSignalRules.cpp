#include "contextapi/acp/AcpSignalRules.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace whalepet::contextapi {

namespace {

struct KindRule {
    const char *kind;
    core::WorkState state;
    double confidence;
};

// 标准信号 → 工作态映射表（kind 为小写精确匹配）。
// 未列入的 kind 一律不映射（由 payload.state 显式指定仍可生效）。
const KindRule kKindRules[] = {
    { "agent.turn", core::WorkState::VibeCoding, 0.90 },
    { "agent.burst", core::WorkState::VibeCoding, 0.90 },
    { "ai.iterate", core::WorkState::VibeCoding, 0.90 },
    { "edit.burst", core::WorkState::Coding, 0.85 },
    { "file.saved", core::WorkState::Coding, 0.70 },
    { "build.start", core::WorkState::Debugging, 0.85 },
    { "debug.start", core::WorkState::Debugging, 0.85 },
    { "test.run", core::WorkState::Debugging, 0.85 },
    { "meeting.start", core::WorkState::Meeting, 0.85 },
    { "browse", core::WorkState::Browsing, 0.80 },
    { "browsing", core::WorkState::Browsing, 0.80 },
    { "game.start", core::WorkState::Game, 0.85 },
    { "idle", core::WorkState::Idle, 0.80 },
    { "afk", core::WorkState::Afk, 0.95 },
    { "session.lock", core::WorkState::Afk, 0.95 },
    { "session.locked", core::WorkState::Afk, 0.95 },

    // ---- ACP（Agent Client Protocol）实测事件 → 工作态 ----
    // 由 AcpEventMapper 从 session/update 产出；见 docs/ACP-EVAL.md §6.2。
    // 未列入的（tool.done / tool.cancelled / agent.usage）**不映射**：不改变当前工作态。
    { "agent.thought", core::WorkState::VibeCoding, 0.90 },
    { "agent.message", core::WorkState::VibeCoding, 0.85 },
    { "agent.plan", core::WorkState::VibeCoding, 0.85 },
    { "tool.read", core::WorkState::Coding, 0.70 },
    { "tool.edit", core::WorkState::Coding, 0.85 },
    { "tool.search", core::WorkState::Coding, 0.70 },
    { "tool.command", core::WorkState::Debugging, 0.85 },
    { "tool.fetch", core::WorkState::Browsing, 0.70 },
    { "tool.plan", core::WorkState::VibeCoding, 0.80 },
    { "tool.subagent", core::WorkState::VibeCoding, 0.80 },
    { "tool.other", core::WorkState::Coding, 0.65 },
    { "tool.running", core::WorkState::Coding, 0.70 },
    { "tool.error", core::WorkState::Debugging, 0.90 },
};

double clampConfidence(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

} // namespace

SignalStateMapping mapSignalToWorkState(const CoreSignal &signal)
{
    SignalStateMapping mapping;
    if (signal.kind.isEmpty()) {
        return mapping;
    }

    const QJsonObject &payload = signal.payload;

    // 1) payload.state 显式指定（IDE 直接定态，最强）
    const QString explicitState = payload.value(QStringLiteral("state")).toString();
    if (!explicitState.isEmpty()) {
        const core::WorkState state = core::workStateFromId(explicitState.toStdString());
        if (state != core::WorkState::Unknown) {
            mapping.mapped = true;
            mapping.state = state;
            mapping.confidence = 0.95;
        }
    }

    // 2) 按 kind 的标准映射表
    if (!mapping.mapped) {
        for (const KindRule &rule : kKindRules) {
            if (signal.kind == QLatin1String(rule.kind)) {
                mapping.mapped = true;
                mapping.state = rule.state;
                mapping.confidence = rule.confidence;
                break;
            }
        }
    }

    if (!mapping.mapped) {
        return mapping;
    }

    // 3) payload 覆盖 confidence / holdMs（显式告知优先）
    const QJsonValue confidence = payload.value(QStringLiteral("confidence"));
    if (confidence.isDouble()) {
        mapping.confidence = clampConfidence(confidence.toDouble());
    }
    const QJsonValue holdMs = payload.value(QStringLiteral("holdMs"));
    if (holdMs.isDouble() && holdMs.toDouble() > 0.0) {
        mapping.holdMs = static_cast<qint64>(holdMs.toDouble());
    }
    return mapping;
}

} // namespace whalepet::contextapi
