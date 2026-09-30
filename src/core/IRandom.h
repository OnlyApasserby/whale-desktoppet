#pragma once

// 可注入随机源：概率事件必须可测（固定序列），因此不直接调用 std::random。

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace whalepet::core {

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual double next01() = 0;         // [0, 1)
    virtual int nextInt(int bound) = 0;  // [0, bound)
};

// 生产用实现：std::mt19937
class SystemRandom final : public IRandom {
public:
    SystemRandom() : m_engine(std::random_device{}()) {}
    explicit SystemRandom(std::uint32_t seed) : m_engine(seed) {}

    double next01() override
    {
        return std::uniform_real_distribution<double>(0.0, 1.0)(m_engine);
    }

    int nextInt(int bound) override
    {
        if (bound <= 0) {
            return 0;
        }
        return std::uniform_int_distribution<int>(0, bound - 1)(m_engine);
    }

private:
    std::mt19937 m_engine;
};

// 测试用实现：固定序列（外部按需喂值）
class ScriptedRandom final : public IRandom {
public:
    std::vector<double> values;
    std::size_t cursor = 0;

    explicit ScriptedRandom(std::vector<double> seq) : values(std::move(seq)) {}

    double next01() override
    {
        if (values.empty()) {
            return 1.0; // 默认「不触发」，保证确定性
        }
        const double v = values[cursor % values.size()];
        ++cursor;
        return v;
    }

    int nextInt(int bound) override
    {
        if (bound <= 0) {
            return 0;
        }
        return static_cast<int>(next01() * bound) % bound;
    }
};

} // namespace whalepet::core
