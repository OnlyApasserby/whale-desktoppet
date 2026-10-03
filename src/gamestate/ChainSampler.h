#pragma once

// 通用的「指针链档案 → GameSample」采样器：generic / unity-mono / unity-il2cpp 共用。
// 归属：docs/ROADMAP-ex1.md §2.6.2.1、§4.3。集中承载魔数校验与连续失败失效自检。

#include "core/GameState.h"
#include "gamestate/GameProfile.h"
#include "gamestate/IGameMemoryReader.h"
#include "gamestate/PointerChainResolver.h"

#include <QString>

namespace whalepet::gamestate {

class ChainSampler {
public:
    ChainSampler(IGameMemoryReader *reader, const GameProfile *profile);

    // 采一轮；返回 true=有可用读数。失败时 out->available=false（nowMs 由调用方自行填充）
    bool sample(core::GameSample *out, QString *error);

    bool invalidated() const { return m_resolver.invalidated(); }
    int consecutiveFailures() const { return m_resolver.consecutiveFailures(); }
    QString lastError() const { return m_error; }
    void reset(); // 重连 / 档案重载后调用

private:
    IGameMemoryReader *m_reader = nullptr;
    const GameProfile *m_profile = nullptr;
    PointerChainResolver m_resolver;
    QString m_error;
};

} // namespace whalepet::gamestate
