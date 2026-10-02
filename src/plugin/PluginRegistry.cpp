#include "plugin/PluginRegistry.h"

#include <QDebug>

#include <utility>

namespace whalepet::plugin {

bool PluginRegistry::add(std::unique_ptr<IPlugin> plugin)
{
    if (plugin == nullptr) {
        return false;
    }
    const PluginInfo info = plugin->info();
    if (info.id.isEmpty()) {
        qWarning() << "[PluginRegistry] 拒绝注册：插件 id 为空";
        return false;
    }
    if (find(info.id) != nullptr) {
        qWarning() << "[PluginRegistry] 插件 id 重复，丢弃后注册者:" << info.id;
        return false;
    }

    // 先收集能力再接管所有权：registerCapabilities 失败不影响注册本身
    plugin->registerCapabilities(m_capabilities);

    m_plugins.push_back(std::move(plugin));
    qInfo() << "[PluginRegistry] 已注册插件:" << info.id << "能力总数:" << m_capabilities.count();
    return true;
}

int PluginRegistry::count() const
{
    return static_cast<int>(m_plugins.size());
}

const IPlugin *PluginRegistry::at(int index) const
{
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return m_plugins[static_cast<std::size_t>(index)].get();
}

const IPlugin *PluginRegistry::find(const QString &id) const
{
    for (const std::unique_ptr<IPlugin> &plugin : m_plugins) {
        if (plugin != nullptr && plugin->info().id == id) {
            return plugin.get();
        }
    }
    return nullptr;
}

int PluginRegistry::startAll(PluginContext &ctx)
{
    ctx.capabilities = &m_capabilities;
    int started = 0;
    for (const std::unique_ptr<IPlugin> &plugin : m_plugins) {
        if (plugin == nullptr) {
            continue;
        }
        if (plugin->start(ctx)) {
            plugin->setStarted(true);
            ++started;
        } else {
            qWarning() << "[PluginRegistry] 插件初始化失败，已跳过:" << plugin->info().id;
        }
    }
    return started;
}

void PluginRegistry::stopAll()
{
    // 逆序停止：后启动者先停止，避免依赖方的资源先被释放
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        IPlugin *plugin = it->get();
        if (plugin == nullptr) {
            continue;
        }
        if (plugin->started()) {
            plugin->stop();
            plugin->setStarted(false);
        }
    }
}

} // namespace whalepet::plugin
