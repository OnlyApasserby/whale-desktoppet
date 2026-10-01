#pragma once

// P4 内容层：每日任务池 —— docs/GAMEPLAY.md §5、docs/ROADMAP-P4.md 任务 2。
//
// 与参考实现（dsh-whale-moe-core.js 的 QUEST_POOL，6 条里 4 条依赖 DSH 宿主）的差异：
// 本项目不接宿主，故任务池**重新设计为 6 条纯互动类**（1 条每日固定 + 5 条抽签），
// 每天 3 槽 = 固定 1 + 随机 2；抽签以 dayKey 为种子，**同一天重启后槽位不变**。
//
// 奖励只发 mood/affinity（本项目 P4 不做经济系统，coins 未启用）。
// 本文件零 Qt 依赖，可被单测直接覆盖。

#include <cstdint>

namespace whalepet::core {

inline constexpr int kQuestSlotCount = 3;

enum class QuestMetric {
    Signin,
    Pat,
    Belly,
    Tail,
    Poke,
    Feed,
    Praise,
    Triple,
};

struct QuestDef {
    const char *id;    // 落库主键（每天重写 quests 表，id 只在当天内唯一即可）
    const char *name;
    const char *desc;
    QuestMetric metric;
    int target;
    int rewardMood;
    int rewardAffinity;
    bool always;       // true = 每天固定占 slot 0
};

inline constexpr QuestDef kQuestPool[] = {
    {"signin-1", "今日签到", "完成今天的签到", QuestMetric::Signin, 1, 5, 6, true},
    {"pat-3", "摸摸头", "摸头 3 次", QuestMetric::Pat, 3, 4, 8, false},
    {"belly-2", "摸摸肚子", "摸摸肚子 2 次", QuestMetric::Belly, 2, 5, 8, false},
    {"feed-1", "投喂点心", "投喂 1 次点心", QuestMetric::Feed, 1, 4, 6, false},
    {"praise-2", "多夸夸我", "夸夸鲸鱼娘 2 次", QuestMetric::Praise, 2, 5, 8, false},
    {"poke-2", "戳一戳", "戳一下鲸鱼娘 2 次", QuestMetric::Poke, 2, 4, 6, false},
};

inline constexpr int kQuestPoolSize =
    static_cast<int>(sizeof(kQuestPool) / sizeof(kQuestPool[0]));

inline const QuestDef *findQuest(const char *id)
{
    if (id == nullptr) {
        return nullptr;
    }
    for (const QuestDef &d : kQuestPool) {
        const char *a = d.id;
        const char *b = id;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') {
            return &d;
        }
    }
    return nullptr;
}

// 任务池里标记为「每天固定」的一条（约定有且仅有一条；缺失时返回 nullptr）
inline const QuestDef *alwaysQuest()
{
    for (const QuestDef &d : kQuestPool) {
        if (d.always) {
            return &d;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 每日抽签：以 dayKey 为种子的确定性随机（同一自然日重启后槽位不变，
// 因此不需要把「今天抽了哪几条」额外落库；换天自动换签）。
// ---------------------------------------------------------------------------

inline std::uint32_t nextQuestRand(std::uint32_t &state)
{
    state = state * 1664525u + 1013904223u; // 线性同余，仅用于「看起来随机」，不用于加密
    return state;
}

inline std::uint32_t seedFromDayKey(const char *key)
{
    std::uint32_t state = 2166136261u; // FNV-1a
    for (const char *p = key; p != nullptr && *p != '\0'; ++p) {
        state ^= static_cast<std::uint32_t>(static_cast<unsigned char>(*p));
        state *= 16777619u;
    }
    return state == 0u ? 1u : state;
}

// 选出今天的槽位（always 项固定在 slot 0，其余按 key 抽签），返回实际写入条数
inline int pickDailyQuests(const char *key, const QuestDef **out, int maxOut)
{
    if (key == nullptr || out == nullptr || maxOut <= 0) {
        return 0;
    }

    int n = 0;
    if (const QuestDef *fixed = alwaysQuest(); fixed != nullptr && n < maxOut) {
        out[n++] = fixed;
    }

    const QuestDef *pool[kQuestPoolSize];
    int poolSize = 0;
    for (const QuestDef &d : kQuestPool) {
        if (!d.always) {
            pool[poolSize++] = &d;
        }
    }

    std::uint32_t state = seedFromDayKey(key);
    const int need = maxOut - n;
    for (int i = 0; i < need && i < poolSize; ++i) {
        const std::uint32_t r = nextQuestRand(state) % static_cast<std::uint32_t>(poolSize - i);
        const int j = i + static_cast<int>(r);
        const QuestDef *tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
        out[n++] = pool[i];
    }
    return n;
}

} // namespace whalepet::core
