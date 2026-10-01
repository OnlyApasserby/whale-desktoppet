#pragma once

// 小游戏插件注册表：宿主启动时装载内置插件，之后只按接口访问。
//
// 扩展方式（新增一个小游戏）：
//   1) 在 src/minigame/<game>/ 下实现 IMiniGamePlugin + MiniGameView；
//   2) 在 MiniGameRegistry.cpp 的 registerBuiltinMiniGames() 追加一行 add(...)。
// 除这一行注册外，宿主（PetWindow / SettingsDialog）与结算服务均无需改动。

#include "minigame/MiniGamePlugin.h"

#include <QString>

#include <memory>
#include <vector>

namespace whalepet {

class MiniGameRegistry {
public:
    MiniGameRegistry() = default;
    MiniGameRegistry(const MiniGameRegistry &) = delete;
    MiniGameRegistry &operator=(const MiniGameRegistry &) = delete;

    // 注册一个插件（接管所有权；空指针忽略）
    void add(std::unique_ptr<IMiniGamePlugin> plugin);

    int count() const { return static_cast<int>(m_plugins.size()); }
    IMiniGamePlugin *at(int index) const;
    IMiniGamePlugin *find(const QString &id) const;

private:
    std::vector<std::unique_ptr<IMiniGamePlugin>> m_plugins;
};

// 注册全部内置小游戏插件（新增小游戏在此追加一行）
void registerBuiltinMiniGames(MiniGameRegistry &registry);

} // namespace whalepet
