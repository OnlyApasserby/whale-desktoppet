#include "plugin/process/ProcessPluginLoader.h"

#include <QDebug>
#include <QSet>

namespace whalepet::plugin {

void ProcessPluginLoader::addServer(const ProcessServerSpec &spec)
{
    m_specs.push_back(spec);
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
            << m_specs.size() << "（P7.4 才真正启动子进程与能力发现）";
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

} // namespace whalepet::plugin
