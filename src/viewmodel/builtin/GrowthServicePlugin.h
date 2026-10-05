#pragma once

// P9-A：养成服务的 builtin 插件（创建 + 载入 + 注入 controller + 只读状态能力）。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

class QObject;

namespace whalepet {

namespace viewmodel {
class GrowthService;
} // namespace viewmodel

class GrowthServicePlugin : public plugin::IPlugin {
public:
    GrowthServicePlugin(QObject *host, BuiltinServiceHandles *handles);

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;

    viewmodel::GrowthService *service() const { return m_service; }

private:
    QObject *m_host = nullptr;                     // 非拥有：服务的 QObject parent
    BuiltinServiceHandles *m_handles = nullptr;    // 非拥有
    viewmodel::GrowthService *m_service = nullptr; // 非拥有（parent = m_host）
};

} // namespace whalepet
