#include "viewmodel/builtin/WorkServicePlugin.h"

#include "core/DaySlotRules.h"
#include "core/MenuInteraction.h"
#include "core/PetTypes.h"
#include "view/ui/WorkBenchPanel.h"
#include "viewmodel/PetController.h"
#include "viewmodel/WorkService.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QDebug>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTime>

#include <memory>
#include <utility>

namespace whalepet {

namespace {

const char *const kPluginId = "builtin.work";
const char *const kCapabilityId = "service.work";
// 会话节点台词用的立绘（全部取自既有 93 张资产，见 core/WorkPosePool.cpp 的 work-* 名单）
const char *const kStartPose = "work-ram";
const char *const kDonePose = "work-celebrate";
const char *const kCancelPose = "work-pat";
const char *const kEggPose = "work-idea";
// 菜单顺序（与既有插件的 order 同域，取中段）
constexpr int kContributionOrder = 60;

// 深夜（23:00–06:59）一律不播报会话节点台词（与状态机「菜单互动让位于深夜」同口径）
bool isLateNightNow()
{
    return core::daySlotOf(QTime::currentTime().hour()) == core::DaySlot::LateNight;
}

} // namespace

WorkServicePlugin::WorkServicePlugin(QObject *host, BuiltinServiceHandles *handles,
                                     const BuiltinServiceHooks *hooks)
    : m_host(host)
    , m_handles(handles)
    , m_hooks(hooks)
{
}

WorkServicePlugin::~WorkServicePlugin()
{
    // 面板为顶层窗口、无 QObject 父：由插件显式释放（与 StatusPanelUiPlugin 同范式）
    delete m_panel;
    m_panel = nullptr;
}

plugin::PluginInfo WorkServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("工作台");
    info.description =
        QStringLiteral("桌宠自己干活：勾选预制 Python 功能块，到时产出脚本到用户指定目录");
    return info;
}

void WorkServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<WorkServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("工作台状态"),
                                        QStringLiteral("开关 / 产出目录 / 时长 / 会话状态 / 最近产出")),
        this,
        [](WorkServicePlugin *plugin) {
            viewmodel::WorkService *service = plugin->service();
            QJsonObject out;
            out.insert(QStringLiteral("enabled"), service->enabled());
            out.insert(QStringLiteral("outputDir"), service->outputDir());
            out.insert(QStringLiteral("durationMinutes"), service->durationMinutes());
            out.insert(QStringLiteral("running"), service->running());
            out.insert(QStringLiteral("selectedBlocks"), service->selectedBlocks().join(QLatin1Char(',')));
            out.insert(QStringLiteral("eggArmed"), service->eggArmed());
            out.insert(QStringLiteral("lastOutputFile"), service->lastOutputFile());
            return out;
        },
        QStringLiteral("工作台服务不可用（插件未启动）")));
}

bool WorkServicePlugin::start(plugin::PluginContext &ctx)
{
    if (m_service != nullptr) {
        return true;
    }
    if (m_host == nullptr) {
        qWarning() << "[WorkServicePlugin] 缺少宿主窗口，跳过工作台服务";
        return false;
    }

    m_controller = ctx.controller;
    m_service = new viewmodel::WorkService(m_host);

    // 逻辑接线 1：戳一戳 → 会话内至多一次标记（产出时插俏皮话；见 core/CodeEasterEgg）
    if (m_controller != nullptr) {
        QObject::connect(m_controller, &PetController::interactionOccurred, m_service,
                         [this](core::Interaction type, qint64) {
                             if (type != core::Interaction::Poke || m_service == nullptr) {
                                 return;
                             }
                             if (m_service->onPoke()) {
                                 speakSessionLine(kEggPose, "menu.work.egg");
                             }
                         });
    }

    // 逻辑接线 2：会话状态 → 「菜单互动」通道（常驻 work-* 立绘，优先级仅低于深夜）
    QObject::connect(m_service, &viewmodel::WorkService::stateChanged, m_service, [this] {
        const bool running = (m_service != nullptr) && m_service->running();
        setInteractionActive(running);
        if (running) {
            speakSessionLine(kStartPose, "menu.work.start");
        }
    });
    QObject::connect(m_service, &viewmodel::WorkService::finished, m_service,
                     [this](const QString &, bool) { speakSessionLine(kDonePose, "menu.work.done"); });

    if (m_handles != nullptr) {
        m_handles->work = m_service;
    }
    return true;
}

void WorkServicePlugin::stop()
{
    if (m_service != nullptr) {
        // 取消进行中的会话（**不产出**），并把通道降级为 None
        m_service->cancel();
    }
    setInteractionActive(false);
    if (m_handles != nullptr) {
        m_handles->work = nullptr;
    }
}

void WorkServicePlugin::setInteractionActive(bool active)
{
    if (m_controller == nullptr) {
        return;
    }
    m_controller->handleMenuInteraction(active ? core::MenuInteraction::Work
                                               : core::MenuInteraction::None);
}

void WorkServicePlugin::speakSessionLine(const char *pose, const char *scene)
{
    if (m_controller == nullptr || pose == nullptr || scene == nullptr) {
        return;
    }
    // 深夜不播报（立绘与台词都不打扰）——与状态机「菜单互动让位于深夜」同口径
    if (isLateNightNow()) {
        return;
    }
    m_controller->presentGame(QString::fromLatin1(pose), QString::fromLatin1(scene),
                              static_cast<int>(core::kCuriousWindowMs));
}

QList<plugin::PluginContribution> WorkServicePlugin::contributions(const plugin::IPluginUiHost &host) const
{
    Q_UNUSED(host);
    QList<plugin::PluginContribution> list;
    if (m_service == nullptr) {
        return list;
    }

    plugin::PluginContribution contextItem;
    contextItem.pluginId = QString::fromLatin1(kPluginId);
    contextItem.id = QStringLiteral("workbench.open");
    contextItem.kind = plugin::ContributionKind::ContextMenu;
    contextItem.label = QStringLiteral("工作台…");
    contextItem.order = kContributionOrder;
    contextItem.createView = [this](QWidget *) -> QWidget * { return ensurePanel(); };
    list.push_back(contextItem);

    plugin::PluginContribution trayItem = contextItem;
    trayItem.id = QStringLiteral("workbench.open.tray");
    trayItem.kind = plugin::ContributionKind::TrayMenu;
    list.push_back(trayItem);

    return list;
}

QWidget *WorkServicePlugin::ensurePanel() const
{
    if (m_panel == nullptr && m_service != nullptr) {
        // 顶层工具窗口：**不设 QObject 父**，所有权归插件（避免与宿主父子关系双重释放）
        // P11：把宿主注入的「工作台页」分发回调交给面板 —— 面板构造末尾据此追加
        // WorkbenchPage 贡献点（绘画三页等）；回调为空时与 P10 行为一致（只有「干活」页）。
        ui::WorkBenchPanel::FillPagesFn fillPages;
        if (m_hooks != nullptr) {
            fillPages = m_hooks->workbenchFillPages;
        }
        auto *panel = new ui::WorkBenchPanel(m_service, std::move(fillPages), nullptr);
        if (m_hooks != nullptr && m_hooks->workOutputDirChosen) {
            panel->outputDirChosen = m_hooks->workOutputDirChosen;
        }
        m_panel = panel;
    }
    return m_panel;
}

} // namespace whalepet
