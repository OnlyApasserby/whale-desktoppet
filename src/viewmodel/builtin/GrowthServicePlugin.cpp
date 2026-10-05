#include "viewmodel/builtin/GrowthServicePlugin.h"

#include "model/Database.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/PetController.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include <memory>

namespace whalepet {

namespace {
const char *const kPluginId = "builtin.growth";
const char *const kCapabilityId = "service.growth";
} // namespace

GrowthServicePlugin::GrowthServicePlugin(QObject *host, BuiltinServiceHandles *handles)
    : m_host(host)
    , m_handles(handles)
{
}

plugin::PluginInfo GrowthServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("养成服务");
    info.description = QStringLiteral("养成数值编排：载入 / 落盘 / 注入 PetController");
    return info;
}

void GrowthServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<GrowthServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("养成状态"),
                                        QStringLiteral("等级 / 经验 / 心情 / 好感 / 饱食 / 羁绊 / 陪伴时长")),
        this,
        [](GrowthServicePlugin *plugin) {
            const model::PetStateData &state = plugin->service()->state();
            QJsonObject out;
            out.insert(QStringLiteral("level"), state.level);
            out.insert(QStringLiteral("exp"), state.exp);
            out.insert(QStringLiteral("coins"), state.coins);
            out.insert(QStringLiteral("mood"), state.mood);
            out.insert(QStringLiteral("affinity"), state.affinity);
            out.insert(QStringLiteral("satiety"), state.satiety);
            out.insert(QStringLiteral("bondLevel"), state.bondLevel);
            out.insert(QStringLiteral("companionMs"), static_cast<double>(state.companionMs));
            out.insert(QStringLiteral("streakDays"), state.streakDays);
            return out;
        },
        QStringLiteral("养成服务不可用（插件未启动）")));
}

bool GrowthServicePlugin::start(plugin::PluginContext &ctx)
{
    if (m_service != nullptr) {
        return true; // 幂等：宿主可能在构造期与 showPet 各调用一次 startAll
    }
    if (m_host == nullptr || ctx.db == nullptr) {
        qWarning() << "[GrowthServicePlugin] 缺少宿主窗口或数据库，跳过养成服务";
        return false;
    }

    m_service = new viewmodel::GrowthService(ctx.db, m_host);
    m_service->load();
    if (ctx.controller != nullptr) {
        ctx.controller->setGrowthService(m_service);
    }

    // 退出前强制落盘 + 记录本次退出时刻（原 PetWindow::setupGrowth 的逻辑型接线）。
    model::Database *db = ctx.db;
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, m_service, [this, db] {
        if (m_service != nullptr) {
            m_service->flush();
        }
        if (db != nullptr && db->isOpen()) {
            db->setMeta(QStringLiteral("app.last_seen_ms"),
                        QString::number(QDateTime::currentMSecsSinceEpoch()));
        }
    });

    if (m_handles != nullptr) {
        m_handles->growth = m_service;
    }
    qInfo() << "[GrowthServicePlugin] 养成服务已创建并注入 controller";
    return true;
}

void GrowthServicePlugin::stop()
{
    if (m_handles != nullptr) {
        m_handles->growth = nullptr;
    }
    // 服务对象由 m_host（QObject parent）持有，此处只停定时器并落盘。
    if (m_service != nullptr) {
        m_service->stopTicking();
        m_service->flush();
    }
}

} // namespace whalepet
