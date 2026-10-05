#include "viewmodel/builtin/BuiltinServicePlugins.h"

#include "viewmodel/builtin/DialogueServicePlugin.h"
#include "viewmodel/builtin/EasterEggServicePlugin.h"
#include "viewmodel/builtin/GrowthServicePlugin.h"
#include "viewmodel/builtin/RecycleBinServicePlugin.h"
#include "viewmodel/builtin/StomachServicePlugin.h"

#include <QDebug>

#include <memory>

namespace whalepet {

namespace {

// 能力 id 唯一真源（与各插件 registerCapabilities 内使用的常量必须一致；
// builtinServiceCapabilityIds() 供测试与可用性门控核对，避免漂移）。
const char *const kGrowthId = "service.growth";
const char *const kStomachId = "service.stomach";
const char *const kDialogueId = "service.dialogue";
const char *const kEasterEggId = "service.easterEgg";
const char *const kRecycleBinId = "service.recycleBin";

} // namespace

int registerBuiltinServicePlugins(plugin::PluginRegistry &registry, QObject *host,
                                  const BuiltinServiceHooks &hooks,
                                  BuiltinServiceHandles *handles)
{
    if (host == nullptr || handles == nullptr) {
        qWarning() << "[BuiltinServicePlugins] host / handles 为空，拒绝注册宿主服务插件";
        return -1;
    }

    // 注册顺序即 startAll 顺序：养成服务先于对话服务（对话的好感度来源经 handles 读取）。
    int registered = 0;
    if (registry.add(std::make_unique<GrowthServicePlugin>(host, handles))) {
        ++registered;
    }
    if (registry.add(std::make_unique<StomachServicePlugin>(host, handles))) {
        ++registered;
    }
    if (registry.add(std::make_unique<RecycleBinServicePlugin>(host, handles))) {
        ++registered;
    }
    if (registry.add(std::make_unique<EasterEggServicePlugin>(host, handles))) {
        ++registered;
    }
    if (registry.add(std::make_unique<DialogueServicePlugin>(host, &hooks, handles))) {
        ++registered;
    }

    if (registered != 5) {
        qWarning() << "[BuiltinServicePlugins] 宿主服务插件注册数异常:" << registered << "/ 5";
    }
    return registered;
}

QStringList builtinServiceCapabilityIds()
{
    return { QString::fromLatin1(kGrowthId), QString::fromLatin1(kStomachId),
             QString::fromLatin1(kDialogueId), QString::fromLatin1(kEasterEggId),
             QString::fromLatin1(kRecycleBinId) };
}

} // namespace whalepet
