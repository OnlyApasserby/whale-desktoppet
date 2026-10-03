#pragma once

// Unity Mono 后端适配器（engine=unity-mono）。
// 归属：docs/ROADMAP-ex1.md §2.6.1。运行期消费 profile（实例字段用偏移表）；
// 静态根/字段偏移的「按类名+字段名」定位由离线 SOP 产出（见 docs/UNITY-SOP.md）。

#include "gamestate/UnityAdapterBase.h"

namespace whalepet::gamestate {

class UnityMonoAdapter final : public UnityAdapterBase {
public:
    explicit UnityMonoAdapter(std::unique_ptr<IGameMemoryReader> reader = nullptr);

    // 本适配器处理的引擎标识。
    static const char *engineName();

protected:
    QString backendHint() const override;
};

} // namespace whalepet::gamestate
