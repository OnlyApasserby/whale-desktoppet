#include "view/ui/StudyUiPlugin.h"

#include "view/ui/StudyPanel.h"
#include "viewmodel/StudyService.h"
#include "viewmodel/WorkService.h"

namespace whalepet::ui {

namespace {

const char *const kPluginId = "builtin.study";
const char *const kPageId = "ui.study.page";
// 页签顺序：紧跟第 1 页「干活」之后（学习页的内容正是从「干活」手动导入的；
// 绘画三页的 order 为 10 / 20 / 30，见 DrawingUiPlugin）
constexpr int kPageOrder = 5;

} // namespace

StudyUiPlugin::StudyUiPlugin(Hooks hooks)
    : m_hooks(std::move(hooks))
    , m_service(std::make_unique<viewmodel::StudyService>())
{
    // 清单变化 → 回写宿主（宿主注入引起的回声由 m_applyingHostJson 抑制）
    QObject::connect(m_service.get(), &viewmodel::StudyService::changed, m_service.get(), [this] {
        if (m_applyingHostJson) {
            return;
        }
        if (m_hooks.importedChanged) {
            m_hooks.importedChanged(m_service->toJson());
        }
    });
}

StudyUiPlugin::~StudyUiPlugin() = default;

plugin::PluginInfo StudyUiPlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("学习");
    info.description = QStringLiteral("工作台页：从「干活」手动导入功能块条目只读学习、"
                                      "导入外部代码文本、拼接整脚本（import 归位去重）");
    return info;
}

void StudyUiPlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    Q_UNUSED(registry); // 本插件只提供 UI 贡献点，不注册能力（与 DrawingUiPlugin 一致）
}

bool StudyUiPlugin::start(plugin::PluginContext &ctx)
{
    Q_UNUSED(ctx);
    // 干活内容层是只读依赖：缺失时学习页仍可用（只支持外部代码条目），不阻断启动
    if (m_hooks.workService && m_service->workService() == nullptr) {
        m_service->setWorkService(m_hooks.workService());
    }
    return true;
}

void StudyUiPlugin::stop()
{
    // 页面所有权在页签容器：stop 只做逻辑停止（与 IPlugin 语义一致）
}

QWidget *StudyUiPlugin::ensurePage(QWidget *parent) const
{
    if (m_service == nullptr) {
        return nullptr;
    }
    if (m_hooks.workService && m_service->workService() == nullptr) {
        m_service->setWorkService(m_hooks.workService()); // start 未跑到时的兜底（幂等）
    }
    if (m_page == nullptr) {
        m_page = new StudyPanel(m_service.get(), parent);
    }
    return m_page;
}

QList<plugin::PluginContribution> StudyUiPlugin::contributions(const plugin::IPluginUiHost &host) const
{
    Q_UNUSED(host);
    QList<plugin::PluginContribution> list;
    if (m_service == nullptr) {
        return list;
    }

    plugin::PluginContribution page;
    page.pluginId = QString::fromLatin1(kPluginId);
    page.id = QString::fromLatin1(kPageId);
    page.kind = plugin::ContributionKind::WorkbenchPage;
    page.label = QStringLiteral("学习");
    page.order = kPageOrder;
    // parent 由宿主提供 = 工作台页签容器；页的所有权随父控件（插件不删）
    page.createView = [this](QWidget *parent) -> QWidget * { return ensurePage(parent); };
    list.push_back(page);
    return list;
}

void StudyUiPlugin::setImportedJson(const QString &json)
{
    if (m_service == nullptr) {
        return;
    }
    m_applyingHostJson = true;
    m_service->loadFromJson(json); // 与当前一致时返回 false 且不刷新（幂等）
    m_applyingHostJson = false;
}

QString StudyUiPlugin::importedJson() const
{
    return (m_service == nullptr) ? QString() : m_service->toJson();
}

} // namespace whalepet::ui
