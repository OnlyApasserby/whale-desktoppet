#include "plugin/dll/DllPluginLoader.h"

#include "plugin/dll/IPluginFactory.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QPluginLoader>

#include <utility>

namespace whalepet::plugin {

DllPluginLoader::DllPluginLoader(const QString &pluginDir)
    : m_dir(pluginDir)
{
}

DllPluginLoader::~DllPluginLoader() = default;

DllLoadReport DllPluginLoader::loadAll(PluginRegistry &registry)
{
    DllLoadReport report;
    if (m_dir.isEmpty()) {
        return report;
    }

    const QDir dir(m_dir);
    if (!dir.exists()) {
        // 没有第三方插件目录是正常状态：只记 info，不告警
        qInfo() << "[DllPluginLoader] 动态插件目录不存在（正常）：" << m_dir;
        return report;
    }

    const QStringList files = dir.entryList({ QStringLiteral("*.dll") }, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QString reason;
        const QString path = dir.absoluteFilePath(name);
        if (loadOne(path, registry, &reason)) {
            report.loaded.append(name);
        } else {
            report.skipped.append(name);
            qWarning() << "[DllPluginLoader] 跳过动态插件:" << name << "原因:" << reason;
        }
    }
    qInfo() << "[DllPluginLoader] 动态插件装载完成：成功" << report.loaded.size() << "跳过"
            << report.skipped.size();
    return report;
}

bool DllPluginLoader::loadOne(const QString &filePath, PluginRegistry &registry, QString *reason)
{
    const auto fail = [reason](const QString &text) {
        if (reason != nullptr) {
            *reason = text;
        }
        return false;
    };

    // 注意：QPluginLoader 继承 QObject → **不可拷贝/移动**，必须直接堆分配。
    // 同时它也不能被析构，否则插件会被卸载（见 IPluginFactory.h 的说明）。
    auto loader = std::make_unique<QPluginLoader>(filePath);

    // 先做元数据与 ABI 版本协商，再尝试实例化（尽早拒绝已知不兼容的 DLL）
    const QJsonObject metaData = loader->metaData();
    if (metaData.isEmpty()) {
        return fail(QStringLiteral("缺少 Q_PLUGIN_METADATA（或该文件不是 Qt 插件）"));
    }
    const QJsonObject pluginMeta = metaData.value(QStringLiteral("MetaData")).toObject();
    const int apiVersion = pluginMeta.value(QStringLiteral("apiVersion")).toInt(0);
    if (apiVersion <= 0) {
        return fail(QStringLiteral("元数据缺少 apiVersion"));
    }
    if (apiVersion > kPluginApiVersion) {
        return fail(QStringLiteral("插件 ABI 版本 %1 高于宿主支持版本 %2")
                        .arg(apiVersion)
                        .arg(kPluginApiVersion));
    }

    QObject *root = loader->instance();
    if (root == nullptr) {
        return fail(QStringLiteral("实例化失败：%1").arg(loader->errorString()));
    }

    auto *factory = qobject_cast<IPluginFactory *>(root);
    if (factory == nullptr) {
        const QString unmatch = QStringLiteral("未实现 IPluginFactory（IID 不匹配或版本不符）");
        loader->unload();
        return fail(unmatch);
    }
    if (factory->apiVersion() <= 0 || factory->apiVersion() > kPluginApiVersion) {
        const QString mismatch = QStringLiteral("factory apiVersion=%1 与宿主支持版本 %2 不兼容")
                                     .arg(factory->apiVersion())
                                     .arg(kPluginApiVersion);
        loader->unload();
        return fail(mismatch);
    }

    std::unique_ptr<IPlugin> plugin = factory->create();
    if (plugin == nullptr) {
        const QString empty = QStringLiteral("IPluginFactory::create() 返回空指针");
        loader->unload();
        return fail(empty);
    }

    const QString pluginId = plugin->info().id;
    if (!registry.add(std::move(plugin))) {
        const QString rejected = QStringLiteral("注册被拒绝（id 为空或与已有插件冲突）");
        loader->unload();
        return fail(rejected);
    }

    m_loaders.push_back(std::move(loader)); // 保活
    m_loadedIds.append(pluginId);
    qInfo() << "[DllPluginLoader] 已装载动态插件:" << pluginId << QFileInfo(filePath).fileName();
    return true;
}

} // namespace whalepet::plugin
