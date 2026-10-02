#pragma once

// 一个外部 MCP 插件进程的会话（docs/PLUGIN-ARCHITECTURE.md §4.2、docs/ROADMAP-P7.md P7.4）：
//   拉起子进程(stdio) → initialize 握手 → tools/list 能力发现
//   → 每个 tool 映射为 `ext.<pluginId>.<tool>` 能力（origin = Process）
//   → tools/call 异步转发；超时 / 进程退出 → 只把该来源的能力标记为不可用（崩溃隔离）。
//
// 本类**不做**「注册表生命周期」编排（那是 ProcessPluginLoader 的职责），只负责一个进程。

#include "plugin/Capability.h"
#include "plugin/process/McpStdioClient.h"
#include "plugin/process/ProcessServerSpec.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace whalepet::plugin {

class McpPluginSession : public QObject {
    Q_OBJECT
public:
    struct ToolInfo {
        QString name;         // 原始 tool 名（tools/call 的 name）
        QString capabilityId; // 对外能力 id = "ext." + pluginId + "." + name
        QString description;
        QString inputSchema;  // JSON 字符串（来自 MCP inputSchema；空 = 无参数）
    };

    explicit McpPluginSession(const ProcessServerSpec &spec, QObject *parent = nullptr);
    ~McpPluginSession() override;

    // 启动 + 握手 + 能力发现（全同步、带超时）。任一步失败 → false 并已停止子进程。
    bool start();
    void stop();
    bool running() const;

    const ProcessServerSpec &spec() const { return m_spec; }
    const std::vector<ToolInfo> &tools() const { return m_tools; }

    // 把发现的全部 tool 注册为能力（origin = Process），返回成功注册数
    int registerCapabilities(CapabilityRegistry &registry);

    // tools/call 转发（异步）：结果到达时调用 responder
    void submitCall(const ToolInfo &tool, const QJsonObject &arguments,
                    InvokeContext::Responder responder);

signals:
    // 子进程退出 / 通道不可用 → 相关能力应被标记为不可用（由 ProcessPluginLoader 落实）
    void availabilityChanged(const QString &capabilityId, bool available);
    void exited(const QString &pluginId);

private:
    void onResult(qint64 id, const QJsonObject &result);
    void onFailed(qint64 id, int code, const QString &message);
    void onProcessExited(int exitCode, int exitStatus);
    void failAllPending(int code, const QString &message);
    QString capabilityIdFor(const QString &toolName) const;

    ProcessServerSpec m_spec;
    std::unique_ptr<McpStdioClient> m_client;
    std::vector<ToolInfo> m_tools;
    QHash<qint64, InvokeContext::Responder> m_pending;
    bool m_started = false;
};

} // namespace whalepet::plugin
