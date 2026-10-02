#include "contextapi/builtin/ContextCapabilities.h"

#include <QJsonObject>

#include <functional>
#include <utility>

namespace whalepet::contextapi {

namespace {

const char *const kContextCapabilitiesPluginId = "whalepet.context";

// 能力 id 唯一真源（顺序：快照 / 环境 / 工作状态 / 桌宠 / 会话）
const char *const kSnapshotId = "context.snapshot";
const char *const kEnvironmentId = "context.environment";
const char *const kWorkStateId = "context.workState";
const char *const kPetStatusId = "pet.status";
const char *const kSessionStatsId = "session.stats";

plugin::CapabilityDescriptor makeDescriptor(const char *id, const QString &displayName,
                                           const QString &description)
{
    plugin::CapabilityDescriptor descriptor;
    descriptor.id = QString::fromLatin1(id);
    descriptor.displayName = displayName;
    descriptor.description = description;
    descriptor.readOnly = true;
    descriptor.origin = plugin::PluginOrigin::Builtin;
    descriptor.paramsSchema = QStringLiteral("{\"type\":\"object\",\"properties\":{}}");
    return descriptor;
}

// 把一个 snapshot() 投影函数包装为能力
class SnapshotCapability : public plugin::SimpleCapability {
public:
    using Projector = std::function<QJsonObject(const ContextSnapshot &)>;

    SnapshotCapability(IContextProvider *provider, plugin::CapabilityDescriptor descriptor,
                       Projector projector)
        : plugin::SimpleCapability(std::move(descriptor))
        , m_provider(provider)
        , m_projector(std::move(projector))
    {
    }

protected:
    bool call(const QJsonObject &in, QJsonObject &out, QJsonObject &error) override
    {
        Q_UNUSED(in);
        if (m_provider == nullptr) {
            error = plugin::makeRpcError(plugin::kRpcErrorCapabilityUnavailable,
                                         QStringLiteral("上下文数据不可用（未装配 provider）"));
            return false;
        }
        out = m_projector(m_provider->snapshot());
        return true;
    }

private:
    IContextProvider *m_provider = nullptr;
    Projector m_projector;
};

class ContextCapabilitiesPlugin : public plugin::SimplePlugin {
public:
    explicit ContextCapabilitiesPlugin(IContextProvider *provider)
        : plugin::SimplePlugin(plugin::PluginInfo{
              // 注意：QStringLiteral 只接受字面量，不能传变量
              QString::fromLatin1(kContextCapabilitiesPluginId), QStringLiteral("本地上下文"),
              QStringLiteral("向 AI Agent / 本地工具提供桌宠上下文能力"), QStringLiteral("1.0"),
              QString() })
        , m_provider(provider)
    {
    }

    void registerCapabilities(plugin::CapabilityRegistry &registry) override
    {
        registry.add(std::make_unique<SnapshotCapability>(
            m_provider, makeDescriptor(kSnapshotId, QStringLiteral("上下文快照"),
                                       QStringLiteral("环境 / 工作状态 / 养成 / 会话的完整快照")),
            [](const ContextSnapshot &snapshot) { return snapshot.toJson(); }));

        registry.add(std::make_unique<SnapshotCapability>(
            m_provider,
            makeDescriptor(kEnvironmentId, QStringLiteral("桌面环境"),
                           QStringLiteral("前台应用 / 窗口标题 / 空闲与输入活跃度（只计数）")),
            [](const ContextSnapshot &snapshot) { return snapshot.envJson(); }));

        registry.add(std::make_unique<SnapshotCapability>(
            m_provider, makeDescriptor(kWorkStateId, QStringLiteral("工作状态"),
                                       QStringLiteral("工作状态 + 置信度 + 进入时刻 + 是否专注态")),
            [](const ContextSnapshot &snapshot) { return snapshot.workJson(); }));

        registry.add(std::make_unique<SnapshotCapability>(
            m_provider, makeDescriptor(kPetStatusId, QStringLiteral("桌宠状态"),
                                       QStringLiteral("等级 / 经验 / 心情 / 好感 / 饱食 / 羁绊 / 陪伴时长")),
            [](const ContextSnapshot &snapshot) { return snapshot.petJson(); }));

        registry.add(std::make_unique<SnapshotCapability>(
            m_provider, makeDescriptor(kSessionStatsId, QStringLiteral("会话统计"),
                                       QStringLiteral("本次运行时长 / 采样数 / 交互数 / 状态变化数")),
            [](const ContextSnapshot &snapshot) { return snapshot.sessionJson(); }));
    }

private:
    IContextProvider *m_provider = nullptr;
};

} // namespace

QStringList contextCapabilityIds()
{
    return { QString::fromLatin1(kSnapshotId), QString::fromLatin1(kEnvironmentId),
             QString::fromLatin1(kWorkStateId), QString::fromLatin1(kPetStatusId),
             QString::fromLatin1(kSessionStatsId) };
}

std::unique_ptr<plugin::IPlugin> makeContextCapabilitiesPlugin(IContextProvider *provider)
{
    return std::make_unique<ContextCapabilitiesPlugin>(provider);
}

} // namespace whalepet::contextapi
