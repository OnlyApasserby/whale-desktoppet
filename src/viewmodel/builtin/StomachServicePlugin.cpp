#include "viewmodel/builtin/StomachServicePlugin.h"

#include "viewmodel/StomachService.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QDebug>

#include <memory>

namespace whalepet {

namespace {
const char *const kPluginId = "builtin.stomach";
const char *const kCapabilityId = "service.stomach";
} // namespace

StomachServicePlugin::StomachServicePlugin(QObject *host, BuiltinServiceHandles *handles)
    : m_host(host)
    , m_handles(handles)
{
}

plugin::PluginInfo StomachServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("胃袋服务");
    info.description = QStringLiteral("拖拽投喂落盘 <安装目录>/stomach + 定时清空到回收站");
    return info;
}

void StomachServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<StomachServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("胃袋状态"),
                                        QStringLiteral("stomach 目录路径与轮询运行状态")),
        this,
        [](StomachServicePlugin *plugin) {
            viewmodel::StomachService *service = plugin->service();
            QJsonObject out;
            out.insert(QStringLiteral("path"), service->stomachPath());
            out.insert(QStringLiteral("running"), service->running());
            out.insert(QStringLiteral("intervalMs"), viewmodel::StomachService::kCheckIntervalMs);
            return out;
        },
        QStringLiteral("胃袋服务不可用（插件未启动）")));
}

bool StomachServicePlugin::start(plugin::PluginContext &ctx)
{
    Q_UNUSED(ctx);
    if (m_service != nullptr) {
        return true;
    }
    if (m_host == nullptr) {
        qWarning() << "[StomachServicePlugin] 缺少宿主窗口，跳过胃袋服务";
        return false;
    }

    m_service = new viewmodel::StomachService(m_host);
    // 安装目录不可写时仅告警（不崩溃、不改写别处），拖拽投喂的动画/数值仍照常触发。
    if (!m_service->ensureStomachDir()) {
        qWarning() << "[StomachServicePlugin] stomach 目录不可用，拖入的文件将无法落盘:"
                   << m_service->stomachPath();
    }

    // 逻辑型日志接线（原 PetWindow::setupStomach 的两条 connect）。
    QObject::connect(m_service, &viewmodel::StomachService::ingested, m_service, [this](int count) {
        qInfo() << "[StomachServicePlugin] 拖拽投喂入胃:" << count << "项 →"
                << (m_service != nullptr ? m_service->stomachPath() : QString());
    });
    QObject::connect(m_service, &viewmodel::StomachService::trashed, m_service, [](int count) {
        qInfo() << "[StomachServicePlugin] stomach 定时清空，移入回收站:" << count << "项";
    });

    if (m_handles != nullptr) {
        m_handles->stomach = m_service;
    }
    return true;
}

void StomachServicePlugin::stop()
{
    if (m_handles != nullptr) {
        m_handles->stomach = nullptr;
    }
    // 子对象随宿主析构，这里只停定时器（与原 PetWindow 析构一致）。
    if (m_service != nullptr) {
        m_service->stop();
    }
}

} // namespace whalepet
