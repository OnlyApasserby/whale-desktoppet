#include "contextapi/ContextApiService.h"

#include "contextapi/JsonRpcDispatcher.h"
#include "contextapi/builtin/ContextCapabilities.h"
#include "contextapi/transport/LocalHttpTransport.h"

#include <QDebug>
#include <QStringList>

namespace whalepet::contextapi {

ContextApiService::ContextApiService(plugin::PluginRegistry *registry, IContextProvider *provider,
                                     QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_provider(provider)
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

    if (m_http == nullptr) {
        m_http = std::make_unique<LocalHttpTransport>(m_dispatcher.get(), this);
    }
    m_http->setToken(m_token);
    if (!m_http->start(m_port)) {
        m_error = m_http->errorString();
        emit stopped();
        return false;
    }

    m_error.clear();
    markCapabilitiesAvailable(true);
    emit started(m_http->port());
    return true;
}

void ContextApiService::stop()
{
    markCapabilitiesAvailable(false);
    if (m_http == nullptr || !m_http->isListening()) {
        return;
    }
    m_http->stop();
    emit stopped();
}

bool ContextApiService::running() const
{
    return m_http != nullptr && m_http->isListening();
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
