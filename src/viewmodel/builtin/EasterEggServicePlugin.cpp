#include "viewmodel/builtin/EasterEggServicePlugin.h"

#include "core/PetTypes.h"
#include "viewmodel/EasterEggService.h"
#include "viewmodel/PetController.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QDebug>

#include <memory>

namespace whalepet {

namespace {
const char *const kPluginId = "builtin.easterEgg";
const char *const kCapabilityId = "service.easterEgg";
} // namespace

EasterEggServicePlugin::EasterEggServicePlugin(QObject *host, BuiltinServiceHandles *handles)
    : m_host(host)
    , m_handles(handles)
{
}

plugin::PluginInfo EasterEggServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("代码彩蛋");
    info.description = QStringLiteral("戳一戳 5% 概率在用户工作区源码注释里藏一句俏皮话");
    return info;
}

void EasterEggServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<EasterEggServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("代码彩蛋状态"),
                                        QStringLiteral("是否启用 / 目标工作区 / 最近藏话的文件")),
        this,
        [](EasterEggServicePlugin *plugin) {
            viewmodel::EasterEggService *service = plugin->service();
            QJsonObject out;
            out.insert(QStringLiteral("enabled"), service->enabled());
            out.insert(QStringLiteral("workspace"), service->workspace());
            out.insert(QStringLiteral("lastFile"), service->lastFile());
            out.insert(QStringLiteral("triggerProbability"),
                       viewmodel::EasterEggService::kTriggerProbability);
            return out;
        },
        QStringLiteral("代码彩蛋服务不可用（插件未启动）")));
}

bool EasterEggServicePlugin::start(plugin::PluginContext &ctx)
{
    if (m_service != nullptr) {
        return true;
    }
    if (m_host == nullptr) {
        qWarning() << "[EasterEggServicePlugin] 缺少宿主窗口，跳过代码彩蛋服务";
        return false;
    }

    m_service = new viewmodel::EasterEggService(m_host);

    // 逻辑型接线：「戳一戳」→ Poke 时掷一次 5%（服务内部再校验「未启用 / 工作区为空」）。
    if (ctx.controller != nullptr) {
        QObject::connect(ctx.controller, &PetController::interactionOccurred, m_service,
                         [this](core::Interaction type, qint64) {
                             if (type == core::Interaction::Poke && m_service != nullptr) {
                                 m_service->poke();
                             }
                         });
    }

    if (m_handles != nullptr) {
        m_handles->easterEgg = m_service;
    }
    return true;
}

void EasterEggServicePlugin::stop()
{
    if (m_handles != nullptr) {
        m_handles->easterEgg = nullptr;
    }
    // 无定时器 / 无外部资源：无需额外停止动作。
}

} // namespace whalepet
