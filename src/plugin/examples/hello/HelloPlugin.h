#pragma once

// P7.3 示例动态插件（DLL）。构建产物 ext_hello.dll 放入 <应用目录>/plugins/ 即被
// DllPluginLoader 装载，能力 `ext.hello.greet` 随即可在 capabilities.list 中看到并被调用。
//
// 本文件同时是第三方插件作者的**最小落地模板**：
//   * 实现 IPluginFactory（ABI 边界），用 Q_PLUGIN_METADATA 导出；
//   * 工厂只负责 create()，插件实例只负责 info() + registerCapabilities()；
//   * 绝不依赖宿主的界面类型，失败一律用 error 表达（不抛异常）。

#include "plugin/PluginInterface.h"
#include "plugin/dll/IPluginFactory.h"

#include <QObject>
#include <QtPlugin>

namespace whalepet::plugin::examples {

// ABI 边界：IID 与 kPluginApiVersion 必须与宿主一致（见 plugin/dll/IPluginFactory.h）
class HelloPluginFactory : public QObject, public IPluginFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "ai.whalepet.PluginFactory/1.0" FILE "metadata.json")
    Q_INTERFACES(whalepet::plugin::IPluginFactory)

public:
    int apiVersion() const override { return kPluginApiVersion; }
    std::unique_ptr<IPlugin> create() override;
};

} // namespace whalepet::plugin::examples
