#pragma once

// P9-B：外部进程插件配置（plugins.json）的**纯逻辑**解析（docs/ROADMAP-P9-Fin.md §P9-B）。
//
// 从宿主下沉而来：此前 `PetWindow::setupProcessPlugins()` 内联「读文件 + 逐字段解析 +
// addServer」，本类把它收敛为可脱 UI 单测的解析器；宿主只保留
// 「定位配置 → 加载 → 注册 → 启动」的编排与状态展示。
//
// 边界：只做「文本/文件 → 配置值」的映射与**结构级**告警（非对象条目等）；
// 语义校验（空 id / 空 program / timeoutMs<=0 / 重复 id）仍由
// `ProcessPluginLoader::configure()` 负责，避免同一规则两处定义。

#include "plugin/process/ProcessServerSpec.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace whalepet::plugin {

struct ProcessPluginConfig {
    std::vector<ProcessServerSpec> servers;
    QStringList warnings; // 结构级告警（如忽略非对象条目）
    int skippedCount = 0; // 被跳过的非对象条目数

    // 解析 JSON 数组文本。失败（语法错误 / 非数组）返回 false 并填 errorOut。
    static bool parse(const QByteArray &json, ProcessPluginConfig *out, QString *errorOut);

    // 读取文件并解析。文件不存在 / 不可读 / 解析失败 → false 并填 errorOut。
    static bool loadFromFile(const QString &path, ProcessPluginConfig *out, QString *errorOut);
};

} // namespace whalepet::plugin
