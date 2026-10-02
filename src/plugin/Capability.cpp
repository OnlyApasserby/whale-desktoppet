#include "plugin/Capability.h"

#include <QDebug>
#include <QJsonValue>

#include <algorithm>
#include <utility>

namespace whalepet::plugin {

namespace {
const char *const kKeyCode = "code";
const char *const kKeyMessage = "message";
const char *const kKeyData = "data";
const char *const kKeyOk = "ok";
const char *const kKeyResult = "result";
const char *const kKeyError = "error";
} // namespace

int pluginOriginPriority(PluginOrigin origin)
{
    switch (origin) {
    case PluginOrigin::Builtin:
        return 0;
    case PluginOrigin::Dll:
        return 1;
    case PluginOrigin::Process:
        return 2;
    }
    return 3;
}

const char *pluginOriginId(PluginOrigin origin)
{
    switch (origin) {
    case PluginOrigin::Builtin:
        return "builtin";
    case PluginOrigin::Dll:
        return "dll";
    case PluginOrigin::Process:
        return "process";
    }
    return "unknown";
}

QJsonObject makeRpcError(int code, const QString &message, const QJsonObject &data)
{
    QJsonObject error;
    error.insert(QLatin1String(kKeyCode), code);
    error.insert(QLatin1String(kKeyMessage), message);
    if (!data.isEmpty()) {
        error.insert(QLatin1String(kKeyData), data);
    }
    return error;
}

int rpcErrorCode(const QJsonObject &error)
{
    const QJsonValue value = error.value(QLatin1String(kKeyCode));
    if (value.isDouble()) {
        return value.toInt();
    }
    return kRpcErrorInternal;
}

QString rpcErrorMessage(const QJsonObject &error)
{
    const QString message = error.value(QLatin1String(kKeyMessage)).toString();
    return message.isEmpty() ? QStringLiteral("未提供错误信息") : message;
}

QJsonObject rpcErrorData(const QJsonObject &error)
{
    return error.value(QLatin1String(kKeyData)).toObject();
}

// ---------------------------------------------------------------------------
// InvokeContext
// ---------------------------------------------------------------------------

InvokeContext::InvokeContext(Responder responder)
    : m_responder(std::move(responder))
{
}

void InvokeContext::setResponder(Responder responder)
{
    m_responder = std::move(responder);
}

InvokeContext::Responder InvokeContext::takeResponder()
{
    Responder taken = std::move(m_responder);
    m_responder = Responder();
    return taken;
}

void InvokeContext::deliver(QJsonObject response)
{
    if (m_answered) {
        // 重复作答只记第一条：异步能力可能既同步调用 respond 又在回调里补齐，
        // 静默丢弃第二次会让通道拿到错误结果，故留一条告警。
        qWarning() << "[Capability] 重复作答被忽略（首次结果已生效）";
        return;
    }
    m_answered = true;
    m_response = response;
    if (m_responder) {
        m_responder(m_response);
    }
}

void InvokeContext::respond(const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QLatin1String(kKeyOk), true);
    response.insert(QLatin1String(kKeyResult), result);
    deliver(std::move(response));
}

void InvokeContext::fail(int code, const QString &message, const QJsonObject &data)
{
    QJsonObject response;
    response.insert(QLatin1String(kKeyOk), false);
    response.insert(QLatin1String(kKeyError), makeRpcError(code, message, data));
    deliver(std::move(response));
}

// ---------------------------------------------------------------------------
// SimpleCapability
// ---------------------------------------------------------------------------

SimpleCapability::SimpleCapability(CapabilityDescriptor descriptor)
    : m_descriptor(std::move(descriptor))
{
}

CapabilityDescriptor SimpleCapability::descriptor() const
{
    return m_descriptor;
}

bool SimpleCapability::invoke(const QJsonObject &in, InvokeContext &ctx, QJsonObject &out,
                             QJsonObject &error)
{
    Q_UNUSED(ctx);
    // SimpleCapability 是**同步**实现：call() 返回 false 表示「同步失败」，
    // 必须翻译为 (返回 true + error 已填充)。
    // 若把 false 直接透传，分发侧会按契约理解为「异步已受理」，请求将永久挂起。
    call(in, out, error);
    return true;
}

