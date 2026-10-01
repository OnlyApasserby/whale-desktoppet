#pragma once

// P4 内容层：周签到里程碑 —— docs/GAMEPLAY.md §4「周签到」、docs/ROADMAP-P4.md 任务 3。
//
// 一周 7 格（周一~周日），每天可签一次；集满 1 / 3 / 7 天各发一次里程碑奖励。
// 奖励只发 mood/affinity（P4 不启用 coins 经济）。
//
// `bit` 与 model::SigninRepo 的 SigninRewardBit 位图一一对应；core 层零 Qt，
// 因此这里用字面量并在注释中标注对应关系，不在 core 里依赖 model。

namespace whalepet::core {

struct SigninMilestone {
    int days;     // 本周已签天数达到该值即发放
    int mood;     // 奖励心情
    int affinity; // 奖励好感（同时按 GrowthRules 累加为经验）
    int bit;      // 对应 model::SigninRewardBit（0x1 / 0x2 / 0x4）
};

inline constexpr SigninMilestone kSigninMilestones[] = {
    {1, 2, 10, 0x1},  // 1 天：小奖励，保证每天签到都有即时反馈
    {3, 10, 30, 0x2}, // 3 天
    {7, 20, 60, 0x4}, // 7 天全勤
};

inline constexpr int kSigninMilestoneCount =
    static_cast<int>(sizeof(kSigninMilestones) / sizeof(kSigninMilestones[0]));

inline constexpr int kSigninWeekDays = 7;

} // namespace whalepet::core
