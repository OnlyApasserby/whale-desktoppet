#pragma once

// 上下文数据提供者接口（docs/CONTEXT-API.md）。
//
// 为什么是接口：ContextApiService 及其内置能力**不依赖** view 层（PetController /
// GrowthService / 各 Service），否则 whalepet_contextapi 必须反向依赖 whalepet_view，
// 与 whalepet_view → whalepet_contextapi 形成循环（见 docs/PLUGIN-ARCHITECTURE.md §3.1）。
// 实现落在 view 侧（viewmodel::PetContextProvider），单测可注入假 provider。

#include "contextapi/ContextSnapshot.h"

namespace whalepet::contextapi {

class IContextProvider {
public:
    virtual ~IContextProvider() = default;

    // 产出一份上下文快照。允许在任何时候调用（GUI 线程）；
    // 数据缺失时对应分组字段留空 / 0，**不得伪造**。
    virtual ContextSnapshot snapshot() const = 0;
};

} // namespace whalepet::contextapi
