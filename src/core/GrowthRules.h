#pragma once

// 养成纯规则（**零 Qt 依赖**，可脱 UI 单测）——见 docs/ROADMAP-P3.md「P3 设计补充」。
//
// 来源：whale 参考实现 referances/dsh-whale-musume/assets/whale-moe-core.js
//   - GROWTH / DEFAULT_GROWTH              : :475-484
//   - applyGrowth 的增量表与夹取            : :542-573
//   - level 由累计值推导 / LEVEL_STEP       : :574-575
//   - 羁绊解锁阈值 BOND                     : :616-624, :716-724
//   - dayKey / 跨天签到                     : :528-531, :553-562
//
// 本文件只做「数值与曲线」，不碰数据库、不碰 Qt。

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 常量（沿用 whale 取值，禁止在别处再写字面量）
// ---------------------------------------------------------------------------

// 上限：whale GROWTH (:475-478)
inline constexpr int kMoodMax = 100;
inline constexpr int kSatietyMax = 100;
inline constexpr int kAffinityMax = 10000;

// 升级曲线：whale LEVEL_STEP = 500（每 500 累计经验升 1 级）
inline constexpr int kLevelStep = 500;

// 饱食衰减：whale SATIETY_DECAY_PER_MIN = 0.15（点/分钟）
// 实现为**整数等价形式**：每 kMsPerSatietyPoint 毫秒掉 1 点，避免 0.15/分钟的
// 小数量在「整数落库」时被反复取整而永久丢失（DATA-MODEL 的 satiety 为 INTEGER）。
// 60000 / 0.15 = 400000 ms。
inline constexpr std::int64_t kMsPerSatietyPoint = 400000;
// 养成结算 tick：whale 每 60000ms 结算一次（dsh-whale-moe.js:2134-2137）
inline constexpr std::int64_t kGrowthTickMs = 60000;

// 初始值：whale DEFAULT_GROWTH (:480-484) + 本项目补充项
inline constexpr int kDefaultMood = 70;
inline constexpr int kDefaultSatiety = 80;
inline constexpr int kDefaultAffinity = 0;
inline constexpr int kDefaultExp = 0;
inline constexpr int kDefaultLevel = 1;
inline constexpr int kDefaultBondLevel = 1;
inline constexpr int kDefaultStreakDays = 0;

// 羁绊解锁阈值（whale BOND.lv3Action / lv5Badge / lv7Egg）
inline constexpr int kBondLevelAction = 3;
inline constexpr int kBondLevelBadge = 5;
inline constexpr int kBondLevelEgg = 7;

inline constexpr std::int64_t kMsPerDay = 86400000;

// ---------------------------------------------------------------------------
// 交互 → 数值增量
// ---------------------------------------------------------------------------

// 与 whale applyGrowth(type) 一一对应（本项目去掉宿主相关项）
enum class Interaction {
    Pat,     // 摸头（分区 head）
    Belly,   // 摸肚子
    Tail,    // 戳尾巴
    Poke,    // 戳一下（菜单）
    Feed,    // 投喂（菜单）
    Praise,  // 夸夸（菜单）
    Triple,  // 三连击
    Signin   // 每日签到（每天首次）
};

struct GrowthDelta {
    int mood = 0;
    int affinity = 0;   // 同时作为 exp 增量累加（见 ROADMAP-P3 §1）
    int satiety = 0;
};

// 一次交互产生的增量（whale whale-moe-core.js:542-569）
inline GrowthDelta deltaFor(Interaction type)
{
    GrowthDelta d;
    switch (type) {
    case Interaction::Pat:    d.mood = 4;  d.affinity = 2;  break;
    case Interaction::Belly:  d.mood = 3;  d.affinity = 2;  break;
    case Interaction::Tail:   d.mood = 2;  d.affinity = 3;  break;
    case Interaction::Poke:   d.mood = -6;                 break;
    case Interaction::Feed:   d.mood = 3;  d.affinity = 5; d.satiety = 30; break;
    case Interaction::Praise: d.mood = 5;  d.affinity = 8;  break;
    case Interaction::Triple: d.mood = 10; d.affinity = 10; break;
    case Interaction::Signin: d.mood = 5;                  break;
    }
    return d;
}

// ---------------------------------------------------------------------------
// 小游戏（扫雷）结算奖励
// ---------------------------------------------------------------------------
//
// 照搬参考项目 applyGrowth 的 game-* 分支（whale-moe-core.js:563-566）与
// GAME.REWARDS_PER_DAY = 3（:251-258，见 dsh-whale-moe.js settleGame:1056-1106）。
// 档位判定（通关 / 及格 / 失败）由 core::MineGrade 给出；此处只放数值，禁止在别处再写字面量。

