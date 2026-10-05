#pragma once

// P9-A：回收站提醒的 builtin 插件（创建 + 逻辑型提醒接线 + 只读状态能力）。
// 说明：托盘气泡属 **UI 反应**，仍由宿主连接 `recycleBinNotEmpty` 后处理。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

class QObject;

namespace whalepet {

namespace viewmodel {
class RecycleBinService;
} // namespace viewmodel

class RecycleBinServicePlugin : public plugin::IPlugin {
public:
    RecycleBinServicePlugin(QObject *host, BuiltinServiceHandles *handles);

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;

    viewmodel::RecycleBinService *service() const { return m_service; }

private:
    QObject *m_host = nullptr;                        // 非拥有：服务的 QObject parent
    BuiltinServiceHandles *m_handles = nullptr;       // 非拥有
    viewmodel::RecycleBinService *m_service = nullptr; // 非拥有（parent = m_host）
};

} // namespace whalepet
