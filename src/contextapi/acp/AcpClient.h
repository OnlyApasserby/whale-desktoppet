#pragma once

// ACP（Agent Client Protocol）客户端：以**子进程**方式拉起 Agent，并按 ACP v1 规范通信
// （docs/ACP-EVAL.md、docs/ROADMAP-P7-Fin.md P7.6）。
//
// 传输：**NDJSON over stdio** —— 每条消息一个紧凑 JSON 对象、以 `\n` 分隔、
// 禁止内嵌换行；Agent 从 stdin 读、向 stdout 写，stderr 才是日志。
//   ⚠️ 这与 `McpStdioClient` 的 `Content-Length` 分帧**不同**，两套分帧不可复用。
//
// 职责边界：
//   * 只做「进程生命周期 + 协议收发 + 会话方法 + 事件→信号映射」；
//   * 收到的 `session/update` 经 `AcpEventMapper` 转成 `CoreSignal` 后广播
//     （下游「信号 → 工作态」由既有 `AcpSignalRules` / `WorkStateService` 负责）；
//   * 不碰界面；不主动发起 prompt（桌宠是「陪伴者」，是否驱动由调用方决定）。
//
// 合规：依据 ACP v1 官方规范**自行实现**，未引入任何第三方代码
// （官方 Rust/TypeScript SDK 为 Apache-2.0，且**无 C++ 绑定**，见 `ACP-EVAL.md` §5）。

#include "contextapi/ISignalSource.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QEventLoop;
class QProcess;

namespace whalepet::contextapi {

class AcpClient : public QObject {
    Q_OBJECT
public:
    explicit AcpClient(QObject *parent = nullptr);
    ~AcpClient() override;

    // ---- 启动配置 ----
    void setProgram(const QString &program);       // 可执行文件（通常是 node）
    void setArguments(const QStringList &arguments); // 例如 { "<dsh>/lib/bin.js", "--profile", "acp" }
    void setWorkingDirectory(const QString &dir);
    void setTimeoutMs(int timeoutMs);
    // Agent 请求权限时是否自动「允许一次」。默认 true（本地自动化场景）；
    // 置 false 则一律回 cancelled（不阻塞、可观测）。
    void setAutoApprovePermissions(bool on) { m_autoApprovePermissions = on; }
    bool autoApprovePermissions() const { return m_autoApprovePermissions; }

    // ---- 生命周期 ----
    bool start(); // 启动子进程 + `initialize` 握手
    void stop();  // 关闭 stdin 并终止子进程（避免孤儿）
    bool running() const;

    // ---- initialize 结果（诊断）----
    int protocolVersion() const { return m_protocolVersion; }
    QString agentName() const { return m_agentName; }
    QJsonObject agentCapabilities() const { return m_agentCapabilities; }
    bool supportsSessionList() const;
    bool supportsSessionResume() const;

    // ---- 会话 ----
    QString sessionId() const { return m_sessionId; }
    bool haveSession() const { return !m_sessionId.isEmpty(); }

    // `session/new`（同步，带超时）——成功即成为当前会话
    bool newSession(const QString &cwd);
    // `session/list`（同步）——返回可恢复的会话 id 列表（失败返回空）
    QStringList listSessionIds();
    // `session/resume`（同步）——接管一个已持久、非活跃的会话
    bool resumeSession(const QString &sessionId, const QString &cwd);

    // `session/prompt`（**异步**）：结果经 `promptSettled(stopReason)` 回投
    bool prompt(const QString &text);
    // `session/cancel`（通知）
    void cancel();

    QString lastError() const { return m_lastError; }
    qint64 updateCount() const { return m_updateCount; }
    qint64 signalCount() const { return m_signalCount; }

signals:
    // `session/update` 经 AcpEventMapper 映射后的信号（链路出口）
    void signalMapped(const whalepet::contextapi::CoreSignal &signal);
    // 原始 update（诊断；无论是否映射成功都会发出）
    void updateReceived(const QJsonObject &update);
    void promptSettled(const QString &stopReason);
    void permissionRequested(const QString &toolTitle);
    void processExited(int exitCode, int exitStatus);
    void failed(const QString &message);

private:
    void onReadyRead();
    void onFinished(int exitCode, int exitStatus);
    void feed(const QByteArray &data);
    void handleLine(const QByteArray &line);
    void handleMessage(const QJsonObject &message);
    void handleServerRequest(const QJsonObject &message);
    void handleSessionUpdate(const QJsonObject &params);

    void sendMessage(const QJsonObject &message);
    void sendResult(const QJsonValue &id, const QJsonObject &result);
    void sendError(const QJsonValue &id, int code, const QString &message);

    QJsonObject requestSync(const QString &method, const QJsonObject &params, QString &error);
    qint64 requestAsync(const QString &method, const QJsonObject &params);
    void notify(const QString &method, const QJsonObject &params);
    bool waitForResponse(qint64 id, QJsonObject &out, QString &error);
    void failPendingAsync(const QString &reason);

    QProcess *m_process = nullptr;
    QString m_program;
    QStringList m_arguments;
    QString m_workingDir;
    int m_timeoutMs = 10000;

    QByteArray m_buffer;
    qint64 m_nextId = 1;
    QString m_lastError;

    int m_protocolVersion = 0;
    QString m_agentName;
    QJsonObject m_agentCapabilities;
    QString m_sessionId;

    bool m_autoApprovePermissions = true;
    qint64 m_updateCount = 0;
    qint64 m_signalCount = 0;
    qint64 m_promptRequestId = -1;

    // 同步等待（仅在 requestSync 期间非空）
    QEventLoop *m_syncLoop = nullptr;
    qint64 m_syncWaitId = -1;
    QHash<qint64, QJsonObject> m_syncInbox;

    QSet<qint64> m_pendingAsync;
};

} // namespace whalepet::contextapi