inline constexpr int kGameWinMood = 8;            // game-win: mood +8
inline constexpr int kGameWinAffinity = 12;       // game-win: affinity +12
inline constexpr int kGameDrawMood = 2;           // game-draw: mood +2
inline constexpr int kGameDrawAffinity = 3;       // game-draw: affinity +3
inline constexpr int kGameLoseMood = -3;          // game-lose: mood -3
inline constexpr int kGameHighScoreAffinity = 5;  // high-score: affinity +5（刷新个人纪录）
inline constexpr int kGameRewardsPerDay = 3;      // 每日最多 3 局计入养成，超出只计分

// ---------------------------------------------------------------------------
// 夹取（whale whale-moe-core.js:571-573）
// ---------------------------------------------------------------------------

inline int clampMood(int v)
{
    return v < 0 ? 0 : (v > kMoodMax ? kMoodMax : v);
}

inline int clampSatiety(int v)
{
    return v < 0 ? 0 : (v > kSatietyMax ? kSatietyMax : v);
}

inline int clampAffinity(int v)
{
    return v < 0 ? 0 : (v > kAffinityMax ? kAffinityMax : v);
}

// ---------------------------------------------------------------------------
// 等级 / 升级曲线
// ---------------------------------------------------------------------------
//
// whale：level = max(1, floor(affinity / LEVEL_STEP) + 1)
// 本项目把「累计值」显式命名为 exp（DATA-MODEL 的 pet_state.exp），公式不变。

inline int levelForExp(int exp)
{
    const int e = exp < 0 ? 0 : exp;
    return e / kLevelStep + 1;
}

// 升到 level+1 所需的**累计**经验：对 level 单调递增（0, 500, 1000, ...）
inline int expNeeded(int level)
{
    const int lv = level < 1 ? 1 : level;
    return kLevelStep * lv;
}

// 当前等级区间的长度（本实现恒为 kLevelStep；若改为递增阶梯只改这里）
// 注意：不能用 expNeeded(lv) - expNeeded(lv - 1)，expNeeded 会把 level < 1 夹到 1，
// 导致 expSpanForLevel(1) 退化为 0。
inline int expSpanForLevel(int level)
{
    const int lv = level < 1 ? 1 : level;
    return expNeeded(lv) - kLevelStep * (lv - 1);
}

// 当前等级内已积累的经验（用于进度条）
inline int expInLevel(int exp)
{
    const int e = exp < 0 ? 0 : exp;
    return e - (levelForExp(e) - 1) * kLevelStep;
}

// ---------------------------------------------------------------------------
// 羁绊（whale bondUnlocks(:716-724)：等级 >= 3 / 5 / 7）
// ---------------------------------------------------------------------------

struct BondUnlocks {
    bool action = false;   // Lv3 新待机动作
    bool badge = false;    // Lv5 称号「鲸汐守护者」
    bool egg = false;      // Lv7 隐藏彩蛋
};

inline BondUnlocks bondUnlocks(int level)
{
    const int lv = level < 1 ? 1 : level;
    BondUnlocks u;
    u.action = lv >= kBondLevelAction;
    u.badge = lv >= kBondLevelBadge;
    u.egg = lv >= kBondLevelEgg;
    return u;
}

// ---------------------------------------------------------------------------
// 自然日 key（whale dayKey(:528-531)：`年-月-日`，月从 1 计）
// ---------------------------------------------------------------------------

inline std::string dayKey(std::int64_t nowMs)
{
    const std::time_t t = static_cast<std::time_t>(nowMs / 1000);
    std::tm tmv{};
#if defined(_MSC_VER)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%d-%d-%d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

inline std::string previousDayKey(std::int64_t nowMs)
{
    return dayKey(nowMs - kMsPerDay);
}

// 跨天连续签到（whale whale-moe-core.js:553-562）：
// 上次签到日 == 今天 → 幂等返回原值（sameDay 置 true）；
// 上次签到日 == 昨天 → +1；否则重置为 1。
inline int nextStreakDays(int currentStreak, const std::string &lastSigninDay, std::int64_t nowMs,
                          bool *sameDay = nullptr)
{
    const std::string today = dayKey(nowMs);
    const bool already = (lastSigninDay == today);
    if (sameDay != nullptr) {
        *sameDay = already;
    }
    if (already) {
        return currentStreak < 0 ? 0 : currentStreak;
    }
    if (lastSigninDay == previousDayKey(nowMs)) {
        return (currentStreak < 0 ? 0 : currentStreak) + 1;
    }
    return 1;
}

} // namespace whalepet::core
