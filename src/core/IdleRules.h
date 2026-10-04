#pragma once

// 闲置与特殊常驻立绘规则（2026-10-04 立绘激活需求）：
// 统一收纳「日间待机小剧场池 / 长时间待机睡眠循环 / 满值特殊立绘 / 一次性表现」的
// 常量与纯函数，供 core::PetStateMachine 使用。
//
// 设计约束：零 Qt 依赖（仅标准库），可脱界面单测。
//
// 需求映射（编号见 docs/POSE-ASSETS.md 附录 A）：
//   日间待机池：1 daily-coffee / 2 daily-cooking / 4 daily-eat / 5 daily-fishing /
//               6 daily-painting / 7 daily-picnic / 8 daily-shower / 10 cool-shades /
//               11 meme-music / 14 wink（wink 需好感度 >= 5000）
//   睡眠循环  ：19 sleep（待机 20min）→ 9 daily-stretch（5s）循环
//   满值常驻  ：13 tail-swing（心情 & 饱腹同时 100）
//   一次性    ：3 daily-done（每日任务完成，5s）/ 15 celebrate（每日 3 局，10s）/
//               16 failure（工作报错，6s）

#include <cstddef>
#include <cstdint>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 日间待机小剧场池（日间 07:00–17:59 静息时，每隔 15s 随机播一张，维持 3s）
// ---------------------------------------------------------------------------

inline constexpr std::int64_t kIdlePoolIntervalMs = 15000;
inline constexpr std::int64_t kIdlePoolHoldMs = 3000;

// wink（14）仅在好感度 >= 5000 时参与随机；其余 9 张无条件参与。
inline constexpr int kWinkAffinityThreshold = 5000;
inline constexpr const char *kWinkPose = "wink";

// 池成员顺序即随机索引顺序；**wink 必须固定在末尾**（按 eligibleCount 截断实现门槛）。
inline constexpr const char *const kIdlePoolPoses[] = {
    "daily-coffee", "daily-cooking", "daily-eat",  "daily-fishing", "daily-painting",
    "daily-picnic", "daily-shower",  "cool-shades", "meme-music",   "wink",
};
inline constexpr std::size_t kIdlePoolPoseCount =
    sizeof(kIdlePoolPoses) / sizeof(kIdlePoolPoses[0]);
inline constexpr std::size_t kIdlePoolBaseCount = kIdlePoolPoseCount - 1; // 不含 wink

// 当前好感度下可参与的池大小（wink 在末尾，故门槛不达标时直接少一个）。
inline std::size_t idlePoolEligibleCount(int affinity)
{
    return (affinity >= kWinkAffinityThreshold) ? kIdlePoolPoseCount : kIdlePoolBaseCount;
}

// ---------------------------------------------------------------------------
// 长时间待机睡眠循环（日间 / 傍晚，待机 > 20min；进入深夜时段自动停止）
// ---------------------------------------------------------------------------

inline constexpr std::int64_t kSleepIdleMs = 20 * 60 * 1000;  // 20min 无输入
inline constexpr std::int64_t kSleepHoldMs = 10 * 60 * 1000;  // 睡 10min
inline constexpr std::int64_t kSleepStretchHoldMs = 5000;     // 醒了伸懒腰 5s
inline constexpr const char *kSleepPose = "sleep";
inline constexpr const char *kSleepStretchPose = "daily-stretch";

// ---------------------------------------------------------------------------
// 特殊持续常驻
// ---------------------------------------------------------------------------

// 心情与饱腹同时满值 → 持续摇尾巴，直到任一不为满值
inline constexpr const char *kVitalsFullPose = "tail-swing";

// ---------------------------------------------------------------------------
// 深夜独立阶段（2026-10-04）：23:00–06:59 不参与任何随机立绘池
// ---------------------------------------------------------------------------
// 深夜立绘是**封闭集合**，只由时段态决定：
//   - 空闲            → evening/late-night 常驻（DaySlotRules::daySlotPoseOf → daily-pajama）
//   - 交互唤醒窗口内  → night
//   - 点击累计达阈值  → meme-smile-pain（虚弱，保持 kLateNightWeakHoldMs）
//   - 点击瞬间        → react-*（既有一次性反馈）
// 因此深夜**不**发生：日间待机小剧场池、睡眠循环、逗弄（teasing）、满值摇尾（tail-swing）。
// 常驻优先级（PetStateMachine::contextPose）：工作态 > 睡眠循环 > 时段态 > 满值 > 挂机 > 游戏 > 静息，
// 即**时段态优先于满值常驻** —— 深夜与傍晚不会被 tail-swing 顶掉。

inline constexpr int kLateNightWeakClickCount = 10;                        // 深夜点击次数阈值
inline constexpr std::int64_t kLateNightWeakHoldMs = 20 * 1000;            // 虚弱立绘保持 20s
inline constexpr const char *kLateNightWeakPose = "meme-smile-pain";       // 虚弱立绘
inline constexpr const char *kLateNightWeakScene = "click.latenight.weak"; // 虚弱台词场景

// 完成全部成就时的显示立绘（一次性表现，维持 8s 后回落常驻）
inline constexpr const char *kAllAchievedPose = "meme-smug";
inline constexpr std::int64_t kAllAchievedTtlMs = 8000;

// ---------------------------------------------------------------------------
// 一次性表现
// ---------------------------------------------------------------------------

inline constexpr const char *kDailyDonePose = "daily-done";  // 每日任务完成
inline constexpr std::int64_t kDailyDoneTtlMs = 5000;

inline constexpr const char *kCelebratePose = "celebrate";  // 每日 3 局游戏完成
inline constexpr std::int64_t kCelebrateTtlMs = 10000;

inline constexpr const char *kWorkErrorPose = "failure";  // 工作侧报错（ACP/宿主显式信号）
inline constexpr std::int64_t kWorkErrorTtlMs = 6000;

// 分时问候立绘（17 greet）
inline constexpr const char *kGreetPose = "greet";

} // namespace whalepet::core
