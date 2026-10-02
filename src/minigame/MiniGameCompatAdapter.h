#pragma once

// 小游戏插件 → 通用插件总线的兼容适配（docs/PLUGIN-ARCHITECTURE.md §7）。
//
// 兼容红线：`MiniGameRegistry` / `IMiniGamePlugin` / `MiniGameView` / `MiniGameContext`
// **一律不做改动**，既有菜单、设置页与结算链路一行不动（12 个既有测试零回归）。
// 本文件在**外层**做适配：把已注册的 IMiniGamePlugin 暴露为通用能力
// （能力 id = `minigame.<pluginId>`），从而让小游戏能力出现在 `capabilities.list` 中。
//
// 所有权：适配器**不拥有** IMiniGamePlugin，指针仍归 MiniGameRegistry 管理；
// 因此必须在 MiniGameRegistry 存活期内使用（宿主在组合根内保证这一点）。

#include "minigame/MiniGameRegistry.h"
#include "plugin/PluginRegistry.h"

#include <memory>

namespace whalepet {

// 把 minigames 中已注册的每个插件适配并注册进通用总线，返回注册成功的插件数。
int registerMiniGamePlugins(const MiniGameRegistry &minigames, plugin::PluginRegistry &registry);

// 单个适配器（供单测直接构造）
class MiniGamePluginAdapter : public plugin::IPlugin {
public:
    explicit MiniGamePluginAdapter(IMiniGamePlugin *game);
    ~MiniGamePluginAdapter() override = default;

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;

private:
    IMiniGamePlugin *m_game = nullptr; // 非拥有；可为空（此时不注册任何能力）
};

// 小游戏元数据能力（只读）：
//   - 打开游戏窗口需要宿主界面上下文，P7.3 起可经宿主能力唤起（见 docs/CONTEXT-API.md）；
//   - 因此本期该能力只提供元数据查询，不伪造「能开窗」的语义。
std::unique_ptr<plugin::ICapability> makeMiniGameInfoCapability(IMiniGamePlugin *game);

} // namespace whalepet
