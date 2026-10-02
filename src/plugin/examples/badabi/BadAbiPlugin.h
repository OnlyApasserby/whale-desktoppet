#pragma once

// P7.3 负例插件：元数据声明的 apiVersion 高于宿主（99），用于验证
// 「ABI 不匹配的 DLL 被跳过、且不影响其它插件与主程序启动」。
//
// 该 DLL **仅供构建与自动化测试**，不随安装包分发（见 docs/packages.md §8）。

#include "plugin/PluginInterface.h"
#include "plugin/dll/IPluginFactory.h"

#include <QObject>
#include <QtPlugin>

namespace whalepet::plugin::examples {

class BadAbiPluginFactory : public QObject, public IPluginFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "ai.whalepet.PluginFactory/1.0" FILE "metadata.json")
    Q_INTERFACES(whalepet::plugin::IPluginFactory)

public:
    int apiVersion() const override { return kPluginApiVersion; }
    std::unique_ptr<IPlugin> create() override;
};

} // namespace whalepet::plugin::examples
