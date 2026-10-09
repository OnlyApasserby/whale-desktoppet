#include "contextapi/ContextApiService.h"

#include "contextapi/JsonRpcDispatcher.h"
#include "contextapi/builtin/ContextCapabilities.h"
#include "contextapi/transport/LocalHttpTransport.h"
#include "contextapi/transport/LocalPipeTransport.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QStringList>

namespace whalepet::contextapi {

ContextApiService::ContextApiService(plugin::PluginRegistry *registry, IContextProvider *provider,
                                     QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_provider(provider)
    , m_pipeName(QString::fromLatin1(kDefaultContextPipeName))
{
}

ContextApiService::~ContextApiService()
{
    stop();
}

int ContextApiService::registerBuiltinCapabilities()
{
    if (m_registry == nullptr) {
        qWarning() << "[ContextApiService] 能力注册表不可用，无法注册上下文能力";
        return -1;
    }
    if (m_builtinRegistered) {
        return 0;
    }
    const bool ok = m_registry->add(makeContextCapabilitiesPlugin(m_provider));
    m_builtinRegistered = ok;
    if (ok) {
        qInfo() << "[ContextApiService] 上下文能力已注册（context.* / pet.status / session.stats）";
    } else {
        qWarning() << "[ContextApiService] 上下文能力注册被拒绝（id 冲突？）";
    }
    return ok ? 1 : 0;
}

void ContextApiService::setPipeName(const QString &name)
{
    if (m_pipe != nullptr) {
        m_pipe->setServerName(name);
    }
    m_pipeName = name;
}

QString ContextApiService::generateToken()
{
    // 256 bit 系统 CSPRNG（QRandomGenerator::global）→ base64url（无填充、无需转义）
    quint32 words[8] = {};
    for (quint32 &word : words) {
        word = QRandomGenerator::global()->generate();
    }
    const QByteArray raw(reinterpret_cast<const char *>(words), static_cast<int>(sizeof(words)));
    return QString::fromLatin1(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void ContextApiService::ensureDispatcher()
{
    if (m_registry == nullptr) {
        return;
    }
    registerBuiltinCapabilities();
    if (m_dispatcher == nullptr) {
        m_dispatcher = std::make_unique<JsonRpcDispatcher>(&m_registry->capabilities());
    }
}

void ContextApiService::markCapabilitiesAvailable(bool available)
{
    if (m_registry == nullptr || !m_builtinRegistered) {
        return;
    }
    plugin::CapabilityRegistry &capabilities = m_registry->capabilities();
    const QStringList ids = contextCapabilityIds();
    for (const QString &id : ids) {
        if (capabilities.contains(id)) {
            capabilities.setAvailable(id, available);
        }
    }
}

bool ContextApiService::start()
{
    if (m_registry == nullptr) {
        m_error = QStringLiteral("能力注册表不可用");
        return false;
    }
    ensureDispatcher();

    // 【幂等】组合根可能**重复**调用 start()：从持久化设置恢复时，菜单 `setChecked`
    // 会先触发一次 toggled（此时端口/令牌可能尚未写入），随后又显式应用一次。
    // 若不先收拢已有通道，第二次 start() 会因命名管道「已在监听」而判失败，进而触发
    // 下方的原子回滚——把刚起来的 HTTP 通道又关掉（见 docs/pitfalls/ex1/P-089）。
    // 故每次 start() 都从干净状态开始，保证「重复 start / 改配置后 start」结果一致。
    if (m_pipe != nullptr) {
        m_pipe->stop();
    }
    if (m_http != nullptr) {
        m_http->stop();
    }

    // ---- 安全（SECURITY-REVIEW.md #1）：HTTP 通道**必须**有非空 token ----
    // 网页可向 127.0.0.1:<port> 发跨站请求，「仅回环」不是授权机制；空 token 时
    // LocalHttpTransport::start() 会 fail closed（不监听）。此时仍启动命名管道通道
    // ——它浏览器不可达，且受 Windows 命名管道 ACL 保护，供 whalepet-mcp.exe 使用。
    const bool httpEnabled = !m_token.trimmed().isEmpty();
    if (httpEnabled) {
        if (m_http == nullptr) {
            m_http = std::make_unique<LocalHttpTransport>(m_dispatcher.get(), this);
        }
        m_http->setToken(m_token);
        if (!m_http->start(m_port)) {
            m_error = m_http->errorString();
            emit stopped();
            return false;
        }
    } else if (m_http != nullptr) {
        m_http->stop(); // 令牌被清空：立即收回已监听的端口
    }

    // P7.2：同一总开关同时控制命名管道。两通道共用 dispatcher 与能力表。
    if (m_pipe == nullptr) {
        m_pipe = std::make_unique<LocalPipeTransport>(m_dispatcher.get(), this);
        m_pipe->setServerName(m_pipeName);
    }
    m_pipe->setToken(m_token);
    if (!m_pipe->start()) {
        // 原子语义：要么两通道都监听，要么都不监听——回滚已启动的 HTTP
        m_error = m_pipe->errorString();
        if (m_http != nullptr) {
            m_http->stop();
        }
        emit stopped();
        return false;
    }

    m_error.clear();
    markCapabilitiesAvailable(true);
    if (httpEnabled) {
        emit started(m_http->port());
    } else {
        qWarning() << "[ContextApiService] 未配置 context_api_token：仅启用命名管道通道，"
                      "不监听本机 HTTP 端口（防浏览器跨站调用，见 SECURITY-REVIEW.md #1）";
        emit started(0); // 0 = 未监听 HTTP
    }
    return true;
}

void ContextApiService::stop()
{
    markCapabilitiesAvailable(false);
    const bool wasListening = running();
    if (m_pipe != nullptr) {
        m_pipe->stop();
    }
    if (m_http != nullptr) {
        m_http->stop();
    }
    if (wasListening) {
        emit stopped();
    }
}

bool ContextApiService::running() const
{
    return (m_http != nullptr && m_http->isListening())
        || (m_pipe != nullptr && m_pipe->isListening());
}

bool ContextApiService::pipeListening() const
{
    return m_pipe != nullptr && m_pipe->isListening();
}

QString ContextApiService::pipeName() const
{
    return (m_pipe != nullptr) ? m_pipe->serverName() : m_pipeName;
}

quint16 ContextApiService::httpPort() const
{
    return (m_http == nullptr) ? 0 : m_http->port();
}

QJsonObject ContextApiService::handleRequest(const QJsonObject &request)
{
    ensureDispatcher();
    if (m_dispatcher == nullptr) {
        QJsonObject error;
        error.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        error.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
        error.insert(QStringLiteral("error"),
                     plugin::makeRpcError(plugin::kRpcErrorInternal,
                                          QStringLiteral("分发核心不可用")));
        return error;
    }
    return m_dispatcher->handleSync(request);
}

plugin::CapabilityRegistry *ContextApiService::capabilities() const
{
    return (m_registry == nullptr) ? nullptr : &m_registry->capabilities();
}

} // namespace whalepet::contextapi
