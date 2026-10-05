#include "viewmodel/builtin/DialogueServicePlugin.h"

#include "viewmodel/DialogueService.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/PetController.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QDebug>

#include <memory>

namespace whalepet {

namespace {
const char *const kPluginId = "builtin.dialogue";
const char *const kCapabilityId = "service.dialogue";
} // namespace

DialogueServicePlugin::DialogueServicePlugin(QObject *host, const BuiltinServiceHooks *hooks,
                                             BuiltinServiceHandles *handles)
    : m_host(host)
    , m_hooks(hooks)
    , m_handles(handles)
{
}

plugin::PluginInfo DialogueServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("预设对话");
    info.description = QStringLiteral("主人提问 → 鲸鱼娘回答（五选一 / 敏感题每日配额）");
    return info;
}

void DialogueServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<DialogueServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("预设对话状态"),
                                        QStringLiteral("语料可用性 / 敏感题配额 / 天气题可用性 / 运行状态")),
        this,
        [](DialogueServicePlugin *plugin) {
            viewmodel::DialogueService *service = plugin->service();
            QJsonObject out;
            out.insert(QStringLiteral("available"), service->available());
            out.insert(QStringLiteral("questionCount"),
                       static_cast<double>(service->questionCount()));
            out.insert(QStringLiteral("running"), service->running());
            out.insert(QStringLiteral("weatherAvailable"), service->weatherAvailable());
            out.insert(QStringLiteral("sensitiveUnlocked"), service->sensitiveUnlocked());
            out.insert(QStringLiteral("sensitiveUsedToday"), service->sensitiveUsedToday());
            out.insert(QStringLiteral("sensitiveRemaining"), service->sensitiveRemaining());
            return out;
        },
        QStringLiteral("预设对话服务不可用（插件未启动）")));
}

bool DialogueServicePlugin::start(plugin::PluginContext &ctx)
{
    if (m_service != nullptr) {
        return true;
    }
    if (ctx.controller == nullptr) {
        qWarning() << "[DialogueServicePlugin] 缺少 PetController，跳过预设对话配置";
        return false;
    }

    // 服务本体由 PetController 构造并持有（P8 既有设计），这里只做配置注入。
    viewmodel::DialogueService *service = ctx.controller->dialogueService();
    if (service == nullptr) {
        qWarning() << "[DialogueServicePlugin] 问答服务不可用，跳过配置";
        return false;
    }

    // 敏感 / 私密题的解锁与每日配额：好感度来自养成服务（经 handles 取，避免插件间直接依赖），
    // 配额落 meta 表。affinity 在调用时读取，故不依赖插件 start 顺序。
    service->setDatabase(ctx.db);
    BuiltinServiceHandles *handles = m_handles;
    service->setAffinityProvider([handles] {
        return (handles != nullptr && handles->growth != nullptr)
                   ? handles->growth->state().affinity
                   : 0;
    });

    // 主动提醒门槛（docs/DIALOGUE.md §4）：由宿主以窄回调注入（读宿主 UI 状态）。
    if (m_hooks != nullptr && m_hooks->dialogueCanAsk) {
        service->setCanAsk(m_hooks->dialogueCanAsk);
    }

    m_service = service;
    if (m_handles != nullptr) {
        m_handles->dialogue = service;
    }
    qInfo() << "[DialogueServicePlugin] 问答服务已配置（敏感门槛 / 每日配额 / 静息门槛）";
    return true;
}

void DialogueServicePlugin::stop()
{
    if (m_handles != nullptr) {
        m_handles->dialogue = nullptr;
    }
    // 服务属 PetController，随其析构；此处不停止（启停由设置开关驱动，见 setDialogueEnabled）。
}

} // namespace whalepet