// ---------------------------------------------------------------------------
// CapabilityRegistry
// ---------------------------------------------------------------------------

CapabilityRegistry::Entry *CapabilityRegistry::findEntry(const QString &id)
{
    for (Entry &entry : m_entries) {
        if (entry.capability != nullptr && entry.capability->descriptor().id == id) {
            return &entry;
        }
    }
    return nullptr;
}

bool CapabilityRegistry::add(std::unique_ptr<ICapability> capability)
{
    if (capability == nullptr) {
        return false;
    }
    const CapabilityDescriptor descriptor = capability->descriptor();
    if (!descriptor.isValid()) {
        qWarning() << "[CapabilityRegistry] 拒绝注册：能力 id 为空";
        return false;
    }

    Entry *existing = findEntry(descriptor.id);
    if (existing != nullptr && existing->capability != nullptr) {
        const CapabilityDescriptor old = existing->capability->descriptor();
        // Builtin > Dll > Process：优先级相同则保留先注册者（注册顺序即显式优先级）
        if (pluginOriginPriority(descriptor.origin) >= pluginOriginPriority(old.origin)) {
            qWarning() << "[CapabilityRegistry] 能力 id 冲突，保留先注册者:" << descriptor.id
                       << pluginOriginId(old.origin) << ">" << pluginOriginId(descriptor.origin);
            return false;
        }
        qWarning() << "[CapabilityRegistry] 能力 id 冲突，以更高优先级者替换:" << descriptor.id
                   << pluginOriginId(descriptor.origin) << ">" << pluginOriginId(old.origin);
        existing->capability = std::move(capability);
        existing->available = true;
        return true;
    }

    Entry entry;
    entry.capability = std::move(capability);
    entry.available = true;
    m_entries.push_back(std::move(entry));
    return true;
}

int CapabilityRegistry::count() const
{
    return static_cast<int>(m_entries.size());
}

bool CapabilityRegistry::contains(const QString &id) const
{
    return const_cast<CapabilityRegistry *>(this)->findEntry(id) != nullptr;
}

ICapability *CapabilityRegistry::find(const QString &id) const
{
    Entry *entry = const_cast<CapabilityRegistry *>(this)->findEntry(id);
    return (entry == nullptr) ? nullptr : entry->capability.get();
}

QList<CapabilityDescriptor> CapabilityRegistry::descriptors() const
{
    QList<CapabilityDescriptor> list;
    list.reserve(count());
    for (const Entry &entry : m_entries) {
        if (entry.capability != nullptr) {
            list.push_back(entry.capability->descriptor());
        }
    }
    std::sort(list.begin(), list.end(),
              [](const CapabilityDescriptor &a, const CapabilityDescriptor &b) { return a.id < b.id; });
    return list;
}

void CapabilityRegistry::setAvailable(const QString &id, bool available)
{
    Entry *entry = findEntry(id);
    if (entry == nullptr) {
        qWarning() << "[CapabilityRegistry] 设置可用性失败：能力不存在" << id;
        return;
    }
    entry->available = available;
    qInfo() << "[CapabilityRegistry] 能力" << id << (available ? "已可用" : "已标记不可用");
}

bool CapabilityRegistry::isAvailable(const QString &id) const
{
    Entry *entry = const_cast<CapabilityRegistry *>(this)->findEntry(id);
    return entry != nullptr && entry->available;
}

bool CapabilityRegistry::invoke(const QString &id, const QJsonObject &params, InvokeContext &ctx,
                                QJsonObject &out, QJsonObject &error)
{
    Entry *entry = findEntry(id);
    if (entry == nullptr || entry->capability == nullptr) {
        error = makeRpcError(kRpcErrorMethodNotFound, QStringLiteral("能力不存在：%1").arg(id));
        return true;
    }
    if (!entry->available) {
        error = makeRpcError(kRpcErrorCapabilityUnavailable,
                             QStringLiteral("能力当前不可用：%1").arg(id));
        return true;
    }
    return entry->capability->invoke(params, ctx, out, error);
}

} // namespace whalepet::plugin
