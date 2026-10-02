#include "plugin/builtin/BuiltinPluginLoader.h"

#include <QDebug>

#include <utility>

namespace whalepet::plugin {

void BuiltinPluginLoader::addRegisterFn(BuiltinRegisterFn fn)
{
    if (fn) {
        m_fns.push_back(std::move(fn));
    }
}

int BuiltinPluginLoader::load(PluginRegistry &registry) const
{
    int total = 0;
    for (std::size_t i = 0; i < m_fns.size(); ++i) {
        const BuiltinRegisterFn &fn = m_fns[i];
        if (!fn) {
            continue;
        }
        const int registered = fn(registry);
        if (registered < 0) {
            qWarning() << "[BuiltinPluginLoader] 内置注册函数失败，已跳过, index =" << i;
            continue;
        }
        total += registered;
    }
    qInfo() << "[BuiltinPluginLoader] 内置插件注册完成, 插件数 =" << registry.count()
            << "能力数 =" << registry.capabilities().count();
    return total;
}

} // namespace whalepet::plugin
