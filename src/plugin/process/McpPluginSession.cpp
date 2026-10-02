#include "plugin/process/McpPluginSession.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <utility>

namespace whalepet::plugin {

namespace {

// MCP 协议版本（initialize 协商用；与 StdioTransport 的 server 侧口径一致）
const char *const kMcpProtocolVersion = "2026-01-01";
const char *const kClientName = "whalepet";
const char *const kClientVersion = "0.2.0";

// 单个 tool 对应的能力（异步转发到外部进程）
class ProcessCapability : public ICapability {
public:
    ProcessCapability(McpPluginSession *session, McpPluginSession::ToolInfo tool)
        : m_session(session)
        , m_tool(std::move(tool))
    {
    }

    CapabilityDescriptor descriptor() const override
    {
        CapabilityDescriptor descriptor;
        descriptor.id = m_tool.capabilityId;
        descriptor.displayName = m_tool.name;
        descriptor.description = m_tool.description;
        descriptor.origin = PluginOrigin::Process;
        descriptor.readOnly = false; // 外部工具可能产生副作用（由外部插件自行约束）
        descriptor.paramsSchema = m_tool.inputSchema;
        return descriptor;
    }

    bool invoke(const QJsonObject &in, InvokeContext &ctx, QJsonObject &out,
                QJsonObject &error) override
    {
        Q_UNUSED(out);
        if (m_session == nullptr || !m_session->running()) {
            error = makeRpcError(kRpcErrorCapabilityUnavailable,
                                 QStringLiteral("外部插件进程未运行：%1").arg(m_tool.capabilityId));
            return true; // 同步失败
        }
        // 异步：取走回调，结果经 responder 回投（不阻塞 GUI 线程）
        InvokeContext::Responder responder = ctx.takeResponder();
        m_session->submitCall(m_tool, in, std::move(responder));
        return false;
    }

private:
    McpPluginSession *m_session = nullptr;
    McpPluginSession::ToolInfo m_tool;
};

} // namespace

McpPluginSession::McpPluginSession(const ProcessServerSpec &spec, QObject *parent)
    : QObject(parent)
    , m_spec(spec)
{
}

McpPluginSession::~McpPluginSession()
{
    stop();
}

bool McpPluginSession::running() const
{
    return m_started && m_client != nullptr && m_client->running();
}

bool McpPluginSession::start()
{
    if (m_started) {
        return true;
    }

    m_client = std::make_unique<McpStdioClient>();
    m_client->setProgram(m_spec.program);
    m_client->setArguments(m_spec.arguments);
    m_client->setTimeoutMs(m_spec.timeoutMs);
    connect(m_client.get(), &McpStdioClient::resultReady, this, &McpPluginSession::onResult);
    connect(m_client.get(), &McpStdioClient::requestFailed, this, &McpPluginSession::onFailed);
    connect(m_client.get(), &McpStdioClient::processExited, this,
            &McpPluginSession::onProcessExited);

    if (!m_client->start()) {
        qWarning() << "[McpPluginSession] 外部插件启动失败:" << m_spec.pluginId;
        m_client.reset();
        return false;
    }

    // ---- initialize 握手 ----
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QLatin1String(kClientName));
    clientInfo.insert(QStringLiteral("version"), QLatin1String(kClientVersion));
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QLatin1String(kMcpProtocolVersion));
    initParams.insert(QStringLiteral("clientInfo"), clientInfo);

    QString error;
    m_client->request(QStringLiteral("initialize"), initParams, &error);
    if (!error.isEmpty()) {
        qWarning() << "[McpPluginSession] initialize 失败:" << m_spec.pluginId << error;
        m_client->stop();
        m_client.reset();
        return false;
    }
    m_client->notify(QStringLiteral("notifications/initialized"), QJsonObject());

    // ---- tools/list 能力发现 ----
    const QJsonObject listResult = m_client->request(QStringLiteral("tools/list"), QJsonObject(), &error);
    if (!error.isEmpty()) {
        qWarning() << "[McpPluginSession] tools/list 失败:" << m_spec.pluginId << error;
        m_client->stop();
        m_client.reset();
        return false;
    }

    m_tools.clear();
    const QJsonArray tools = listResult.value(QStringLiteral("tools")).toArray();
    for (const QJsonValue &value : tools) {
        const QJsonObject tool = value.toObject();
        const QString name = tool.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            qWarning() << "[McpPluginSession] 跳过缺少 name 的 tool:" << m_spec.pluginId;
            continue;
        }
        ToolInfo info;
        info.name = name;
        info.capabilityId = capabilityIdFor(name);
        info.description = tool.value(QStringLiteral("description")).toString();
        const QJsonValue schema = tool.value(QStringLiteral("inputSchema"));
        if (schema.isObject()) {
            info.inputSchema =
                QString::fromUtf8(QJsonDocument(schema.toObject()).toJson(QJsonDocument::Compact));
        }
        m_tools.push_back(std::move(info));
    }

    m_started = true;
    qInfo() << "[McpPluginSession] 外部插件已接入:" << m_spec.pluginId
            << "发现能力数 =" << static_cast<int>(m_tools.size());
    return true;
}

void McpPluginSession::stop()
{
    if (m_client != nullptr) {
        m_client->stop(); // 触发 onProcessExited → failAllPending + availability=false
        m_client.reset();
    }
    m_started = false;
}

int McpPluginSession::registerCapabilities(CapabilityRegistry &registry)
{
    int count = 0;
    for (const ToolInfo &tool : m_tools) {
        if (registry.add(std::make_unique<ProcessCapability>(this, tool))) {
            ++count;
        }
    }
    return count;
}

void McpPluginSession::submitCall(const ToolInfo &tool, const QJsonObject &arguments,
                                  InvokeContext::Responder responder)
{
    if (m_client == nullptr) {
        InvokeContext ctx(std::move(responder));
        ctx.fail(kRpcErrorCapabilityUnavailable, QStringLiteral("外部插件会话不可用"));
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("name"), tool.name);
    params.insert(QStringLiteral("arguments"), arguments);
    const qint64 id = m_client->requestAsync(QStringLiteral("tools/call"), params);
    m_pending.insert(id, std::move(responder));
}

void McpPluginSession::onResult(qint64 id, const QJsonObject &result)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return; // 非本会话请求 / 已随进程退出清理
    }
    InvokeContext::Responder responder = it.value();
    m_pending.erase(it);
    if (!responder) {
        return;
    }
    InvokeContext ctx(responder);
    ctx.respond(result);
}

void McpPluginSession::onFailed(qint64 id, int code, const QString &message)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    InvokeContext::Responder responder = it.value();
    m_pending.erase(it);
    if (!responder) {
        return;
    }
    InvokeContext ctx(responder);
    ctx.fail(code, message);
}

void McpPluginSession::onProcessExited(int exitCode, int exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    failAllPending(kRpcErrorCapabilityUnavailable, QStringLiteral("外部插件进程已退出"));
    for (const ToolInfo &tool : m_tools) {
        emit availabilityChanged(tool.capabilityId, false);
    }
    m_started = false;
    emit exited(m_spec.pluginId);
}

void McpPluginSession::failAllPending(int code, const QString &message)
{
    if (m_pending.isEmpty()) {
        return;
    }
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        InvokeContext::Responder responder = it.value();
        if (!responder) {
            continue;
        }
        InvokeContext ctx(responder);
        ctx.fail(code, message);
    }
    m_pending.clear();
}

QString McpPluginSession::capabilityIdFor(const QString &toolName) const
{
    return QStringLiteral("ext.%1.%2").arg(m_spec.pluginId, toolName);
}

} // namespace whalepet::plugin
