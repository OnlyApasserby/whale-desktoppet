#pragma once

// EX6：随机闪现的 builtin 插件（创建 + 门槛注入 + 只读状态能力）。
// 说明：落点抽样与窗口移动属 **UI 反应**，仍由宿主连接 `flickerRequested` 后处理
//（与 RecycleBinServicePlugin 同构：插件只管调度，不碰几何）。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

class QObject;

namespace whalepet {

namespace viewmodel {
class FlickerService;
} // namespace viewmodel

class FlickerServicePlugin : public plugin::IPlugin {
public:
    FlickerServicePlugin(QObject *host, const BuiltinServiceHooks *hooks,
                         BuiltinServiceHandles *handles);

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;

    viewmodel::FlickerService *service() const { return m_service; }

private:
    QObject *m_host = nullptr;                       // 非拥有：服务的 QObject parent
    const BuiltinServiceHooks *m_hooks = nullptr;    // 非拥有
    BuiltinServiceHandles *m_handles = nullptr;      // 非拥有
    viewmodel::FlickerService *m_service = nullptr;   // 非拥有（parent = m_host）
};

} // namespace whalepet
