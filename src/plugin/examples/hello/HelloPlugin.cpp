#include "plugin/examples/hello/HelloPlugin.h"

#include "plugin/Capability.h"

#include <QJsonObject>

#include <memory>

namespace whalepet::plugin::examples {

namespace {

// 示例能力（同步、只读）：回显 "hello, <name>"。
// 注意 origin = Dll —— 同名能力冲突时优先级低于内置能力（见 Capability.h）。
class GreetCapability : public SimpleCapability {
public:
    GreetCapability()
        : SimpleCapability(makeDescriptor())
    {
    }

protected:
    bool call(const QJsonObject &in, QJsonObject &out, QJsonObject &error) override
    {
        Q_UNUSED(error);
        const QString name = in.value(QStringLiteral("name")).toString(QStringLiteral("world"));
        out.insert(QStringLiteral("message"), QStringLiteral("hello, ") + name);
        out.insert(QStringLiteral("from"), QStringLiteral("ext.hello"));
        return true;
    }

private:
    static CapabilityDescriptor makeDescriptor()
    {
        CapabilityDescriptor descriptor;
        descriptor.id = QStringLiteral("ext.hello.greet");
        descriptor.displayName = QStringLiteral("示例问候");
        descriptor.description = QStringLiteral("P7.3 示例插件能力：回显 hello, <name>");
        descriptor.origin = PluginOrigin::Dll;
        descriptor.readOnly = true;
        descriptor.paramsSchema =
            QStringLiteral("{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}}}");
        return descriptor;
    }
};

class HelloPlugin : public SimplePlugin {
public:
    HelloPlugin()
        : SimplePlugin(makeInfo())
    {
    }

    void registerCapabilities(CapabilityRegistry &registry) override
    {
        registry.add(std::make_unique<GreetCapability>());
    }

private:
    static PluginInfo makeInfo()
    {
        PluginInfo info;
        info.id = QStringLiteral("ext.hello");
        info.displayName = QStringLiteral("示例插件 ext.hello");
        info.description = QStringLiteral("P7.3 动态插件示例：注册 ext.hello.greet 能力");
        info.version = QStringLiteral("1.0");
        return info;
    }
};

} // namespace

std::unique_ptr<IPlugin> HelloPluginFactory::create()
{
    return std::make_unique<HelloPlugin>();
}

} // namespace whalepet::plugin::examples
