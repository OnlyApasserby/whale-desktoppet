#include "viewmodel/builtin/FlickerServicePlugin.h"

#include "viewmodel/FlickerService.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QDebug>

#include <memory>

namespace whalepet {

namespace {
const char *const kPluginId = "builtin.flicker";
const char *const kCapabilityId = "service.flicker";
} // namespace

FlickerServicePlugin::FlickerServicePlugin(QObject *host, const BuiltinServiceHooks *hooks,
                                           BuiltinServiceHandles *handles)
    : m_host(host)
    , m_hooks(hooks)
    , m_handles(handles)
{
}

plugin::PluginInfo FlickerServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("随机闪现");
    info.description = QStringLiteral("以随机间隔把桌宠闪现到屏幕另一处（只读状态）");
    return info;
}

void FlickerServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<FlickerServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("闪现状态"),
                                        QStringLiteral("是否在调度 / 区间上下限 / 下次等待时长")),
        this,
        [](FlickerServicePlugin *plugin) {
            viewmodel::FlickerService *service = plugin->service();
            QJsonObject out;
            out.insert(QStringLiteral("running"), service->running());
            out.insert(QStringLiteral("minIntervalMs"), service->minIntervalMs());
            out.insert(QStringLiteral("maxIntervalMs"), service->maxIntervalMs());
            out.insert(QStringLiteral("lastDelayMs"), service->lastDelayMs());
            return out;
        },
        QStringLiteral("闪现服务不可用（插件未启动）")));
}

bool FlickerServicePlugin::start(plugin::PluginContext &ctx)
{
    Q_UNUSED(ctx);
    if (m_service != nullptr) {
        return true;
    }
    if (m_host == nullptr) {
        qWarning() << "[FlickerServicePlugin] 缺少宿主窗口，跳过随机闪现服务";
        return false;
    }

    m_service = new viewmodel::FlickerService(m_host);

    // 门槛（拖拽中 / 面板占用 / 工作忙 / 深夜 / 小游戏可见）由宿主注入：
    // 与预设对话的 dialogueCanAsk 同一口径——插件不读 UI 状态。
    if (m_hooks != nullptr && m_hooks->flickerAllowed) {
        m_service->setCanFlicker(m_hooks->flickerAllowed);
    }

    if (m_handles != nullptr) {
        m_handles->flicker = m_service;
    }
    return true;
}

void FlickerServicePlugin::stop()
{
    if (m_handles != nullptr) {
        m_handles->flicker = nullptr;
    }
    if (m_service != nullptr) {
        m_service->stop();
    }
}

} // namespace whalepet
