#include "view/ui/StatusPanelUiPlugin.h"

#include "view/StatusPanel.h"

#include <QObject>
#include <QWidget>

#include <utility>

namespace whalepet::ui {
namespace {

const char *const kPluginId = "builtin.statusPanel";
const char *const kContextMenuId = "ui.statusPanel.contextMenu";
const char *const kTrayMenuId = "ui.statusPanel.trayMenu";

// 与迁移前硬编码位置一致：「状态」位于「回原位」之后、「日常」之前。
constexpr int kMenuOrder = 20;

} // namespace

StatusPanelUiPlugin::StatusPanelUiPlugin(Hooks hooks)
    : m_hooks(std::move(hooks))
{
}

StatusPanelUiPlugin::~StatusPanelUiPlugin()
{
    delete m_panel;
    m_panel = nullptr;
}

plugin::PluginInfo StatusPanelUiPlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("状态面板");
    info.description = QStringLiteral("UI 面板型插件（P9-C 试点）：经贡献点注册「状态」入口");
    return info;
}

void StatusPanelUiPlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    Q_UNUSED(registry); // 本插件只提供 UI 贡献点，不注册能力
}

bool StatusPanelUiPlugin::start(plugin::PluginContext &ctx)
{
    Q_UNUSED(ctx);
    // 视图延迟到首次展示（createView）时创建，避免开机即建窗
    return true;
}

void StatusPanelUiPlugin::stop()
{
    // 面板所有权归插件，随析构释放；stop 只做逻辑停止（与 IPlugin 语义一致）
}

QWidget *StatusPanelUiPlugin::ensurePanel() const
{
    if (m_panel == nullptr) {
        // 顶层工具窗口：**不设 QObject 父**，所有权归插件，避免与宿主父子关系双重释放
        m_panel = new StatusPanel(nullptr);
        if (m_hooks.signIn) {
            QObject::connect(m_panel, &StatusPanel::signInRequested, m_panel,
                             [this] {
                                 if (m_hooks.signIn) {
                                     m_hooks.signIn();
                                 }
                             });
        }
    }
    return m_panel;
}

QList<plugin::PluginContribution> StatusPanelUiPlugin::contributions(const plugin::IPluginUiHost &host) const
{
    Q_UNUSED(host);

    const auto makePanelContribution = [this](const QString &id, plugin::ContributionKind kind) {
        plugin::PluginContribution contribution;
        contribution.pluginId = QString::fromLatin1(kPluginId);
        contribution.id = id;
        contribution.kind = kind;
        contribution.label = QStringLiteral("状态");
        contribution.order = kMenuOrder;
        contribution.createView = [this](QWidget *) -> QWidget * {
            QWidget *panel = ensurePanel();
            if (panel != nullptr && m_hooks.refresh) {
                m_hooks.refresh(m_panel); // 展示前刷新为最新数据
            }
            return panel;
        };
        return contribution;
    };

    QList<plugin::PluginContribution> list;
    list.append(makePanelContribution(QString::fromLatin1(kContextMenuId),
                                      plugin::ContributionKind::ContextMenu));
    list.append(makePanelContribution(QString::fromLatin1(kTrayMenuId),
                                      plugin::ContributionKind::TrayMenu));
    return list;
}

} // namespace whalepet::ui
