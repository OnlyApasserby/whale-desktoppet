#pragma once

// P9-A：服务只读状态能力的通用模板（docs/ROADMAP-P9.md §P9-A）。
//
// 关键约束：能力在 `PluginRegistry::add()` 时（**早于** `start()`）就被注册进能力表，
// 因此能力不能在构造时捕获服务实例，只能持有插件指针，在 `call()` 时经
// `plugin->service()` 取当前实例。服务尚未就绪（未 start / 已 stop）时返回
// kRpcErrorCapabilityUnavailable —— 不伪造数据（与 ContextCapabilities 的口径一致）。

#include "plugin/Capability.h"

#include <QJsonObject>
#include <QString>

#include <functional>
#include <utility>

namespace whalepet {

// 服务状态能力的统一元数据：origin 固定 Builtin、readOnly 固定 true、无参数。
inline plugin::CapabilityDescriptor makeServiceCapabilityDescriptor(const char *id,
                                                                   const QString &displayName,
                                                                   const QString &description)
{
    plugin::CapabilityDescriptor descriptor;
    descriptor.id = QString::fromLatin1(id);
    descriptor.displayName = displayName;
    descriptor.description = description;
    descriptor.origin = plugin::PluginOrigin::Builtin;
    descriptor.readOnly = true;
    descriptor.paramsSchema = QStringLiteral("{\"type\":\"object\",\"properties\":{}}");
    return descriptor;
}

// PluginT 需提供 `Service *service() const`。
template <typename PluginT>
class ServiceStatusCapability : public plugin::SimpleCapability {
public:
    using Snapshot = std::function<QJsonObject(PluginT *)>;

    ServiceStatusCapability(plugin::CapabilityDescriptor descriptor, PluginT *plugin,
                            Snapshot snapshot, QString unavailableMessage)
        : plugin::SimpleCapability(std::move(descriptor))
        , m_plugin(plugin)
        , m_snapshot(std::move(snapshot))
        , m_unavailable(std::move(unavailableMessage))
    {
    }

protected:
    bool call(const QJsonObject &in, QJsonObject &out, QJsonObject &error) override
    {
        Q_UNUSED(in);
        if (m_plugin == nullptr || m_plugin->service() == nullptr) {
            error = plugin::makeRpcError(plugin::kRpcErrorCapabilityUnavailable, m_unavailable);
            return false;
        }
        // 快照取**插件指针**（回调内再经 plugin->service() 读取，避免能力缓存服务实例）
        out = m_snapshot(m_plugin);
        return true;
    }

private:
    PluginT *m_plugin = nullptr; // 非拥有：插件由 PluginRegistry 持有，存活期覆盖能力
    Snapshot m_snapshot;
    QString m_unavailable;
};

} // namespace whalepet
