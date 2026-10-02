#pragma once

// 外部进程插件层（三层中的第三层）——**本期只落配置与校验骨架**，
// 真实的「拉起子进程 + MCP 握手 + 能力发现」在 docs/ROADMAP-P7.md P7.4 实施。
//
// 为什么现在就建：这一层唯一的理由是**崩溃隔离**（第三方插件崩了不能带走桌宠），
// 而隔离边界会反向约束能力调用契约（必须异步、必须超时、必须可标记不可用），
// 这些约束已经落在 plugin::InvokeContext / CapabilityRegistry::setAvailable 上。
// 本期把「配置 → 校验 → 未来映射」的形状固定下来，避免 P7.4 返工。
//
// P7.4 的实现流程（已定，见 docs/PLUGIN-ARCHITECTURE.md §4.2）：
//   启动子进程(stdio) → initialize 握手 → tools/list 发现能力
//   → 每个 tool 映射为 `ext.<pluginId>.<tool>` 能力（origin = Process、异步回投）
//   → tools/call 转发；超时 / 心跳失败 / 进程退出 → 只把该来源的能力标记为不可用。

#include "plugin/PluginRegistry.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace whalepet::plugin {

// 一个外部插件进程的配置
struct ProcessServerSpec {
    QString pluginId;        // 稳定标识；能力 id 前缀 = "ext." + pluginId + "."
    QString program;         // 可执行文件（绝对路径或 PATH 中的名字）
    QStringList arguments;   // 启动参数
    int timeoutMs = 2000;    // 单次调用超时
};

// 配置校验结果（不启动进程，故不会产生「假运行中」状态）
struct ProcessServerState {
    ProcessServerSpec spec;
    bool valid = false;
    QString reason;          // valid == false 时的可读原因
};

class ProcessPluginLoader {
public:
    ProcessPluginLoader() = default;

    // 添加配置（重复 pluginId 会被 configure() 判为非法，不静默覆盖）
    void addServer(const ProcessServerSpec &spec);
    void clear() { m_specs.clear(); }

    int serverCount() const { return static_cast<int>(m_specs.size()); }
    const std::vector<ProcessServerSpec> &servers() const { return m_specs; }

    // 校验全部配置并记录结果；返回**合法**配置的数量。
    // 非法配置只记录（qWarning），不影响其它配置与主进程。
    int configure();

    const std::vector<ProcessServerState> &states() const { return m_states; }
    QStringList validPluginIds() const;

private:
    std::vector<ProcessServerSpec> m_specs;
    std::vector<ProcessServerState> m_states;
};

} // namespace whalepet::plugin
