#pragma once

// 有界指针链解析 + 逐跳校验 + 字节预算 + 连续失败失效自检。
// 归属：docs/ROADMAP-ex1.md §2.6.2.1、§4.3（异常即退回，不伪造）、§六 6.2。

#include "gamestate/GameProfile.h"
#include "gamestate/IGameMemoryReader.h"

#include <QString>

#include <cstdint>
#include <string>

namespace whalepet::gamestate {

inline constexpr int kGameInvalidateAfterFailures = 3;   // 连续失败 → 判定档案失效
inline constexpr std::size_t kGameUtf16MaxCodeUnits = 64; // utf16 字段单次读取上限

// 一个字段的读取结果（按 kind 取用对应成员）
struct GameFieldValue {
    double number = 0.0;
    long long integer = 0;
    bool boolean = false;
    std::string text; // utf16 → UTF-8
};

class PointerChainResolver {
public:
    explicit PointerChainResolver(IGameMemoryReader *reader = nullptr);

    void setReader(IGameMemoryReader *reader) { m_reader = reader; }
    void setMaxJumps(int maxJumps) { m_maxJumps = maxJumps; }
    void setMaxBytesPerRound(std::uint64_t bytes) { m_maxBytesPerRound = bytes; }
    int maxJumps() const { return m_maxJumps; }

    // staticRoot = moduleBase + moduleBaseOffset（由调用方算好，便于脱系统单测）
    bool resolve(const GameFieldSpec &field, std::uint64_t staticRoot, std::uint64_t *outAddress,
                 QString *error) const;

    // 解析并读取：链上任一跳失败 / 超出字节预算 → false（不返回陈旧数据）
    bool readField(const GameFieldSpec &field, std::uint64_t staticRoot, GameFieldValue *out,
                   QString *error);

    // 魔数校验：base = moduleBase + moduleBaseOffset；未配置魔数时返回 true
    bool verifyMagic(std::uint64_t base, const GameProfile::Validation &validation,
                     QString *error) const;

    // 连续失败失效自检（§4.3）
    int consecutiveFailures() const { return m_failures; }
    void noteFailure() { ++m_failures; }
    void noteSuccess() { m_failures = 0; }
    bool invalidated() const { return m_failures >= kGameInvalidateAfterFailures; }
    void resetFailures() { m_failures = 0; }

    // 每轮开始重置字节预算（防止单轮读取失控）
    void resetRoundBudget() { m_bytesThisRound = 0; }
    std::uint64_t bytesThisRound() const { return m_bytesThisRound; }

private:
    // const：resolve() 为契约要求的 const 方法，这两处只累加「本轮字节预算」，
    // 因此 m_bytesThisRound 声明为 mutable（无其它可变状态）。
    bool readRaw(std::uint64_t address, void *buffer, std::size_t size, QString *error) const;
    bool readPointer(std::uint64_t address, std::uint64_t *out, QString *error) const;

    IGameMemoryReader *m_reader = nullptr;
    int m_maxJumps = 4;
    std::uint64_t m_maxBytesPerRound = 4096;
    mutable std::uint64_t m_bytesThisRound = 0;
    int m_failures = 0;
};

} // namespace whalepet::gamestate
