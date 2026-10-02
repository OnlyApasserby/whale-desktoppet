#include "contextapi/acp/AcpClient.h"

#include "contextapi/acp/AcpEventMapper.h"
#include "plugin/Capability.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>

namespace whalepet::contextapi {

namespace {

// ACP v1：本地 Agent 由客户端以子进程方式拉起，协议走 stdio 的 NDJSON
constexpr int kProtocolVersion = 1;
constexpr int kDefaultTimeoutMs = 10000;
constexpr int kStopWaitMs = 1000;
constexpr int kMinTimeoutMs = 500;

// JSON-RPC 错误码（客户端本地使用；与 plugin::kRpcError* 同源自 JSON-RPC 2.0）
constexpr int kErrorMethodNotFound = -32601;
constexpr int kErrorInternal = -32603;

QJsonObject makeRequest(qint64 id, const QString &method, const QJsonObject &params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), static_cast<double>(id));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    return message;
}

QJsonObject makeNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), method);
    message.insert(QStringLiteral("params"), params);
    return message;
}

} // namespace

AcpClient::AcpClient(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    // ACP 规范：stdout 只承载协议，stderr 是 Agent 日志 → 必须分离通道
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &AcpClient::onReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                onFinished(exitCode, static_cast<int>(status));
            });
}

AcpClient::~AcpClient()
{
    stop();
}

void AcpClient::setProgram(const QString &program)
{
    m_program = program;
}

void AcpClient::setArguments(const QStringList &arguments)
{
    m_arguments = arguments;
}

void AcpClient::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
}

void AcpClient::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = (timeoutMs >= kMinTimeoutMs) ? timeoutMs : kDefaultTimeoutMs;
}

bool AcpClient::running() const
{
    return m_process != nullptr && m_process->state() == QProcess::Running;
}

bool AcpClient::supportsSessionList() const
{
    const QJsonObject session = m_agentCapabilities.value(QStringLiteral("sessionCapabilities")).toObject();
    return session.contains(QStringLiteral("list"));
}

bool AcpClient::supportsSessionResume() const
{
    const QJsonObject session = m_agentCapabilities.value(QStringLiteral("sessionCapabilities")).toObject();
    return session.contains(QStringLiteral("resume"));
}

bool AcpClient::start()
{
    if (m_program.isEmpty()) {
        m_lastError = QStringLiteral("未配置 program");
        emit failed(m_lastError);
        return false;
    }
    if (running()) {
        return true;
    }

    m_buffer.clear();
    m_syncInbox.clear();
    m_pendingAsync.clear();
    m_nextId = 1;

    if (!m_workingDir.isEmpty()) {
        m_process->setWorkingDirectory(m_workingDir);
    }
    m_process->setProgram(m_program);
    m_process->setArguments(m_arguments);
    m_process->start();

    if (!m_process->waitForStarted(m_timeoutMs)) {
        m_lastError = QStringLiteral("启动 ACP Agent 失败：%1").arg(m_process->errorString());
        qWarning() << "[AcpClient]" << m_lastError;
        emit failed(m_lastError);
        return false;
    }
    qInfo() << "[AcpClient] 已启动 ACP Agent:" << m_program << m_arguments;

    // ---- initialize 握手 ----
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("whalepet"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("0.3.0"));
    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
    params.insert(QStringLiteral("clientCapabilities"), QJsonObject());
    params.insert(QStringLiteral("clientInfo"), clientInfo);

    QString error;
    const QJsonObject result = requestSync(QStringLiteral("initialize"), params, error);
    if (!error.isEmpty()) {
        m_lastError = QStringLiteral("initialize 失败：%1").arg(error);
        qWarning() << "[AcpClient]" << m_lastError;
        emit failed(m_lastError);
        stop();
        return false;
    }

    m_protocolVersion = result.value(QStringLiteral("protocolVersion")).toInt();
    m_agentName = result.value(QStringLiteral("agentInfo")).toObject().value(QStringLiteral("name")).toString();
    m_agentCapabilities = result.value(QStringLiteral("agentCapabilities")).toObject();
    m_lastError.clear();
    qInfo() << "[AcpClient] 握手完成：agent =" << m_agentName << "protocolVersion ="
            << m_protocolVersion;
    return true;
}

