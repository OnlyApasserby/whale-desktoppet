#pragma once

// ACP（Agent Client Protocol）`session/update` → `CoreSignal` 的**纯映射**
// （docs/ACP-EVAL.md、docs/ROADMAP-P7-Fin.md 全阶段目标：实时获取 Vibe Coding 状态）。
//
// 职责边界：
//   * 本类只把协议事件解析/归类为 `CoreSignal.kind` + payload；
//   * 「信号 → 工作态」由 `AcpSignalRules`（已有）负责；
//   * 全程**零副作用、零 I/O**，因此可脱 UI、脱子进程单测（含真实报文夹具）。
//
// 事实依据（真实 dsh ACP `@deepseek-ai/dsh-acp` 0.1.5-rc.3 实测报文，见
// `tests/fixtures/acp-real-events.json`）：
//   * `initialize` → protocolVersion=1，agentInfo=deepseek-harness-acp；
//   * `session/update` 实测变体：`agent_thought_chunk` / `agent_message_chunk` /
//     `tool_call` / `tool_call_update` / `usage_update`；
//   * `tool_call.title` = **工具名**（实测 `"read"`），`kind` 实测恒为 `"other"`，
//     故**工具细分必须依据 `title`**（与参考实现按 `data-tool` 映射同源）；
//   * `tool_call_update.status` 实测 `in_progress` → `completed`，失败为 `error`。
//
// 未知变体一律返回 false（**忽略，不猜测**），与项目「不伪造数据」口径一致。

#include "contextapi/ISignalSource.h"

#include <QJsonObject>
#include <QString>

namespace whalepet::contextapi {

class AcpEventMapper {
public:
    // 输入一个 `update` 对象（即 `params.update`）；返回 false 表示该变体不映射。
    static bool mapUpdate(const QJsonObject &update, CoreSignal &out);

    // 输入整条 `session/update` 通知（取 `params.update`）。
    static bool mapNotification(const QJsonObject &notification, CoreSignal &out);

    // 从 `agent_thought_chunk` / `agent_message_chunk` 中取文本（无则空串）。
    static QString textOf(const QJsonObject &update);

    // 工具名（`tool_call.title`）→ 细分类别：
    //   tool.read / tool.edit / tool.command / tool.search / tool.fetch /
    //   tool.plan / tool.subagent / tool.other
    static QString classifyTool(const QString &toolName);
};

} // namespace whalepet::contextapi
