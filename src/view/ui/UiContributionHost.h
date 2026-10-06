#pragma once

// P9-C：宿主 UI 贡献点的收集与分发（实现 plugin::IPluginUiHost，G1）。
//
// 职责：
//   * 向插件提供「父窗口 / 原生句柄 / 生命周期 / 布局刷新 / 面板展示」；
//   * 从 PluginRegistry 收集 UI 贡献点（G2），按 kind 挂载到右键菜单 / 托盘 / 设置页。
//
// 边界：本类**不感知任何具体插件**（不认识状态面板 / 对话 / 小游戏），
// 只按贡献点数据驱动，因此宿主无需为新插件新增业务分支。
//
// 外观：本类不创建任何带样式的控件（菜单项 / 标签页均沿用宿主既有 QMenu / QTabWidget），
// 不自行设计样式（resources/qt-ui/default.qss 全局样式负责）。

#include "plugin/PluginRegistry.h"
#include "plugin/ui/IPluginUiHost.h"

#include <QList>
#include <QObject>

#include <functional>

class QAction;
class QMenu;
class QTabWidget;

namespace whalepet::ui {

class UiContributionHost : public QObject, public plugin::IPluginUiHost {
    Q_OBJECT
public:
    explicit UiContributionHost(QWidget *host);
    ~UiContributionHost() override;

    // ---- plugin::IPluginUiHost ----
    QObject *hostObject() const override;
    QWidget *hostWidget() const override;
    void *nativeWindowHandle() const override;
    void addShutdownCallback(std::function<void()> callback) override;
    void requestRelayout() override;
    bool initialLayout() const override;
    void presentPanel(QWidget *panel) override;

    // 宿主设置的重建处理器（requestRelayout 时调用；为空则忽略）
    void setRelayoutHandler(std::function<void()> handler);

    // 把指定 kind 的贡献点追加到菜单（按 order 升序）；insertBefore 非空时插在其前。
    // 返回追加成功的项数。createView 生成的 QAction 由 menu 托管。
    int appendToMenu(plugin::PluginRegistry &registry, plugin::ContributionKind kind, QMenu *menu,
                     QAction *insertBefore = nullptr);

    // 把 SettingsTab 贡献点追加为标签页（页由贡献点的 createView 创建）；返回追加成功的页数。
    int appendToTabs(plugin::PluginRegistry &registry, QTabWidget *tabs);

private:
    QWidget *m_host = nullptr;
    bool m_initialLayout = true;
    std::function<void()> m_relayoutHandler;
    QList<std::function<void()>> m_shutdownCallbacks;
};

} // namespace whalepet::ui