void AcpClient::stop()
{
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }
    // 规范：客户端关闭 stdin，然后终止子进程
    m_process->closeWriteChannel();
    m_process->terminate();
    if (!m_process->waitForFinished(kStopWaitMs)) {
        qWarning() << "[AcpClient] Agent 未响应 terminate，强制结束";
        m_process->kill();
        m_process->waitForFinished(kStopWaitMs);
    }
    m_sessionId.clear();
}

void AcpClient::onReadyRead()
{
    if (m_process == nullptr) {
        return;
    }
    feed(m_process->readAllStandardOutput());
}

void AcpClient::onFinished(int exitCode, int exitStatus)
{
    qInfo() << "[AcpClient] Agent 进程退出：exitCode =" << exitCode << "exitStatus =" << exitStatus;
    failPendingAsync(QStringLiteral("ACP Agent 进程已退出"));
    m_sessionId.clear();
    if (m_syncLoop != nullptr) {
        m_syncLoop->quit();
    }
    emit processExited(exitCode, exitStatus);
}

void AcpClient::feed(const QByteArray &data)
{
    m_buffer.append(data);

    // NDJSON：以 '\n' 分隔，逐行成帧
    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            return; // 行未收全
        }
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.trimmed().isEmpty()) {
            handleLine(line);
        }
    }
}

void AcpClient::handleLine(const QByteArray &line)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // 规范要求 stdout 只承载合法 ACP 消息；非法行明确记录（不静默）
        qWarning() << "[AcpClient] 忽略非法协议行:" << err.errorString();
        return;
    }
    handleMessage(doc.object());
}

void AcpClient::handleMessage(const QJsonObject &message)
{
    const bool hasId = message.contains(QStringLiteral("id")) && !message.value(QStringLiteral("id")).isNull();
    const QString method = message.value(QStringLiteral("method")).toString();

    // ① Agent → Client 的**请求**（有 id 且有 method）：必须应答，否则对方挂起
    if (hasId && !method.isEmpty()) {
        handleServerRequest(message);
        return;
    }

    // ② 通知（无 id 有 method）
    if (!hasId && !method.isEmpty()) {
        if (method == QLatin1String("session/update")) {
            handleSessionUpdate(message.value(QStringLiteral("params")).toObject());
        }
        return;
    }

    if (!hasId) {
        return;
    }

    // ③ 响应
    const qint64 id = static_cast<qint64>(message.value(QStringLiteral("id")).toDouble());
    if (m_syncLoop != nullptr && m_syncWaitId == id) {
        m_syncInbox.insert(id, message);
        m_syncLoop->quit();
        return;
    }
    if (!m_pendingAsync.remove(id)) {
        return; // 超时后的迟到响应：忽略
    }
    if (id == m_promptRequestId) {
        m_promptRequestId = -1;
        const QJsonObject result = message.value(QStringLiteral("result")).toObject();
        const QString stopReason = result.value(QStringLiteral("stopReason")).toString();
        qInfo() << "[AcpClient] prompt 结束，stopReason =" << stopReason;
        emit promptSettled(stopReason);
    }
}

void AcpClient::handleSessionUpdate(const QJsonObject &params)
{
    const QJsonObject update = params.value(QStringLiteral("update")).toObject();
    if (update.isEmpty()) {
        return;
    }
    ++m_updateCount;
    emit updateReceived(update);

    // 协议层 → CoreSignal（纯映射；未识别变体不发信号）
    CoreSignal signal;
    if (AcpEventMapper::mapUpdate(update, signal)) {
        ++m_signalCount;
        emit signalMapped(signal);
    }
}

