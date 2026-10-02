#pragma once

// Agent 会话桥接（ACP / IDE Agent 集成入口）——**P7.5 预留，仅接口，不实现协议**
// （docs/CONTEXT-API.md §6）。
//
// 职责边界：只固定「会话生命周期 + 事件推送」这一最小形状；
// 具体协议（ACP 报文、IDE 扩展握手等）留待 P7.5 落地时新增实现，不改总线与分发核心。

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace whalepet::contextapi {

class IAgentBridge {
public:
    virtual ~IAgentBridge() = default;

    // 建立会话；返回 false 表示该 agent 当前不可桥接
    virtual bool start(const QString &agentId) = 0;
    // 结束会话（幂等）
    virtual void stop(const QString &agentId) = 0;
    // 向会话推送一个事件（如上下文变化 / 工具结果）；返回 false 表示会话不存在或推送失败
    virtual bool push(const QString &agentId, const QJsonObject &event) = 0;
    // 当前活跃会话
    virtual QStringList sessions() const = 0;
};

} // namespace whalepet::contextapi
