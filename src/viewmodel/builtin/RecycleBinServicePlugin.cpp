#include "viewmodel/builtin/RecycleBinServicePlugin.h"

#include "viewmodel/PetController.h"
#include "viewmodel/RecycleBinService.h"
#include "viewmodel/builtin/ServiceStatusCapability.h"

#include <QDebug>

#include <memory>

namespace whalepet {

namespace {
const char *const kPluginId = "builtin.recycleBin";
const char *const kCapabilityId = "service.recycleBin";
} // namespace

RecycleBinServicePlugin::RecycleBinServicePlugin(QObject *host, BuiltinServiceHandles *handles)
    : m_host(host)
    , m_handles(handles)
{
}

plugin::PluginInfo RecycleBinServicePlugin::info() const
{
    plugin::PluginInfo info;
    info.id = QString::fromLatin1(kPluginId);
    info.displayName = QStringLiteral("回收站提醒");
    info.description = QStringLiteral("随机轮询回收站 → sweep 立绘与清理提醒（只读）");
    return info;
}

void RecycleBinServicePlugin::registerCapabilities(plugin::CapabilityRegistry &registry)
{
    registry.add(std::make_unique<ServiceStatusCapability<RecycleBinServicePlugin>>(
        makeServiceCapabilityDescriptor(kCapabilityId, QStringLiteral("回收站状态"),
                                        QStringLiteral("最近一次查询：是否可用 / 条目数 / 占用字节 / 是否运行")),
        this,
        [](RecycleBinServicePlugin *plugin) {
            viewmodel::RecycleBinService *service = plugin->service();
            const viewmodel::RecycleBinInfo &info = service->lastInfo();
            QJsonObject out;
            out.insert(QStringLiteral("running"), service->running());
            out.insert(QStringLiteral("available"), info.available);
            out.insert(QStringLiteral("itemCount"), info.itemCount);
            out.insert(QStringLiteral("sizeBytes"), static_cast<double>(info.sizeBytes));
            out.insert(QStringLiteral("notEmpty"), info.available && !info.isEmpty());
            return out;
        },
        QStringLiteral("回收站服务不可用（插件未启动）")));
}

bool RecycleBinServicePlugin::start(plugin::PluginContext &ctx)
{
    if (m_service != nullptr) {
        return true;
    }
    if (m_host == nullptr) {
        qWarning() << "[RecycleBinServicePlugin] 缺少宿主窗口，跳过回收站服务";
        return false;
    }

    m_service = new viewmodel::RecycleBinService(m_host);

    // 逻辑型接线：提醒经 PetController 呈现 sweep 立绘与台词。
    // 托盘气泡（UI）由宿主另行连接同一信号，见 PetWindow::setupRecycleBin。
    if (ctx.controller != nullptr) {
        PetController *controller = ctx.controller;
        QObject::connect(m_service, &viewmodel::RecycleBinService::recycleBinNotEmpty, m_service,
                         [controller](int itemCount, qint64 sizeBytes) {
                             qInfo() << "[RecycleBinServicePlugin] 回收站非空: 条目" << itemCount
                                     << "占用" << sizeBytes << "字节";
                             controller->presentRecycleBinReminder(itemCount);
                         });
    }

    if (m_handles != nullptr) {
        m_handles->recycleBin = m_service;
    }
    return true;
}

void RecycleBinServicePlugin::stop()
{
    if (m_handles != nullptr) {
        m_handles->recycleBin = nullptr;
    }
    if (m_service != nullptr) {
        m_service->stop();
    }
}

} // namespace whalepet