void AcpClient::handleServerRequest(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));

    if (method == QLatin1String("session/request_permission")) {
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        const QString toolTitle = params.value(QStringLiteral("toolCall")).toObject()
                                      .value(QStringLiteral("title"))
                                      .toString();
        emit permissionRequested(toolTitle);

        QJsonObject outcome;
        if (m_autoApprovePermissions) {
            QString optionId;
            const QJsonArray options = params.value(QStringLiteral("options")).toArray();
            for (const QJsonValue &value : options) {
                const QJsonObject option = value.toObject();
                const QString kind = option.value(QStringLiteral("kind")).toString();
                if (kind == QLatin1String("allow_once") || kind == QLatin1String("allow_always")) {
                    optionId = option.value(QStringLiteral("optionId")).toString();
                    break;
                }
            }
            if (optionId.isEmpty() && !options.isEmpty()) {
                optionId = options.first().toObject().value(QStringLiteral("optionId")).toString();
            }
            outcome.insert(QStringLiteral("outcome"), QStringLiteral("selected"));
            outcome.insert(QStringLiteral("optionId"), optionId);
            qInfo() << "[AcpClient] 自动允许权限请求:" << toolTitle << optionId;
        } else {
            outcome.insert(QStringLiteral("outcome"), QStringLiteral("cancelled"));
            qInfo() << "[AcpClient] 拒绝权限请求（未开启自动允许）:" << toolTitle;
        }
        QJsonObject result;
        result.insert(QStringLiteral("outcome"), outcome);
        sendResult(id, result);
        return;
    }

    // 其它客户端能力（fs/* 等）本实现不提供：明确回错误，避免 Agent 挂起
    sendError(id, kErrorMethodNotFound,
              QStringLiteral("WhalePet 未实现该客户端方法：%1").arg(method));
}

void AcpClient::sendMessage(const QJsonObject &message)
{
    if (!running()) {
        m_lastError = QStringLiteral("ACP Agent 未运行，无法发送消息");
        return;
    }
    // Compact 序列化保证不含内嵌换行（NDJSON 的硬要求）
    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    m_process->write(payload);
    m_process->waitForBytesWritten(200);
}

void AcpClient::sendResult(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("result"), result);
    sendMessage(message);
}

void AcpClient::sendError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("error"), error);
    sendMessage(response);
}

QJsonObject AcpClient::requestSync(const QString &method, const QJsonObject &params, QString &error)
{
    QJsonObject result;
    if (!running()) {
        error = QStringLiteral("ACP Agent 未运行");
        return result;
    }

    const qint64 id = m_nextId++;
    sendMessage(makeRequest(id, method, params));

    QJsonObject response;
    if (!waitForResponse(id, response, error)) {
        return result;
    }
    if (response.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = response.value(QStringLiteral("error")).toObject();
        error = errorObject.value(QStringLiteral("message")).toString(QStringLiteral("Agent 返回错误"));
        return result;
    }
    return response.value(QStringLiteral("result")).toObject();
}

qint64 AcpClient::requestAsync(const QString &method, const QJsonObject &params)
{
    const qint64 id = m_nextId++;
    if (!running()) {
        QTimer::singleShot(0, this, [this, id] {
            if (m_pendingAsync.remove(id)) {
                emit failed(QStringLiteral("ACP Agent 未运行"));
            }
        });
        m_pendingAsync.insert(id);
        return id;
    }

    sendMessage(makeRequest(id, method, params));
    m_pendingAsync.insert(id);

    const int waitMs = m_timeoutMs;
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id, timer] {
        timer->deleteLater();
        if (m_pendingAsync.remove(id)) {
            qWarning() << "[AcpClient] 异步请求超时，id =" << id;
            if (id == m_promptRequestId) {
                m_promptRequestId = -1;
                emit promptSettled(QStringLiteral("timeout"));
            }
        }
    });
    timer->start(waitMs);
    return id;
}

void AcpClient::notify(const QString &method, const QJsonObject &params)
{
    sendMessage(makeNotification(method, params));
}

