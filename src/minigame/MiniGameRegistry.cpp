#include "minigame/MiniGameRegistry.h"

#include "minigame/chess/ChessPlugin.h"
#include "minigame/kitten/KittenPlugin.h"
#include "minigame/minesweeper/MinesweeperPlugin.h"

namespace whalepet {

void MiniGameRegistry::add(std::unique_ptr<IMiniGamePlugin> plugin)
{
    if (plugin != nullptr) {
        m_plugins.push_back(std::move(plugin));
    }
}

IMiniGamePlugin *MiniGameRegistry::at(int index) const
{
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return m_plugins[static_cast<std::size_t>(index)].get();
}

IMiniGamePlugin *MiniGameRegistry::find(const QString &id) const
{
    for (const std::unique_ptr<IMiniGamePlugin> &plugin : m_plugins) {
        if (plugin != nullptr && plugin->info().id == id) {
            return plugin.get();
        }
    }
    return nullptr;
}

void registerBuiltinMiniGames(MiniGameRegistry &registry)
{
    // 注册顺序即菜单 / 设置页展示顺序；新增小游戏在此追加一行即可。
    registry.add(std::make_unique<MinesweeperPlugin>());
    registry.add(std::make_unique<KittenPlugin>());
    registry.add(std::make_unique<ChessPlugin>());
}

} // namespace whalepet
