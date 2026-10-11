#include "view/ui/DrawingUiPlugin.h"

#include "view/ui/AbstractArtView.h"
#include "view/ui/DrawingBoardView.h"
#include "view/ui/LineArtView.h"

namespace whalepet::ui {

namespace {

const char *const kPluginId = "builtin.drawing";
// 三个工作台页的贡献点 id（稳定；冲突时保留先注册者）
const char *const kAbstractPageId = "ui.drawing.abstract";
const char *const kLinePageId = "ui.drawing.line";
const char *const kBoardPageId = "ui.drawing.board";

// 页签顺序：抽象画 → 线条画 → 画板（叙事上从「看她画」到「自己画」）
constexpr int kAbstractOrder = 10;
constexpr int kLineOrder = 20;
constexpr int kBoardOrder = 30;

} // namespace

plugin::PluginInfo DrawingUiPlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("绘画");
    info.description = QStringLiteral("工作台页：鲸鱼娘抽象画生成器 / 线条画生成器 / 简易画板"
                                      "（含屏幕取色与放大镜）");
    return info;
}

void DrawingUiPlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    Q_UNUSED(registry); // 本插件只提供 UI 贡献点，不注册能力（与 StatusPanelUiPlugin 一致）
}

bool DrawingUiPlugin::start(plugin::PluginContext &ctx)
{
    Q_UNUSED(ctx);
    // 页面延迟到工作台面板首次创建时按需生成（不占启动时间、不开机建窗）
    return true;
}

void DrawingUiPlugin::stop()
{
    // 页面所有权在页签容器：stop 只做逻辑停止（与 IPlugin 语义一致）
}

QWidget *DrawingUiPlugin::ensurePage(int index, QWidget *parent) const
{
    switch (index) {
    case 0:
        if (m_abstractPage == nullptr) {
            m_abstractPage = new AbstractArtView(parent);
        }
        return m_abstractPage;
    case 1:
        if (m_linePage == nullptr) {
            m_linePage = new LineArtView(parent);
        }
        return m_linePage;
    case 2:
        if (m_boardPage == nullptr) {
            m_boardPage = new DrawingBoardView(parent);
        }
        return m_boardPage;
    default:
        return nullptr;
    }
}

QList<plugin::PluginContribution> DrawingUiPlugin::contributions(const plugin::IPluginUiHost &host) const
{
    Q_UNUSED(host);

    const auto makePage = [this](const QString &id, const QString &label, int order, int index) {
        plugin::PluginContribution contribution;
        contribution.pluginId = QString::fromLatin1(kPluginId);
        contribution.id = id;
        contribution.kind = plugin::ContributionKind::WorkbenchPage;
        contribution.label = label;
        contribution.order = order;
        // parent 由宿主提供 = 工作台页签容器；页的所有权随父控件（插件不删）
        contribution.createView = [this, index](QWidget *parent) -> QWidget * {
            return ensurePage(index, parent);
        };
        return contribution;
    };

    QList<plugin::PluginContribution> list;
    list.append(makePage(QString::fromLatin1(kAbstractPageId), QStringLiteral("抽象画"), kAbstractOrder, 0));
    list.append(makePage(QString::fromLatin1(kLinePageId), QStringLiteral("线条画"), kLineOrder, 1));
    list.append(makePage(QString::fromLatin1(kBoardPageId), QStringLiteral("画板"), kBoardOrder, 2));
    return list;
}

} // namespace whalepet::ui
