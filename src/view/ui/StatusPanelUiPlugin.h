#pragma once

// P9-C 试点：把「状态面板」迁移为 UI 面板型插件。
//
// 与 P9-A 的边界保持一致：
//   * 插件**创建并拥有** StatusPanel 视图，经贡献点注册右键 / 托盘「状态」入口；
//   * 面板刷新所需的宿主数据经**窄回调**注入（Hooks，与 BuiltinServiceHooks 同构），
//     插件不反向依赖 PetWindow，也不需要宿主为其编写业务分支；
//   * 宿主删除硬编码「状态」菜单项，改由 UiContributionHost 按贡献点通用分发。

#include "plugin/PluginInterface.h"

#include <functional>

class QWidget;

namespace whalepet {
class StatusPanel;
} // namespace whalepet

namespace whalepet::ui {

class StatusPanelUiPlugin : public plugin::IPlugin {
public:
    // 宿主注入的窄回调：只承载「把最新数据显示到面板」与「签到请求回传」，
    // 不含通用 UI 契约（后者由 IPluginUiHost 承担）。
    struct Hooks {
        std::function<void(StatusPanel *panel)> refresh; // 展示前刷新（宿主填充数据）
        std::function<void()> signIn;                    // 面板「今日签到」→ 宿主
    };

    explicit StatusPanelUiPlugin(Hooks hooks);
    ~StatusPanelUiPlugin() override;

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;
    QList<plugin::PluginContribution> contributions(const plugin::IPluginUiHost &host) const override;

    // 当前面板实例（延迟创建；未展示过时为 nullptr）；供宿主刷新与单测诊断。
    StatusPanel *panel() const { return m_panel; }

private:
    QWidget *ensurePanel() const;

    Hooks m_hooks;
    mutable StatusPanel *m_panel = nullptr; // 拥有（顶层窗口，无 QObject 父）
};

} // namespace whalepet::ui
