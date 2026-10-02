#include "plugin/process/McpStdioClient.h"

#include "plugin/Capability.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QTimer>

namespace whalepet::plugin {

namespace {

const char *const kHeaderSeparator = "\r\n\r\n";
const char *const kContentLength = "content-length:";
constexpr int kStopWaitMs = 1000;

} // namespace

McpStdioClient::McpStdioClient(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    // 分离通道：stdout 只承载协议帧，stderr 归子进程日志（不混入帧流）
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &McpStdioClient::onReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                onFinished(exitCode, static_cast<int>(status));
            });
}

McpStdioClient::~McpStdioClient()
{
    stop();
}

bool McpStdioClient::running() const
{
    return m_process != nullptr && m_process->state() == QProcess::Running;
}

bool McpStdioClient::start()
{
    if (m_program.isEmpty()) {
        m_lastError = QStringLiteral("未配置 program");
        return false;
    }
    if (running()) {
        return true;
    }

    m_buffer.clear();
    m_syncInbox.clear();
    m_nextId = 1;
    if (!m_workingDir.isEmpty()) {
        m_process->setWorkingDirectory(m_workingDir);
    }
    m_process->setProgram(m_program);
    m_process->setArguments(m_arguments);
    m_process->start();

    const int waitMs = (m_timeoutMs > 0) ? m_timeoutMs : 2000;
    if (!m_process->waitForStarted(waitMs)) {
        m_lastError = QStringLiteral("启动子进程失败：%1").arg(m_process->errorString());
        qWarning() << "[McpStdioClient]" << m_lastError << m_program;
        return false;
    }
    m_lastError.clear();
    qInfo() << "[McpStdioClient] 已启动外部 MCP 进程:" << m_program;
    return true;
}

void McpStdioClient::stop()
{
    if (m_process == nullptr) {
        return;
    }
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }
    m_process->terminate();
    if (!m_process->waitForFinished(kStopWaitMs)) {
        qWarning() << "[McpStdioClient] 子进程未响应 terminate，强制结束:" << m_program;
        m_process->kill();
        m_process->waitForFinished(kStopWaitMs);
    }
}

void McpStdioClient::onReadyRead()
{
    if (m_process == nullptr) {
        return;
    }
    feed(m_process->readAllStandardOutput());
}

void McpStdioClient::onFinished(int exitCode, int exitStatus)
{
    qInfo() << "[McpStdioClient] 子进程退出:" << m_program << "exitCode =" << exitCode
            << "exitStatus =" << exitStatus;
    // 唤醒同步等待（waitForMessage 会因 running() == false 判为失败）
    if (m_syncLoop != nullptr) {
        m_syncLoop->quit();
    }
    emit processExited(exitCode, exitStatus);
}

void McpStdioClient::feed(const QByteArray &data)
{
    m_buffer.append(data);

    while (true) {
        const int headerEnd = m_buffer.indexOf(kHeaderSeparator);
        if (headerEnd < 0) {
            return; // 头部未收全
        }

        int contentLength = -1;
        const QByteArray header = m_buffer.left(headerEnd);
        const QList<QByteArray> lines = header.split('\n');
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith(kContentLength)) {
                bool ok = false;
                const int value =
                    trimmed.mid(static_cast<int>(qstrlen(kContentLength))).trimmed().toInt(&ok);
                if (ok) {
                    contentLength = value;
                }
            }
        }
        if (contentLength < 0) {
            // 丢弃坏帧头，避免死循环（明确记录，不静默）
            m_buffer.remove(0, headerEnd + static_cast<int>(qstrlen(kHeaderSeparator)));
            qWarning() << "[McpStdioClient] 丢弃缺少 Content-Length 的帧头";
            continue;
        }

        const int bodyStart = headerEnd + static_cast<int>(qstrlen(kHeaderSeparator));
        if (m_buffer.size() - bodyStart < contentLength) {
            return; // 正文未收全
        }
        const QByteArray payload = m_buffer.mid(bodyStart, contentLength);
        m_buffer.remove(0, bodyStart + contentLength);
        handleFrame(payload);
    }
}

void McpStdioClient::handleFrame(const QByteArray &payload)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[McpStdioClient] 收到非法 JSON 帧:" << err.errorString();
        return;
    }
    handleMessage(doc.object());
}

