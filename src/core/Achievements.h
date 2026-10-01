#pragma once

// P4 内容层：**39 项成就的权威定义** —— docs/GAMEPLAY.md §3、docs/ROADMAP-P4.md 任务 1。
//
// 与参考实现（dsh-whale-moe-core.js 的 39 项）的差异，见 docs/ROADMAP-P4.md §设计补充：
//   * 参考实现里有 15 项依赖 DSH 宿主（工具调用数、代码行数、会话消息数、余额告警…），
//     本项目 Out of Scope 不接宿主，这部分**重新设计为本地可判定的指标**
//     （摸肚子/摸尾巴/戳一戳/夸夸/心情满值/饱食满值/任务累计/全勤…）后总数仍为 39。
//   * 小游戏类 7 项本期**只定义、不判定**（`AchMetric::MiniGameReserved`），UI 显示为
//     「敬请期待」并保持灰显；接上小游戏后改一个阈值即可点亮，**不需要改表或迁移**。
//
// 判定统一为「指标 >= 阈值」：所有指标都能从 `AchievementSnapshot` 取到值，
// 因此新增/调整成就只需改本表的阈值，判定逻辑与落库结构都不用动。
// 本文件零 Qt 依赖，可被单测直接覆盖。

#include <cstdint>

namespace whalepet::core {

enum class AchCategory {
    Interaction, // 互动
    Companion,   // 陪伴
    Growth,      // 养成
    Quest,       // 任务
    MiniGame,    // 小游戏（本期预留）
};

enum class AchMetric {
    PatCount,
    BellyCount,
    TailCount,
    FeedCount,
    PraiseCount,
    PokeCount,
    TripleCount,
    CompanionDays,
    StreakDays,
    WeekSigninDays,
    Level,
    BondLevel,
    MoodPeak,
    SatietyPeak,
    NightInteraction,
    Comeback,
    QuestDoneTotal,
    QuestAllToday,
    QuestFullStreak,
    MiniGameReserved,
};

struct AchievementDef {
    const char *id;    // 落库主键，稳定不可改
    const char *name;  // 成就名
    const char *icon;  // 成就墙图标（emoji，与项目「不引入图片资源」约定一致）
    const char *desc;  // 判定条件说明（用户可见）
    AchCategory category;
    AchMetric metric;
    int threshold;
};

// ---- 39 项清单（互动 10 / 陪伴 8 / 养成 8 / 任务 6 / 小游戏 7）------------
inline constexpr AchievementDef kAchievements[] = {
    // 互动类 10
    {"first-pat", "初次摸头", "\xF0\x9F\x91\x8B", "第一次摸摸鲸鱼娘的头", AchCategory::Interaction, AchMetric::PatCount, 1},
    {"ten-pats", "摸头十连", "\xF0\x9F\x96\x90\xEF\xB8\x8F", "累计摸头 10 次", AchCategory::Interaction, AchMetric::PatCount, 10},
    {"hundred-pats", "摸头百连", "\xF0\x9F\x92\xAF", "累计摸头 100 次", AchCategory::Interaction, AchMetric::PatCount, 100},
    {"first-belly", "肚皮初体验", "\xF0\x9F\x90\x8B", "第一次摸摸肚子", AchCategory::Interaction, AchMetric::BellyCount, 1},
    {"first-tail", "尾巴观察员", "\xF0\x9F\x90\x9F", "第一次摸摸尾巴", AchCategory::Interaction, AchMetric::TailCount, 1},
    {"first-feed", "投喂成功", "\xF0\x9F\x8D\xB0", "第一次投喂点心", AchCategory::Interaction, AchMetric::FeedCount, 1},
    {"first-triple", "三连击", "\xF0\x9F\x8E\x89", "触发一次三连击比心", AchCategory::Interaction, AchMetric::TripleCount, 1},
    {"thanks", "嘴甜", "\xF0\x9F\x92\xAC", "夸夸鲸鱼娘 5 次", AchCategory::Interaction, AchMetric::PraiseCount, 5},
    {"praise-10", "甜言蜜语", "\xF0\x9F\x8D\xAC", "累计夸夸 10 次", AchCategory::Interaction, AchMetric::PraiseCount, 10},
    {"poke-10", "戳一戳十连", "\xF0\x9F\x91\x89", "累计戳一下 10 次", AchCategory::Interaction, AchMetric::PokeCount, 10},

    // 陪伴类 8
    {"day1", "一日之缘", "\xF0\x9F\x92\x9E", "累计陪伴满 1 天", AchCategory::Companion, AchMetric::CompanionDays, 1},
    {"day7", "一周相伴", "\xF0\x9F\x92\x8E", "累计陪伴满 7 天", AchCategory::Companion, AchMetric::CompanionDays, 7},
    {"day30", "三十日契约", "\xF0\x9F\x8F\x9B\xEF\xB8\x8F", "累计陪伴满 30 天", AchCategory::Companion, AchMetric::CompanionDays, 30},
    {"signin3", "常客", "\xF0\x9F\x93\x85", "连续签到 3 天", AchCategory::Companion, AchMetric::StreakDays, 3},
    {"signin7", "一周之约", "\xF0\x9F\x97\x93\xEF\xB8\x8F", "连续签到 7 天", AchCategory::Companion, AchMetric::StreakDays, 7},
    {"week-signin7", "周常满勤", "\xF0\x9F\x8F\x86", "本周签到板集满 7 格", AchCategory::Companion, AchMetric::WeekSigninDays, 7},
    {"night-owl", "深夜陪伴", "\xF0\x9F\x8C\x99", "在 22:00–06:00 与鲸鱼娘互动", AchCategory::Companion, AchMetric::NightInteraction, 1},
    {"comeback", "欢迎回来", "\xF0\x9F\x91\x8B", "离开 2 小时后再回来见面", AchCategory::Companion, AchMetric::Comeback, 1},

    // 养成类 8
    {"lv5", "五级伙伴", "\xE2\xAD\x90", "等级达到 5 级", AchCategory::Growth, AchMetric::Level, 5},
    {"lv10", "十级挚友", "\xF0\x9F\x91\x91", "等级达到 10 级", AchCategory::Growth, AchMetric::Level, 10},
    {"lv20", "二十级家人", "\xF0\x9F\x8C\x9F", "等级达到 20 级", AchCategory::Growth, AchMetric::Level, 20},
    {"bond-action", "新动作解锁", "\xF0\x9F\x98\x89", "羁绊达到 Lv3，解锁待机动作（眨眼）", AchCategory::Growth, AchMetric::BondLevel, 3},
    {"bond-badge", "称号首解锁", "\xF0\x9F\x8E\x96\xEF\xB8\x8F", "羁绊达到 Lv5，解锁称号「鲸汐守护者」", AchCategory::Growth, AchMetric::BondLevel, 5},
    {"bond-egg", "彩蛋猎人", "\xF0\x9F\xA5\x9A", "羁绊达到 Lv7，解锁隐藏彩蛋", AchCategory::Growth, AchMetric::BondLevel, 7},
    {"mood-full", "心情满格", "\xF0\x9F\x98\x8A", "心情达到过 100", AchCategory::Growth, AchMetric::MoodPeak, 100},
    {"satiety-full", "吃饱喝足", "\xF0\x9F\x8D\x9A", "饱食达到过 100", AchCategory::Growth, AchMetric::SatietyPeak, 100},

    // 任务类 6
    {"quest-first", "任务初体验", "\xF0\x9F\x8E\xAF", "完成第一个每日任务", AchCategory::Quest, AchMetric::QuestDoneTotal, 1},
    {"quest-all", "一日全勤", "\xF0\x9F\x8E\x9F\xEF\xB8\x8F", "单日 3 个任务全部领取", AchCategory::Quest, AchMetric::QuestAllToday, 1},
    {"quest-10", "任务十连", "\xF0\x9F\x94\x9F", "累计完成 10 个每日任务", AchCategory::Quest, AchMetric::QuestDoneTotal, 10},
    {"quest-50", "任务五十连", "\xF0\x9F\x8F\x85", "累计完成 50 个每日任务", AchCategory::Quest, AchMetric::QuestDoneTotal, 50},
    {"quest-100", "任务百连", "\xF0\x9F\x8F\xB5\xEF\xB8\x8F", "累计完成 100 个每日任务", AchCategory::Quest, AchMetric::QuestDoneTotal, 100},
    {"quest-all-7", "全勤一周", "\xF0\x9F\x93\x86", "连续 7 天完成全部每日任务", AchCategory::Quest, AchMetric::QuestFullStreak, 7},

    // 小游戏类 7（本期预留：不可判定，UI 灰显「敬请期待」）
    {"game-first", "初次开玩", "\xF0\x9F\xAB\xA7", "第一次玩小游戏", AchCategory::MiniGame, AchMetric::MiniGameReserved, 1},
    {"game-win", "泡泡之王", "\xF0\x9F\x91\x91", "单局得分达到 300", AchCategory::MiniGame, AchMetric::MiniGameReserved, 300},
    {"game-combo10", "连击达人", "\xF0\x9F\x94\xA5", "单局最高连击 10", AchCategory::MiniGame, AchMetric::MiniGameReserved, 10},
    {"game-highscore", "纪录刷新", "\xF0\x9F\x8F\x86", "刷新个人最高分", AchCategory::MiniGame, AchMetric::MiniGameReserved, 1},
    {"game-play10", "十局纪念", "\xF0\x9F\x8E\xAE", "累计玩满 10 局", AchCategory::MiniGame, AchMetric::MiniGameReserved, 10},
    {"game-perfect", "零失误", "\xF0\x9F\x8C\x88", "一局内没有失误", AchCategory::MiniGame, AchMetric::MiniGameReserved, 1},
    {"game-daily3", "三局全清", "\xE2\x9C\xA8", "单日完成 3 局", AchCategory::MiniGame, AchMetric::MiniGameReserved, 3},
};

inline constexpr int kAchievementCount =
    static_cast<int>(sizeof(kAchievements) / sizeof(kAchievements[0]));

inline constexpr const char *kAchCategoryNames[] = {"互动", "陪伴", "养成", "任务", "小游戏"};

inline int achCategoryCount(AchCategory category)
{
    int n = 0;
    for (const AchievementDef &d : kAchievements) {
        if (d.category == category) {
            ++n;
        }
    }
    return n;
}

// ---- 判定快照 -------------------------------------------------------------
// 计数器型指标来自 meta 表（`stat.*` 键），状态型指标来自 pet_state / signin / quests 表。
struct AchievementSnapshot {
    int patCount = 0;
    int bellyCount = 0;
    int tailCount = 0;
    int feedCount = 0;
    int praiseCount = 0;
    int pokeCount = 0;
    int tripleCount = 0;
    int companionDays = 0;
    int streakDays = 0;
    int weekSigninDays = 0;
    int level = 1;
    int bondLevel = 1;
    int moodPeak = 0;
    int satietyPeak = 0;
    int nightInteraction = 0;
    int comeback = 0;
    int questDoneTotal = 0;
    int questAllToday = 0;
    int questFullStreak = 0;

