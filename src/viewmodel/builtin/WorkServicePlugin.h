#pragma once

// P10 工作能力插件（builtin，P9-A 注册路径）：服务 + 面板一体（docs/ARCHITECTURE.md 附录 D.2 / D.4）。
//   - 能力 `service.work`（只读状态：开关 / 产出目录 / 时长 / 会话状态 / 蛋标记 / 最近产出）；
//   - 贡献点：右键菜单 + 托盘「工作台…」→ createView 懒建 WorkBenchPanel（**插件拥有**）；
//   - 逻辑接线 1：PetController::interactionOccurred(Poke) → WorkService::onPoke()（会话内至多一次）；
//   - 逻辑接线 2：会话开始 / 结束 → PetController::handleMenuInteraction(Work / None)
//     （常驻 work-* 工作立绘，优先级**仅低于深夜**，见 core/MenuInteraction.h）；
//   - 会话节点台词（menu.work.start / .done / .cancel / .egg）经 presentGame 播报，
//     深夜一律不播报（与状态机同口径）。

#include "plugin/PluginInterface.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

#include <QString>

class QObject;
class QWidget;

namespace whalepet {

class PetController;

namespace viewmodel {
class WorkService;
}

namespace ui {
class WorkBenchPanel;
}

class WorkServicePlugin : public plugin::IPlugin {
public:
    WorkServicePlugin(QObject *host, BuiltinServiceHandles *handles,
                      const BuiltinServiceHooks *hooks);

    ~WorkServicePlugin() override;

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;
    QList<plugin::PluginContribution> contributions(const plugin::IPluginUiHost &host) const override;

    // 当前服务实例（未 start / 已 stop 时为 nullptr）；供只读状态能力与测试诊断
    viewmodel::WorkService *service() const { return m_service; }

private:
    QWidget *ensurePanel() const;
    void setInteractionActive(bool active);
    void speakSessionLine(const char *pose, const char *scene);

    QObject *m_host = nullptr;                   // 非拥有：服务的 QObject parent
    BuiltinServiceHandles *m_handles = nullptr;  // 非拥有
    const BuiltinServiceHooks *m_hooks = nullptr; // 非拥有（取 workOutputDirChosen）
    viewmodel::WorkService *m_service = nullptr;  // 非拥有（parent = m_host）
    PetController *m_controller = nullptr;        // 非拥有（PluginContext 注入）
    mutable ui::WorkBenchPanel *m_panel = nullptr; // 拥有（顶层窗口，无 QObject 父）
};

} // namespace whalepet
