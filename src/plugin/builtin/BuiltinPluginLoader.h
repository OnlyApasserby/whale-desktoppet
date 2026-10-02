#pragma once

// 内置插件装载器（三层插件中的第一层：进程内静态注册）。
//
// 为什么不做成「自动发现」：
//   内置插件随主程序编译，其注册函数由**宿主**（组合根）提供——这样 plugin 静态库
//   不必反向依赖 whalepet_view / whalepet_contextapi（否则形成循环依赖，见
//   docs/PLUGIN-ARCHITECTURE.md §3.1）。装载器因此只做「按序调用注册函数 + 计数 + 日志」。
//
// 优先级：内置层高于 DLL 层与外部进程层（同名能力由 CapabilityRegistry 仲裁）。

#include "plugin/PluginRegistry.h"

#include <functional>
#include <vector>

namespace whalepet::plugin {

// 一个注册函数：把一组内置插件注册进总线，返回本次注册成功的插件数（负值 = 失败）
using BuiltinRegisterFn = std::function<int(PluginRegistry &)>;

class BuiltinPluginLoader {
public:
    BuiltinPluginLoader() = default;

    void addRegisterFn(BuiltinRegisterFn fn);

    // 依序执行全部注册函数；单个函数返回负值只记录并继续（不阻断开机）。
    // 返回本次注册的插件总数。
    int load(PluginRegistry &registry) const;

    int registerFnCount() const { return static_cast<int>(m_fns.size()); }

private:
    std::vector<BuiltinRegisterFn> m_fns;
};

} // namespace whalepet::plugin
