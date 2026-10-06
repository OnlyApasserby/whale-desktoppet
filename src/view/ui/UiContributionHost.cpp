#include "view/ui/UiContributionHost.h"

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QTabWidget>
#include <QWidget>

#include <utility>

namespace whalepet::ui {

UiContributionHost::UiContributionHost(QWidget *host)
    : QObject(host)
    , m_host(host)
{
}

UiContributionHost::~UiContributionHost()
{
    // 宿主关闭：先通知插件（释放它们持有的界面资源），再断开自身
    for (std::function<void()> &callback : m_shutdownCallbacks) {
        if (callback) {
            callback();
        }
    }
    m_shutdownCallbacks.clear();
}

QObject *UiContributionHost::hostObject() const
{
    return m_host;
}

QWidget *UiContributionHost::hostWidget() const
{
    return m_host;
}

void *UiContributionHost::nativeWindowHandle() const
{
    if (m_host == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void *>(static_cast<quintptr>(m_host->winId()));
}

void UiContributionHost::addShutdownCallback(std::function<void()> callback)
{
    if (callback) {
        m_shutdownCallbacks.append(std::move(callback));
    }
}

void UiContributionHost::requestRelayout()
{
    if (m_relayoutHandler) {
        m_relayoutHandler();
    } else {
        qInfo() << "[UiContributionHost] 收到布局刷新请求，但宿主未设置重建处理器";
    }
}

bool UiContributionHost::initialLayout() const
{
    return m_initialLayout;
}

void UiContributionHost::presentPanel(QWidget *panel)
{
    if (panel == nullptr) {
        return;
    }
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void UiContributionHost::setRelayoutHandler(std::function<void()> handler)
{
    m_relayoutHandler = std::move(handler);
}

int UiContributionHost::appendToMenu(plugin::PluginRegistry &registry, plugin::ContributionKind kind,
                                     QMenu *menu, QAction *insertBefore)
{
    if (menu == nullptr) {
        return 0;
    }

    int added = 0;
    const QList<plugin::PluginContribution> contributions = registry.collectContributions(*this);
    for (const plugin::PluginContribution &contribution : contributions) {
        if (contribution.kind != kind) {
            continue;
        }

        auto *action = new QAction(contribution.label, menu);
        if (contribution.checkable) {
            action->setCheckable(true);
            if (contribution.isChecked) {
                action->setChecked(contribution.isChecked());
            }
            if (contribution.setChecked) {
                const plugin::PluginContribution captured = contribution;
                QObject::connect(action, &QAction::toggled, menu,
                                 [captured](bool on) { captured.setChecked(on); });
            }
        }

        const plugin::PluginContribution captured = contribution;
        QObject::connect(action, &QAction::triggered, menu, [this, captured] {
            if (captured.trigger) {
                captured.trigger();
                return;
            }
            if (captured.createView) {
                presentPanel(captured.createView(hostWidget()));
            }
        });

        if (insertBefore != nullptr) {
            menu->insertAction(insertBefore, action);
        } else {
            menu->addAction(action);
        }
        ++added;
    }

    m_initialLayout = false;
    return added;
}

int UiContributionHost::appendToTabs(plugin::PluginRegistry &registry, QTabWidget *tabs)
{
    if (tabs == nullptr) {
        return 0;
    }

    int added = 0;
    const QList<plugin::PluginContribution> contributions = registry.collectContributions(*this);
    for (const plugin::PluginContribution &contribution : contributions) {
        if (contribution.kind != plugin::ContributionKind::SettingsTab || !contribution.createView) {
            continue;
        }
        QWidget *page = contribution.createView(tabs);
        if (page == nullptr) {
            continue;
        }
        tabs->addTab(page, contribution.label);
        ++added;
    }

    m_initialLayout = false;
    return added;
}

} // namespace whalepet::ui
