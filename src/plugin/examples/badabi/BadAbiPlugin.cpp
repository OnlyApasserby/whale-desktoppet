#include "plugin/examples/badabi/BadAbiPlugin.h"

#include "plugin/Capability.h"

#include <memory>

namespace whalepet::plugin::examples {

namespace {

class BadAbiPlugin : public SimplePlugin {
public:
    BadAbiPlugin()
        : SimplePlugin(makeInfo())
    {
    }

    void registerCapabilities(CapabilityRegistry &registry) override
    {
        Q_UNUSED(registry); // 负例：宿主不应装载到此处
    }

private:
    static PluginInfo makeInfo()
    {
        PluginInfo info;
        info.id = QStringLiteral("ext.badabi");
        info.displayName = QStringLiteral("ABI 不匹配负例（仅测试）");
        info.description = QStringLiteral("元数据 apiVersion=99，应被宿主跳过");
        return info;
    }
};

} // namespace

// 宿主在元数据阶段即拒绝本插件，本函数不会被调用
std::unique_ptr<IPlugin> BadAbiPluginFactory::create()
{
    return std::make_unique<BadAbiPlugin>();
}

} // namespace whalepet::plugin::examples