bool AcpClient::newSession(const QString &cwd)
{
    QJsonObject params;
    params.insert(QStringLiteral("cwd"), cwd);
    params.insert(QStringLiteral("mcpServers"), QJsonArray());

    QString error;
    const QJsonObject result = requestSync(QStringLiteral("session/new"), params, error);
    if (!error.isEmpty()) {
        m_lastError = QStringLiteral("session/new 失败：%1").arg(error);
        qWarning() << "[AcpClient]" << m_lastError;
        return false;
    }
    m_sessionId = result.value(QStringLiteral("sessionId")).toString();
    if (m_sessionId.isEmpty()) {
        m_lastError = QStringLiteral("session/new 未返回 sessionId");
        return false;
    }
    qInfo() << "[AcpClient] 已建立会话:" << m_sessionId;
    return true;
}

QStringList AcpClient::listSessionIds()
{
    QStringList ids;
    QString error;
    const QJsonObject result = requestSync(QStringLiteral("session/list"), QJsonObject(), error);
    if (!error.isEmpty()) {
        qWarning() << "[AcpClient] session/list 失败:" << error;
        return ids;
    }
    const QJsonArray sessions = result.value(QStringLiteral("sessions")).toArray();
    for (const QJsonValue &value : sessions) {
        const QString id = value.toObject().value(QStringLiteral("sessionId")).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    return ids;
}

bool AcpClient::resumeSession(const QString &sessionId, const QString &cwd)
{
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), sessionId);
    params.insert(QStringLiteral("cwd"), cwd);
    params.insert(QStringLiteral("mcpServers"), QJsonArray());

    QString error;
    requestSync(QStringLiteral("session/resume"), params, error);
    if (!error.isEmpty()) {
        m_lastError = QStringLiteral("session/resume 失败：%1").arg(error);
        qWarning() << "[AcpClient]" << m_lastError;
        return false;
    }
    m_sessionId = sessionId;
    qInfo() << "[AcpClient] 已接管会话:" << sessionId;
    return true;
}

bool AcpClient::prompt(const QString &text)
{
    if (!running()) {
        m_lastError = QStringLiteral("ACP Agent 未运行");
        return false;
    }
    if (m_sessionId.isEmpty()) {
        m_lastError = QStringLiteral("尚未建立会话");
        return false;
    }

    QJsonObject item;
    item.insert(QStringLiteral("type"), QStringLiteral("text"));
    item.insert(QStringLiteral("text"), text);
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    params.insert(QStringLiteral("prompt"), QJsonArray{ item });

    m_promptRequestId = requestAsync(QStringLiteral("session/prompt"), params);
    return true;
}

void AcpClient::cancel()
{
    if (!running() || m_sessionId.isEmpty()) {
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    notify(QStringLiteral("session/cancel"), params);
}

bool AcpClient::waitForResponse(qint64 id, QJsonObject &out, QString &error)
{
    if (m_syncInbox.contains(id)) {
        out = m_syncInbox.take(id);
        return true;
    }

    QEventLoop loop;
    m_syncLoop = &loop;
    m_syncWaitId = id;

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(m_timeoutMs);

    loop.exec();
    timer.stop();

    m_syncLoop = nullptr;
    m_syncWaitId = -1;

    if (m_syncInbox.contains(id)) {
        out = m_syncInbox.take(id);
        return true;
    }
    if (!running()) {
        error = QStringLiteral("ACP Agent 进程已退出");
        return false;
    }
    error = QStringLiteral("等待 Agent 响应超时（%1 ms）").arg(m_timeoutMs);
    return false;
}

void AcpClient::failPendingAsync(const QString &reason)
{
    if (m_pendingAsync.isEmpty()) {
        return;
    }
    m_pendingAsync.clear();
    if (m_promptRequestId >= 0) {
        m_promptRequestId = -1;
        emit promptSettled(QStringLiteral("agent-exited"));
    }
    emit failed(reason);
}

} // namespace whalepet::contextapi
