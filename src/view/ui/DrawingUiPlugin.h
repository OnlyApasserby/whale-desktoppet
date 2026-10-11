#pragma once

// P11 绘画模块的**插件外壳**（UI 面板型插件，与 P9-C 的 StatusPanelUiPlugin 同范式）。
//
// 职责与边界：
//   * 提供三个 **WorkbenchPage 贡献点**（抽象画 / 线条画 / 画板）—— 宿主按 kind 通用分发，
//     把它们追加为「工作台…」面板的页签（见 docs/DRAWING.md §2）。宿主**不认识**任何具体页面，
//     也不为绘画新增业务分支（新增绘画功能 = 新增一个贡献点）；
//   * 不注册能力：页面状态归页面自己（与 StatusPanelUiPlugin 的处理一致）；
//   * 页面所有权：`createView(parent)` 把页面交给页签容器（Qt 父子链），插件只保留只读指针，
//     因此插件析构时**不删除**页面 —— 避免与容器双重释放。

#include "plugin/PluginInterface.h"

class QWidget;

namespace whalepet::ui {

class AbstractArtView;
class DrawingBoardView;
class LineArtView;

class DrawingUiPlugin : public plugin::IPlugin {
public:
    DrawingUiPlugin() = default;
    ~DrawingUiPlugin() override = default;

    plugin::PluginInfo info() const override;
    void registerCapabilities(plugin::CapabilityRegistry &registry) override;
    bool start(plugin::PluginContext &ctx) override;
    void stop() override;
    QList<plugin::PluginContribution> contributions(const plugin::IPluginUiHost &host) const override;

    // 页面（懒建；尚未贡献过时为 nullptr）——供测试核对页数与接线
    AbstractArtView *abstractPage() const { return m_abstractPage; }
    LineArtView *linePage() const { return m_linePage; }
    DrawingBoardView *boardPage() const { return m_boardPage; }

private:
    QWidget *ensurePage(int index, QWidget *parent) const;

    mutable AbstractArtView *m_abstractPage = nullptr;
    mutable LineArtView *m_linePage = nullptr;
    mutable DrawingBoardView *m_boardPage = nullptr;
};

} // namespace whalepet::ui
