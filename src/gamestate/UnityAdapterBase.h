#pragma once

// Unity 引擎适配器公共基类：承载「只读内存 + 指针链 + 失效自检」的公共逻辑，
// 由 UnityMonoAdapter / UnityIl2CppAdapter 继承并注入引擎特定提示。
// 归属：docs/ROADMAP-ex1.md §2.6.1、§六 6.2。
// 【红线】读取失败一律 out->available=false + 原因；绝不伪造数据、不写目标进程。

#include "gamestate/ChainSampler.h"
#include "gamestate/IGameMemoryReader.h"
#include "gamestate/IGameStateAdapter.h"

#include <memory>
#include <string>

namespace whalepet::gamestate {

class UnityAdapterBase : public IGameStateAdapter {
public:
    ~UnityAdapterBase() override;

    bool attach(const GameProfile &profile, QString *error) override;
    void detach() override;
    bool attached() const override;
    bool read(core::GameSample *out, QString *error) override;

    bool invalidated() const { return m_sampler != nullptr && m_sampler->invalidated(); }
    int consecutiveFailures() const
    {
        return m_sampler != nullptr ? m_sampler->consecutiveFailures() : 0;
    }
    const std::string &engineId() const { return m_engineId; }

protected:
    // reader 为 nullptr 时使用平台默认只读读取器（Windows: Win32GameMemoryReader）。
    UnityAdapterBase(std::unique_ptr<IGameMemoryReader> reader, std::string engineId,
                     std::string fallbackModule);

    // 引擎特定的「后端不匹配 / 模块缺失」提示（附于失败原因后，便于用户改档）。
    virtual QString backendHint() const = 0;

private:
    std::unique_ptr<IGameMemoryReader> m_reader;
    std::unique_ptr<ChainSampler> m_sampler;
    GameProfile m_profile;
    std::string m_engineId;
    std::string m_fallbackModule;
};

} // namespace whalepet::gamestate
