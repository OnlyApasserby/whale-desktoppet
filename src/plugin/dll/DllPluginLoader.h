#pragma once

// 动态插件层装载器（三层中的第二层）：QPluginLoader + 元数据握手 + 版本协商。
//
// 硬约束（docs/PLUGIN-ARCHITECTURE.md §4.1）：
//   * **任何失败都只记录并跳过**——缺符号 / IID 不匹配 / apiVersion 高于宿主 / 目录不存在，
//     一律不得 Fatal、不得影响主进程与其它插件；
//   * 目录不存在属于正常情况（没有第三方插件），只记一条 info；
//   * 加载器必须**保活 QPluginLoader**：Qt 的插件在最后一个 loader 卸载时会被卸载，
//     仅保存 root QObject 指针不足以保证插件存活。

#include "plugin/PluginRegistry.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QPluginLoader;

namespace whalepet::plugin {

struct DllLoadReport {
    QStringList loaded;  // 成功装载的插件 id
    QStringList skipped; // 被跳过的插件文件名（原因见日志）
};

class DllPluginLoader {
public:
    DllPluginLoader() = default;
    explicit DllPluginLoader(const QString &pluginDir);
    ~DllPluginLoader();

    DllPluginLoader(const DllPluginLoader &) = delete;
    DllPluginLoader &operator=(const DllPluginLoader &) = delete;

    void setPluginDirectory(const QString &dir) { m_dir = dir; }
    QString pluginDirectory() const { return m_dir; }

    // 扫描目录并装载全部合法插件。已装载的插件在装载器析构前一直可用。
    DllLoadReport loadAll(PluginRegistry &registry);

    // 已成功装载的插件 id（按装载顺序）
    QStringList loadedPluginIds() const { return m_loadedIds; }

private:
    // 返回 true 表示已成功注册；失败时 reason 给出可读原因
    bool loadOne(const QString &filePath, PluginRegistry &registry, QString *reason);

    QString m_dir;
    QStringList m_loadedIds;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders; // 保活：卸载会在 loader 析构时发生
};

} // namespace whalepet::plugin
