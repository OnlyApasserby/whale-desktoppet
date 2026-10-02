#pragma once

// ACP 会话桥接的具体实现（docs/CONTEXT-API.md §6、docs/ROADMAP-P7-Fin.md P7.5）。
//
// 职责边界（与 IAgentBridge 一致）：只固定「会话生命周期 + 事件推送」这一最小形状。
// 本实现把事件推送到一个本地 **JSONL 事件日志**：桌宠每推送一个事件追加一行，
// IDE / Agent 侧（或后续的实现）自行消费——因此**不改总线与分发核心**，
// 传输细节留在这一层，替换为管道 / socket 时只是换一个 IAgentBridge 实现。
//
// 事件行格式：
//   {"agentId":"vscode","atMs":1699999999999,"event":{...}}
//
// 会话语义：
//   * start(agentId)：建立会话（幂等；空 agentId 拒绝）；
//   * stop(agentId)：结束会话（幂等）；
//   * push(agentId, event)：仅对**已建立**的会话推送；会话不存在 → false（不静默写入）。

#include "contextapi/IAgentBridge.h"

#include <QString>
#include <QStringList>

namespace whalepet::contextapi {

class AcpAgentBridge : public IAgentBridge {
public:
    // eventLogPath：事件日志文件路径；为空时 push 一律失败（明确拒绝，不静默丢弃）。
    explicit AcpAgentBridge(QString eventLogPath);

    bool start(const QString &agentId) override;
    void stop(const QString &agentId) override;
    bool push(const QString &agentId, const QJsonObject &event) override;
    QStringList sessions() const override;

    qint64 pushedCount() const { return m_pushed; }
    const QString &eventLogPath() const { return m_eventLogPath; }

private:
    QString m_eventLogPath;
    QStringList m_sessions; // 保持建立顺序（sessions() 输出稳定）
    qint64 m_pushed = 0;
};

} // namespace whalepet::contextapi
