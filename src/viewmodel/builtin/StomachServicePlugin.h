#pragma once

// P9-A：「胃袋」服务的 builtin 插件（创建 + 目录校验 + 逻辑型日志接线 + 只读状态能力）。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

class QObject;

namespace whalepet {

namespace viewmodel {
class StomachService;
} // namespace viewmodel

class StomachServicePlugin : public plugin::IPlugin {
public:
    StomachServicePlugin(QObject *host, BuiltinServiceHandles *handles);

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;

    viewmodel::StomachService *service() const { return m_service; }

private:
    QObject *m_host = nullptr;                     // 非拥有：服务的 QObject parent
    BuiltinServiceHandles *m_handles = nullptr;    // 非拥有
    viewmodel::StomachService *m_service = nullptr; // 非拥有（parent = m_host）
};

} // namespace whalepet
