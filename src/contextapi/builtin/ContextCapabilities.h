#pragma once

// 上下文能力插件（内置层）：把上下文查询暴露为**能力**。
//
// 关键设计：能力 id 就是 JSON-RPC 方法名 ——
//   context.snapshot / context.environment / context.workState / pet.status / session.stats
// JsonRpcDispatcher 的「能力别名」路由（方法名 == 能力 id ⇒ 等价 capability.invoke）
// 因此无需任何额外映射，且 MCP `tools/list` 直接由能力清单生成。

#include "contextapi/IContextProvider.h"
#include "plugin/PluginInterface.h"

#include <QStringList>

#include <memory>

namespace whalepet::contextapi {

// provider 可空：为空时能力调用返回 kRpcErrorCapabilityUnavailable（不伪造数据）
std::unique_ptr<plugin::IPlugin> makeContextCapabilitiesPlugin(IContextProvider *provider);

// 上下文能力 id 列表（唯一真源）：供运行期可用性门控与测试核对，避免 id 漂移
QStringList contextCapabilityIds();

} // namespace whalepet::contextapi
