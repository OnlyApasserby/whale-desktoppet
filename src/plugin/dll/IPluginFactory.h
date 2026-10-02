#pragma once

// 动态插件（DLL）侧的稳定 ABI 边界（docs/PLUGIN-ARCHITECTURE.md §4.1）。
//
// 约定：
//   * DLL 内实现 IPluginFactory，并用 `Q_PLUGIN_METADATA` + `Q_PLUGIN_INSTANCE` 导出；
//   * IID 中的 `/1.0` **就是 ABI 版本**：任何破坏性变更必须提升该版本号；
//   * 宿主按 IID 匹配 + `apiVersion` 比对做版本协商；不匹配只记录并跳过，绝不影响主进程；
//   * 元数据（`Q_PLUGIN_METADATA(... FILE "metadata.json")`）至少包含：
//       { "id": "ext.hello", "displayName": "示例插件", "apiVersion": 1 }
//     宿主只使用 apiVersion 做准入，插件自述信息以 create() 后 info() 为准。

#include "plugin/PluginInterface.h"

#include <QtPlugin>

#include <memory>

namespace whalepet::plugin {

// 宿主与插件共同支持的 ABI 版本（元数据与 factory 必须同时一致）
inline constexpr int kPluginApiVersion = 1;

class IPluginFactory {
public:
    virtual ~IPluginFactory() = default;

    // 宿主要求的 ABI 版本；宿主只接受 (0, kPluginApiVersion]
    virtual int apiVersion() const = 0;

    // 创建插件实例（所有权交给宿主）
    virtual std::unique_ptr<IPlugin> create() = 0;
};

} // namespace whalepet::plugin

// IID 末尾的 /1.0 与 kPluginApiVersion 对应；提升 ABI 版本时同步修改
Q_DECLARE_INTERFACE(whalepet::plugin::IPluginFactory, "ai.whalepet.PluginFactory/1.0")
