#pragma once

// 外部进程插件（MCP server）的配置与校验状态（docs/PLUGIN-ARCHITECTURE.md §4.2）。
//
// 从 ProcessPluginLoader.h 抽出为独立头：ProcessPluginLoader（编排）与 McpPluginSession
// （单进程会话）都需要这两个值类型，抽出后两者不必互相包含，避免循环依赖。

#include <QString>
#include <QStringList>

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

// P9-B：外部进程插件的运行状态快照（供宿主**只读**展示；查询不启动 / 不改变任何状态）
struct ProcessPluginStatus {
    QString pluginId;
    QString program;
    bool valid = false;      // 配置是否合法（configure() 的结论）
    bool running = false;    // 会话是否运行中
    int toolCount = 0;       // 已发现的 tool 数（对应注册的能力数）
    QString reason;          // 非正常原因（非法配置 / 未接入 / 已退出）；空 = 正常
};

} // namespace whalepet::plugin
