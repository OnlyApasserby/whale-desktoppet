#pragma once

// P9-A：预设对话的 builtin 插件。
//
// 与其它服务插件的差异：`DialogueService` 由 `PetController` 构造并持有（与 ChatService 同构），
// 因此本插件**不拥有**服务，只负责把它「认领并配置」——
// 敏感题配额（Database）/ 好感度来源（养成服务）/ 静息门槛（宿主窄回调）。
// 提问面板（DialoguePanel）与其信号连接属 **UI**，仍由宿主负责。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

class QObject;

namespace whalepet {

namespace viewmodel {
class DialogueService;
} // namespace viewmodel

class DialogueServicePlugin : public plugin::IPlugin {
public:
    DialogueServicePlugin(QObject *host, const BuiltinServiceHooks *hooks,
                          BuiltinServiceHandles *handles);

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;

    viewmodel::DialogueService *service() const { return m_service; }

private:
    QObject *m_host = nullptr;                      // 非拥有（由 PetController 持有服务）
    const BuiltinServiceHooks *m_hooks = nullptr;   // 非拥有：宿主回调
    BuiltinServiceHandles *m_handles = nullptr;     // 非拥有
    viewmodel::DialogueService *m_service = nullptr; // 非拥有（属 PetController）
};

} // namespace whalepet
