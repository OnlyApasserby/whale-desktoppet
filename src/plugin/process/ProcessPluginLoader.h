#pragma once

// 外部进程插件层（三层中的第三层）：作为 **MCP Client** 接入外部进程插件
// （docs/PLUGIN-ARCHITECTURE.md §4.2、docs/ROADMAP-P7.md P7.4）。
//
// 流程：
//   配置 → 校验 → 启动子进程(stdio) → initialize 握手 → tools/list 发现
//   → 每个 tool 映射为 `ext.<pluginId>.<tool>` 能力（origin = Process、异步回投）
//   → tools/call 转发；超时 / 崩溃 / 进程退出 → 只把该来源的能力标记为不可用。
//
// 为什么需要这一层：唯一的理由是**崩溃隔离**——第三方插件崩了不能带走桌宠。
// 隔离边界反向约束了能力调用契约（必须异步、必须超时、必须可标记不可用），
// 这些约束落在 plugin::InvokeContext / CapabilityRegistry::setAvailable 上。
//
// 与宿主的关系：宿主只按能力 id 访问，三层（内置 / DLL / 外部进程）不可区分。

#include "plugin/PluginRegistry.h"
#include "plugin/process/McpPluginSession.h"
#include "plugin/process/ProcessServerSpec.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace whalepet::plugin {

class ProcessPluginLoader : public QObject {
    Q_OBJECT
public:
    explicit ProcessPluginLoader(QObject *parent = nullptr);
    ~ProcessPluginLoader() override;

    // 添加配置（重复 pluginId 会被 configure() 判为非法，不静默覆盖）
    void addServer(const ProcessServerSpec &spec);
    void clear();

    int serverCount() const { return static_cast<int>(m_specs.size()); }
    const std::vector<ProcessServerSpec> &servers() const { return m_specs; }

    // 校验全部配置并记录结果；返回**合法**配置的数量。
    // 非法配置只记录（qWarning），不影响其它配置与主进程。
    int configure();

    const std::vector<ProcessServerState> &states() const { return m_states; }
    QStringList validPluginIds() const;

    // 拉起全部合法配置的外部插件进程：握手 + 能力发现 + 注册（origin = Process）。
    // 单个 server 失败只记录并跳过（不阻断其它 server 与主进程）。
    // 返回成功接入（握手成功）的 server 数。
    int start(CapabilityRegistry &registry);
    void stop();

    bool running() const;
    int sessionCount() const { return static_cast<int>(m_sessions.size()); }

    // 已注册的外部能力 id（诊断 / 测试）
    QStringList registeredCapabilityIds() const;

signals:
    // 外部来源的能力可用性变化（进程退出 → 相关能力标记为不可用）
    void capabilityAvailabilityChanged(const QString &capabilityId, bool available);
    void sessionExited(const QString &pluginId);

private:
    std::vector<ProcessServerSpec> m_specs;
    std::vector<ProcessServerState> m_states;
    std::vector<std::unique_ptr<McpPluginSession>> m_sessions;
    CapabilityRegistry *m_registry = nullptr;
};

} // namespace whalepet::plugin
