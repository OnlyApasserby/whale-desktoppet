#pragma once

// pet_state（单例）行数据 —— 字段与 docs/DATA-MODEL.md §3.2 一一对应。
// 纯数据结构，不含任何行为，便于在测试与 Service 之间传递。

#include <cstdint>

namespace whalepet::model {

struct PetStateData {
    int level = 1;
    int exp = 0;
    int coins = 0;
    int mood = 70;
    int affinity = 0;
    int satiety = 80;
    int bondLevel = 1;
    std::int64_t companionMs = 0;
    int streakDays = 0;
    std::int64_t lastActiveMs = 0;
    std::int64_t updatedMs = 0;
};

} // namespace whalepet::model
