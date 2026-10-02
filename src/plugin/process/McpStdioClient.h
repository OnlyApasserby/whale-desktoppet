#pragma once

// 外部 MCP server 的 stdio 客户端（docs/PLUGIN-ARCHITECTURE.md §4.2、docs/ROADMAP-P7.md P7.4）。
//
// 以 `QProcess` 拉起一个独立进程（MCP server），用 **Content-Length 分帧的 JSON-RPC**
// 与其通信（MCP 标准 stdio 传输）。本类只做「启动 / 分帧收发 / 请求应答配对 / 超时」，
// 不含任何业务语义——握手、能力发现与转发在 McpPluginSession / ProcessPluginLoader。
//
// 两个通道：
//   * request()      ：**同步**（带超时，内部跑局部事件循环）——用于启动期握手 / tools/list；
//   * requestAsync() ：**异步**——用于 tools/call 转发，**不阻塞 GUI 线程**（结果经信号回投）。
//
// 崩溃隔离：子进程退出只发 processExited 信号，调用方据此把该来源的能力标记为不可用；
// 本类不抛异常、不终止主进程。

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QProcess;
class QEventLoop;

namespace whalepet::plugin {

class McpStdioClient : public QObject {
    Q_OBJECT
public:
    explicit McpStdioClient(QObject *parent = nullptr);
    ~McpStdioClient() override;

    void setProgram(const QString &program) { m_program = program; }
    void setArguments(const QStringList &arguments) { m_arguments = arguments; }
    void setWorkingDirectory(const QString &dir) { m_workingDir = dir; }
    void setTimeoutMs(int timeoutMs) { m_timeoutMs = timeoutMs; }
    int timeoutMs() const { return m_timeoutMs; }

    // 启动子进程（等待 started），成功返回 true
    bool start();

    // 终止子进程（先 terminate，超时后 kill）；可重复调用
    void stop();

    bool running() const;

    // 同步请求：成功返回响应 result；失败返回空对象并（可选）写入 errorOut
    QJsonObject request(const QString &method, const QJsonObject &params, QString *errorOut = nullptr);

    // 异步请求：返回请求 id（结果经 resultReady / requestFailed 回投）
    qint64 requestAsync(const QString &method, const QJsonObject &params);

    // 发送通知（无 id，不期待响应）
    void notify(const QString &method, const QJsonObject &params);

    QString lastError() const { return m_lastError; }

signals:
    void notificationReceived(const QString &method, const QJsonObject &params);
    void resultReady(qint64 id, const QJsonObject &result);
    void requestFailed(qint64 id, int code, const QString &message);
    // QProcess::ExitStatus 以 int 传出（避免头文件暴露 Qt 枚举）
    void processExited(int exitCode, int exitStatus);

private:
    void onReadyRead();
    void onFinished(int exitCode, int exitStatus);
    void handleMessage(const QJsonObject &message);
    void handleFrame(const QByteArray &payload);
    void feed(const QByteArray &data);
    void writeFrame(const QJsonObject &message);
    bool waitForMessage(qint64 id, QJsonObject &out, QString &error);

    QProcess *m_process = nullptr;
    QString m_program;
    QStringList m_arguments;
    QString m_workingDir;
    int m_timeoutMs = 2000;

    QByteArray m_buffer;
    qint64 m_nextId = 1;
    QString m_lastError;

    // 同步等待（仅在 request() 期间非空）
    QEventLoop *m_syncLoop = nullptr;
    qint64 m_syncWaitId = -1;
    QHash<qint64, QJsonObject> m_syncInbox;

    // 未完成的异步请求（做超时保护；响应到达或超时后移除）
    QSet<qint64> m_pendingAsync;
};

} // namespace whalepet::plugin
