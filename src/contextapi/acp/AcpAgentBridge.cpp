#include "contextapi/acp/AcpAgentBridge.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace whalepet::contextapi {

AcpAgentBridge::AcpAgentBridge(QString eventLogPath)
    : m_eventLogPath(std::move(eventLogPath))
{
}

bool AcpAgentBridge::start(const QString &agentId)
{
    if (agentId.isEmpty()) {
        qWarning() << "[AcpAgentBridge] 拒绝建立会话：agentId 为空";
        return false;
    }
    if (!m_sessions.contains(agentId)) {
        m_sessions.append(agentId);
        qInfo() << "[AcpAgentBridge] 会话已建立:" << agentId;
    }
    return true; // 幂等
}

void AcpAgentBridge::stop(const QString &agentId)
{
    if (m_sessions.removeAll(agentId) > 0) {
        qInfo() << "[AcpAgentBridge] 会话已结束:" << agentId;
    }
    // 未建立 / 已结束：静默幂等（与接口约定一致）
}

bool AcpAgentBridge::push(const QString &agentId, const QJsonObject &event)
{
    if (agentId.isEmpty() || !m_sessions.contains(agentId)) {
        qWarning() << "[AcpAgentBridge] 推送失败：会话不存在" << agentId;
        return false;
    }
    if (m_eventLogPath.isEmpty()) {
        qWarning() << "[AcpAgentBridge] 推送失败：未配置事件日志路径";
        return false;
    }

    const QFileInfo info(m_eventLogPath);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        qWarning() << "[AcpAgentBridge] 无法创建事件日志目录:" << info.absolutePath();
        return false;
    }

    QFile file(m_eventLogPath);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[AcpAgentBridge] 无法写入事件日志:" << m_eventLogPath << file.errorString();
        return false;
    }

    QJsonObject line;
    line.insert(QStringLiteral("agentId"), agentId);
    line.insert(QStringLiteral("atMs"),
                static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    line.insert(QStringLiteral("event"), event);
    QByteArray payload = QJsonDocument(line).toJson(QJsonDocument::Compact);
    payload.append('\n');

    if (file.write(payload) < 0) {
        qWarning() << "[AcpAgentBridge] 事件写入失败:" << file.errorString();
        return false;
    }
    file.flush();
    ++m_pushed;
    return true;
}

QStringList AcpAgentBridge::sessions() const
{
    return m_sessions;
}

} // namespace whalepet::contextapi
