#pragma once

// EX8 学习页的**插件外壳**（UI 面板型插件，与 DrawingUiPlugin / StatusPanelUiPlugin 同范式）。
//
// 职责与边界：
//   * 提供 **一个 WorkbenchPage 贡献点**（「学习」页）——宿主按 kind 通用分发，
//     宿主**不认识**本页，也不为学习功能新增业务分支（见 docs/STUDY.md §2）；
//   * 页面所有权随页签容器（Qt 父子链），插件只保留只读指针，析构时**不删除**页面；
//   * 清单状态（已导入条目）归 `viewmodel::StudyService`；插件只负责
//     「宿主注入持久化清单（setImportedJson）」与「清单变化回写宿主（Hooks::importedChanged）」。

#include "plugin/PluginInterface.h"

#include <QString>

#include <functional>
#include <memory>

class QWidget;

namespace whalepet::viewmodel {
class StudyService;
class WorkService;
} // namespace whalepet::viewmodel

namespace whalepet::ui {

class StudyPanel;

class StudyUiPlugin : public plugin::IPlugin {
public:
    // 宿主注入的窄回调（与 BuiltinServiceHooks / StatusPanelUiPlugin::Hooks 同构）
    struct Hooks {
        // 干活内容层（功能块条目 / 代码块正文）的**只读**入口；为空 = 学习页只支持外部代码条目
        std::function<viewmodel::WorkService *()> workService;
        // 清单变化 → 宿主持久化（写设置键 `study_imported`）；为空 = 不持久化（仅本次运行期）
        std::function<void(const QString &)> importedChanged;
    };

    explicit StudyUiPlugin(Hooks hooks = {});
    ~StudyUiPlugin() override;

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;
    QList<plugin::PluginContribution> contributions(const plugin::IPluginUiHost &host) const override;

    // 宿主注入已持久化的清单（JSON 数组文本；与当前一致时不动，避免回声写库）
    void setImportedJson(const QString &json);
    QString importedJson() const;

    // 清单状态（测试 / 诊断用；插件持有）
    viewmodel::StudyService *service() const { return m_service.get(); }
    // 学习页（懒建；尚未贡献过时为 nullptr）
    StudyPanel *page() const { return m_page; }

private:
    QWidget *ensurePage(QWidget *parent) const;

    Hooks m_hooks;
    std::unique_ptr<viewmodel::StudyService> m_service; // 拥有（无 QObject 父）
    mutable StudyPanel *m_page = nullptr;               // 非拥有（页签容器持有）
    bool m_applyingHostJson = false;                    // 宿主注入期间抑制回写
};

} // namespace whalepet::ui
