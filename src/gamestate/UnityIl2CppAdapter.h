#pragma once

// Unity IL2CPP 后端适配器（engine=unity-il2cpp）。
// 归属：docs/ROADMAP-ex1.md §2.6.1。运行期只消费 profile；
// 「dump.cs → profile」的离线转换见 UnityDumpConverter 与 docs/UNITY-SOP.md。

#include "gamestate/UnityAdapterBase.h"

namespace whalepet::gamestate {

class UnityIl2CppAdapter final : public UnityAdapterBase {
public:
    explicit UnityIl2CppAdapter(std::unique_ptr<IGameMemoryReader> reader = nullptr);

    // 本适配器处理的引擎标识。
    static const char *engineName();

protected:
    QString backendHint() const override;
};

} // namespace whalepet::gamestate
