#include "plugin/process/ProcessPluginLoader.h"

#include <QDebug>
#include <QSet>

namespace whalepet::plugin {

ProcessPluginLoader::ProcessPluginLoader(QObject *parent)
    : QObject(parent)
{
}

ProcessPluginLoader::~ProcessPluginLoader()
{
    stop();
}

void ProcessPluginLoader::addServer(const ProcessServerSpec &spec)
{
    m_specs.push_back(spec);
}

void ProcessPluginLoader::clear()
{
    stop();
    m_specs.clear();
    m_states.clear();
}

int ProcessPluginLoader::configure()
{
    m_states.clear();
    m_states.reserve(m_specs.size());

    QSet<QString> seen;
    int validCount = 0;

    for (const ProcessServerSpec &spec : m_specs) {
        ProcessServerState state;
        state.spec = spec;

        if (spec.pluginId.isEmpty()) {
            state.reason = QStringLiteral("pluginId 为空");
        } else if (spec.program.isEmpty()) {
            state.reason = QStringLiteral("program 为空");
        } else if (spec.timeoutMs <= 0) {
            state.reason = QStringLiteral("timeoutMs 必须为正数");
        } else if (seen.contains(spec.pluginId)) {
            state.reason = QStringLiteral("pluginId 重复");
        } else {
            state.valid = true;
            ++validCount;
            seen.insert(spec.pluginId);
        }

        if (!state.valid) {
            qWarning() << "[ProcessPluginLoader] 非法外部插件配置:" << spec.pluginId
                       << "原因:" << state.reason;
        }
        m_states.push_back(state);
    }

    qInfo() << "[ProcessPluginLoader] 外部插件配置校验完成：合法" << validCount << "/"
            << m_specs.size();
    return validCount;
}

QStringList ProcessPluginLoader::validPluginIds() const
{
    QStringList ids;
    for (const ProcessServerState &state : m_states) {
        if (state.valid) {
            ids.append(state.spec.pluginId);
        }
    }
    return ids;
}

int ProcessPluginLoader::start(CapabilityRegistry &registry)
{
    m_registry = &registry;
    configure();

    int startedCount = 0;
    for (const ProcessServerState &state : m_states) {
        if (!state.valid) {
            continue;
        }

        auto session = std::make_unique<McpPluginSession>(state.spec);
        // 进程退出 / 能力不可用 → 只操作该来源的能力（崩溃隔离）
        connect(session.get(), &McpPluginSession::availabilityChanged, this,
                [this](const QString &capabilityId, bool available) {
                    if (m_registry != nullptr && m_registry->contains(capabilityId)) {
                        m_registry->setAvailable(capabilityId, available);
                    }
                    emit capabilityAvailabilityChanged(capabilityId, available);
                });
        connect(session.get(), &McpPluginSession::exited, this,
                [this](const QString &pluginId) { emit sessionExited(pluginId); });

        if (!session->start()) {
            qWarning() << "[ProcessPluginLoader] 外部插件接入失败，已跳过:" << state.spec.pluginId;
            continue;
        }

        const int registered = session->registerCapabilities(registry);
        qInfo() << "[ProcessPluginLoader] 外部插件已接入:" << state.spec.pluginId
                << "注册能力数 =" << registered;
        ++startedCount;
        m_sessions.push_back(std::move(session));
    }

    return startedCount;
}

void ProcessPluginLoader::stop()
{
    // 正常停止也要把该来源的能力标记为不可用（能力无法从注册表移除）
    if (m_registry != nullptr) {
        for (const std::unique_ptr<McpPluginSession> &session : m_sessions) {
            if (session == nullptr) {
                continue;
            }
            for (const McpPluginSession::ToolInfo &tool : session->tools()) {
                if (m_registry->contains(tool.capabilityId)) {
                    m_registry->setAvailable(tool.capabilityId, false);
                }
            }
        }
    }

    for (std::unique_ptr<McpPluginSession> &session : m_sessions) {
        if (session != nullptr) {
            session->stop();
        }
    }
    m_sessions.clear();
}

bool ProcessPluginLoader::running() const
{
    for (const std::unique_ptr<McpPluginSession> &session : m_sessions) {
        if (session != nullptr && session->running()) {
            return true;
        }
    }
    return false;
}

QStringList ProcessPluginLoader::registeredCapabilityIds() const
{
    QStringList ids;
    for (const std::unique_ptr<McpPluginSession> &session : m_sessions) {
        if (session == nullptr) {
            continue;
        }
        for (const McpPluginSession::ToolInfo &tool : session->tools()) {
            ids.append(tool.capabilityId);
        }
    }
    return ids;
}

} // namespace whalepet::plugin
