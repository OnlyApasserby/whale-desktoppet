#include "minigame/MiniGameCompatAdapter.h"

#include "plugin/Capability.h"

#include <QDebug>
#include <QJsonObject>

#include <utility>

namespace whalepet {

namespace {

// 能力 id 前缀：`minigame.<pluginId>`（pluginId 即 IMiniGamePlugin::info().id）
const char *const kCapabilityPrefix = "minigame.";

// 小游戏元数据能力：只读，返回插件自述信息。
// 语义边界：**不**负责打开窗口——开窗需要宿主界面上下文，
// 由宿主（PetWindow::showMiniGame）驱动；见 docs/CONTEXT-API.md §3。
class MiniGameInfoCapability : public plugin::SimpleCapability {
public:
    explicit MiniGameInfoCapability(IMiniGamePlugin *game)
        : plugin::SimpleCapability(makeDescriptor(game))
        , m_game(game)
    {
    }

protected:
    bool call(const QJsonObject &in, QJsonObject &out, QJsonObject &error) override
    {
        Q_UNUSED(in);
        if (m_game == nullptr) {
            error = plugin::makeRpcError(plugin::kRpcErrorCapabilityUnavailable,
                                         QStringLiteral("小游戏插件不可用"));
            return false;
        }
        const MiniGameInfo info = m_game->info();
        out.insert(QStringLiteral("id"), info.id);
        out.insert(QStringLiteral("displayName"), info.displayName);
        out.insert(QStringLiteral("menuLabel"), info.menuLabel);
        out.insert(QStringLiteral("description"), info.description);
        return true;
    }

private:
    static plugin::CapabilityDescriptor makeDescriptor(IMiniGamePlugin *game)
    {
        plugin::CapabilityDescriptor descriptor;
        descriptor.origin = plugin::PluginOrigin::Builtin;
        descriptor.readOnly = true;
        descriptor.paramsSchema = QStringLiteral("{\"type\":\"object\",\"properties\":{}}");
        if (game != nullptr) {
            const MiniGameInfo info = game->info();
            descriptor.id = QString::fromLatin1(kCapabilityPrefix) + info.id;
            descriptor.displayName = info.displayName;
            descriptor.description = info.description.isEmpty()
                                         ? QStringLiteral("小游戏插件元数据（窗口由宿主界面打开）")
                                         : info.description;
        }
        return descriptor;
    }

    IMiniGamePlugin *m_game = nullptr; // 非拥有
};

} // namespace

std::unique_ptr<plugin::ICapability> makeMiniGameInfoCapability(IMiniGamePlugin *game)
{
    if (game == nullptr) {
        return nullptr;
    }
    return std::make_unique<MiniGameInfoCapability>(game);
}

MiniGamePluginAdapter::MiniGamePluginAdapter(IMiniGamePlugin *game)
    : m_game(game)
{
}

plugin::PluginInfo MiniGamePluginAdapter::info() const
{
    plugin::PluginInfo info;
    if (m_game == nullptr) {
        return info;
    }
    const MiniGameInfo game = m_game->info();
    info.id = QString::fromLatin1(kCapabilityPrefix) + game.id;
    info.displayName = game.displayName;
    info.description = game.description.isEmpty() ? QStringLiteral("小游戏插件") : game.description;
    info.menuLabel = game.menuLabel;
    info.version = QStringLiteral("1.0");
    return info;
}

void MiniGamePluginAdapter::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(makeMiniGameInfoCapability(m_game));
}

int registerMiniGamePlugins(const MiniGameRegistry &minigames, plugin::PluginRegistry &registry)
{
    int registered = 0;
    for (int i = 0; i < minigames.count(); ++i) {
        IMiniGamePlugin *game = minigames.at(i);
        if (game == nullptr) {
            continue;
        }
        if (registry.add(std::make_unique<MiniGamePluginAdapter>(game))) {
            ++registered;
        }
    }
    if (registered != minigames.count()) {
        qWarning() << "[MiniGameCompatAdapter] 适配注册数与已注册小游戏数不一致:"
                   << registered << "/" << minigames.count();
    }
    return registered;
}

} // namespace whalepet
