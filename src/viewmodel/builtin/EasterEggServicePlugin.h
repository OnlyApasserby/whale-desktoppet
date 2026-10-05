#pragma once

// P9-A：代码彩蛋的 builtin 插件（创建 + 「戳一戳」逻辑接线 + 只读状态能力）。
// 开关与目标工作区仍由宿主的设置流程注入（applyCodeEggSettings）。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

class QObject;

namespace whalepet {

namespace viewmodel {
class EasterEggService;
} // namespace viewmodel

class EasterEggServicePlugin : public plugin::IPlugin {
public:
    EasterEggServicePlugin(QObject *host, BuiltinServiceHandles *handles);

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;

    viewmodel::EasterEggService *service() const { return m_service; }

private:
    QObject *m_host = nullptr;                       // 非拥有：服务的 QObject parent
    BuiltinServiceHandles *m_handles = nullptr;      // 非拥有
    viewmodel::EasterEggService *m_service = nullptr; // 非拥有（parent = m_host）
};

} // namespace whalepet
