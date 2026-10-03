#pragma once

// 通用指针链适配器（engine=generic；亦为 unity-mono / unity-il2cpp 的共用底座）。
// 归属：docs/ROADMAP-ex1.md §2.6.2.1。

#include "gamestate/ChainSampler.h"
#include "gamestate/IGameMemoryReader.h"
#include "gamestate/IGameStateAdapter.h"

#include <memory>

namespace whalepet::gamestate {

class GenericChainAdapter final : public IGameStateAdapter {
public:
    // reader 为 nullptr 时使用平台默认只读读取器（Windows: Win32GameMemoryReader）；
    // 注入能力用于单测与「不支持平台」的优雅降级。
    explicit GenericChainAdapter(std::unique_ptr<IGameMemoryReader> reader = nullptr);
    ~GenericChainAdapter() override;

    bool attach(const GameProfile &profile, QString *error) override;
    void detach() override;
    bool attached() const override;
    bool read(core::GameSample *out, QString *error) override;

    // 连续读取失败 → 档案失效（§4.3）；供测试与上层诊断使用
    bool invalidated() const { return m_sampler != nullptr && m_sampler->invalidated(); }
    int consecutiveFailures() const
    {
        return m_sampler != nullptr ? m_sampler->consecutiveFailures() : 0;
    }

private:
    std::unique_ptr<IGameMemoryReader> m_reader;
    std::unique_ptr<ChainSampler> m_sampler;
    GameProfile m_profile;
};

} // namespace whalepet::gamestate
