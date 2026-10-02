#pragma once

// 通用插件注册表 + 能力注册表（三层插件共用的总线）。
//
// 职责边界：
//   * 本类只负责「持有插件 → 收集能力 → 生命周期广播」，**不感知任何具体能力**；
//   * 宿主（PetWindow）与传输通道只通过 capabilities() 按 id 访问能力；
//   * 内置层（进程内静态注册）由 BuiltinPluginLoader 驱动，见
//     src/plugin/builtin/BuiltinPluginLoader.h；DLL / 外部进程层各有独立装载器。
//
// 与既有小游戏插件机制的关系：MiniGameRegistry **不做任何改动**，
// 由 src/minigame/MiniGameCompatAdapter.h 提供的适配函数把既有
// IMiniGamePlugin 适配为 IPlugin 注册进本注册表（详见 docs/PLUGIN-ARCHITECTURE.md §7）。

#include "plugin/PluginInterface.h"

#include <QString>

#include <memory>
#include <vector>

namespace whalepet::plugin {

class PluginRegistry {
public:
    PluginRegistry() = default;
    ~PluginRegistry() = default;

    PluginRegistry(const PluginRegistry &) = delete;
    PluginRegistry &operator=(const PluginRegistry &) = delete;

    // 注册插件（接管所有权）：
    //   - 空指针 / id 为空 → 忽略并返回 false；
    //   - id 重复 → 丢弃后注册者并记日志（不静默），返回 false；
    //   - 成功时立即调用 registerCapabilities() 把能力并入能力表。
    bool add(std::unique_ptr<IPlugin> plugin);

    int count() const;
    const IPlugin *at(int index) const;
    const IPlugin *find(const QString &id) const;

    CapabilityRegistry &capabilities() { return m_capabilities; }
    const CapabilityRegistry &capabilities() const { return m_capabilities; }

    // 生命周期：按注册顺序 startAll（ctx.capabilities 由调用方预先指向 capabilities()）；
    // 单个插件 start 失败只记录并跳过（不阻断其余插件与开机）。
    int startAll(PluginContext &ctx);
    void stopAll();

private:
    std::vector<std::unique_ptr<IPlugin>> m_plugins;
    CapabilityRegistry m_capabilities;
};

} // namespace whalepet::plugin