    int valueFor(AchMetric metric) const
    {
        switch (metric) {
        case AchMetric::PatCount: return patCount;
        case AchMetric::BellyCount: return bellyCount;
        case AchMetric::TailCount: return tailCount;
        case AchMetric::FeedCount: return feedCount;
        case AchMetric::PraiseCount: return praiseCount;
        case AchMetric::PokeCount: return pokeCount;
        case AchMetric::TripleCount: return tripleCount;
        case AchMetric::CompanionDays: return companionDays;
        case AchMetric::StreakDays: return streakDays;
        case AchMetric::WeekSigninDays: return weekSigninDays;
        case AchMetric::Level: return level;
        case AchMetric::BondLevel: return bondLevel;
        case AchMetric::MoodPeak: return moodPeak;
        case AchMetric::SatietyPeak: return satietyPeak;
        case AchMetric::NightInteraction: return nightInteraction;
        case AchMetric::Comeback: return comeback;
        case AchMetric::QuestDoneTotal: return questDoneTotal;
        case AchMetric::QuestAllToday: return questAllToday;
        case AchMetric::QuestFullStreak: return questFullStreak;
        case AchMetric::MiniGameReserved: return 0;
        }
        return 0;
    }
};

inline bool achievementReached(const AchievementDef &def, const AchievementSnapshot &snapshot)
{
    if (def.metric == AchMetric::MiniGameReserved) {
        return false; // 本期预留，永不判定
    }
    return snapshot.valueFor(def.metric) >= def.threshold;
}

// 成就墙进度（已解锁 / 总数）
inline int achProgress(const AchievementSnapshot &snapshot, int *unlockedOut)
{
    int unlocked = 0;
    for (const AchievementDef &d : kAchievements) {
        if (achievementReached(d, snapshot)) {
            ++unlocked;
        }
    }
    if (unlockedOut != nullptr) {
        *unlockedOut = unlocked;
    }
    return kAchievementCount;
}

inline constexpr std::int64_t kCompanionMsPerDay = 86400000;

inline int companionDaysFromMs(std::int64_t ms)
{
    return static_cast<int>(ms / kCompanionMsPerDay);
}

// 计数器型指标对应的 meta 键；返回 nullptr 表示该指标不是计数器（由状态推导）
inline const char *achStatKey(AchMetric metric)
{
    switch (metric) {
    case AchMetric::PatCount: return "stat.pat";
    case AchMetric::BellyCount: return "stat.belly";
    case AchMetric::TailCount: return "stat.tail";
    case AchMetric::FeedCount: return "stat.feed";
    case AchMetric::PraiseCount: return "stat.praise";
    case AchMetric::PokeCount: return "stat.poke";
    case AchMetric::TripleCount: return "stat.triple";
    case AchMetric::MoodPeak: return "stat.mood_peak";
    case AchMetric::SatietyPeak: return "stat.satiety_peak";
    case AchMetric::NightInteraction: return "stat.night_interaction";
    case AchMetric::Comeback: return "stat.comeback";
    case AchMetric::QuestDoneTotal: return "stat.quest_done";
    case AchMetric::QuestAllToday: return "stat.quest_allday";
    case AchMetric::QuestFullStreak: return "stat.quest_fullstreak";
    case AchMetric::CompanionDays:
    case AchMetric::StreakDays:
    case AchMetric::WeekSigninDays:
    case AchMetric::Level:
    case AchMetric::BondLevel:
    case AchMetric::MiniGameReserved:
        return nullptr;
    }
    return nullptr;
}

// 「全勤」判定的统计口径：当日 3 槽全部领取 → questAllToday + 1 且 quest_fullstreak + 1，
// 否则 quest_fullstreak 归零（任务槽位数 kQuestSlotCount 定义在 core/Quests.h，
// 统计更新入口为 AchievementService::reportQuestFullDay）。

} // namespace whalepet::core