void McpStdioClient::handleMessage(const QJsonObject &message)
{
    // 无 id = 通知（如 server 主动上报）
    if (!message.contains(QStringLiteral("id")) || message.value(QStringLiteral("id")).isNull()) {
        const QString method = message.value(QStringLiteral("method")).toString();
        if (!method.isEmpty()) {
            emit notificationReceived(method, message.value(QStringLiteral("params")).toObject());
        }
        return;
    }

    const qint64 id = static_cast<qint64>(message.value(QStringLiteral("id")).toDouble());

    // 同步等待中的响应：投递到收件箱并唤醒局部事件循环
    if (m_syncLoop != nullptr && m_syncWaitId == id) {
        m_syncInbox.insert(id, message);
        m_syncLoop->quit();
        return;
    }

    // 异步路径：仅对**本客户端发出且未超时**的请求回投（同步请求不在此列）
    if (!m_pendingAsync.remove(id)) {
        return;
    }
    if (message.contains(QStringLiteral("error"))) {
        const QJsonObject error = message.value(QStringLiteral("error")).toObject();
        emit requestFailed(id, error.value(QStringLiteral("code")).toInt(kRpcErrorInternal),
                           error.value(QStringLiteral("message")).toString());
        return;
    }
    emit resultReady(id, message.value(QStringLiteral("result")).toObject());
}

void McpStdioClient::writeFrame(const QJsonObject &message)
{
    if (!running()) {
        m_lastError = QStringLiteral("子进程未运行，无法写帧");
        return;
    }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.append("Content-Length: ");
    frame.append(QByteArray::number(payload.size()));
    frame.append(kHeaderSeparator);
    frame.append(payload);
    m_process->write(frame);
    m_process->waitForBytesWritten(200);
}

QJsonObject McpStdioClient::request(const QString &method, const QJsonObject &params,
                                    QString *errorOut)
{
    QJsonObject result;
    const auto fail = [&](const QString &message) {
        m_lastError = message;
        if (errorOut != nullptr) {
            *errorOut = message;
        }
        return result;
    };

    if (!running()) {
        return fail(QStringLiteral("子进程未运行"));
    }

    const qint64 id = m_nextId++;
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), static_cast<double>(id));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    writeFrame(message);

    QJsonObject response;
    QString error;
    if (!waitForMessage(id, response, error)) {
        return fail(error);
    }
    if (response.contains(QStringLiteral("error"))) {
        const QJsonObject errObj = response.value(QStringLiteral("error")).toObject();
        return fail(errObj.value(QStringLiteral("message"))
                        .toString(QStringLiteral("外部插件返回错误")));
    }
    if (errorOut != nullptr) {
        errorOut->clear();
    }
    m_lastError.clear();
    return response.value(QStringLiteral("result")).toObject();
}

qint64 McpStdioClient::requestAsync(const QString &method, const QJsonObject &params)
{
    const qint64 id = m_nextId++;
    if (!running()) {
        // 立即回投失败（不静默）：调用方在下一轮事件循环即可收到
        QTimer::singleShot(0, this, [this, id] {
            emit requestFailed(id, kRpcErrorCapabilityUnavailable, QStringLiteral("子进程未运行"));
        });
        return id;
    }
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), static_cast<double>(id));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    writeFrame(message);

    // 超时保护：到期仍未收到响应 → 以能力失败回投（不静默挂起）
    m_pendingAsync.insert(id);
    const int waitMs = (m_timeoutMs > 0) ? m_timeoutMs : 2000;
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id, timer] {
        timer->deleteLater();
        if (m_pendingAsync.remove(id)) {
            emit requestFailed(id, kRpcErrorCapabilityFailed,
                               QStringLiteral("外部插件响应超时（%1 ms）").arg(m_timeoutMs));
        }
    });
    timer->start(waitMs);
    return id;
}

void McpStdioClient::notify(const QString &method, const QJsonObject &params)
{
    if (!running()) {
        return;
    }
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    writeFrame(message);
}

bool McpStdioClient::waitForMessage(qint64 id, QJsonObject &out, QString &error)
{
    if (m_syncInbox.contains(id)) {
        out = m_syncInbox.take(id);
        return true;
    }

    const int waitMs = (m_timeoutMs > 0) ? m_timeoutMs : 2000;
    QEventLoop loop;
    m_syncLoop = &loop;
    m_syncWaitId = id;

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(waitMs);

    loop.exec();
    timer.stop();

    m_syncLoop = nullptr;
    m_syncWaitId = -1;

    if (m_syncInbox.contains(id)) {
        out = m_syncInbox.take(id);
        return true;
    }
    if (!running()) {
        error = QStringLiteral("外部插件进程已退出");
        return false;
    }
    error = QStringLiteral("等待外部插件响应超时（%1 ms）").arg(waitMs);
    return false;
}

} // namespace whalepet::plugin
