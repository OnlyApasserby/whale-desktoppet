#pragma once

// 预设对话的**独立立绘池**（P8，docs/DIALOGUE.md §3）。
//
// 与常驻立绘（时段 / 工作 / 节日）完全分离：问答期间由问答立绘接管，
// QA 结束后一次性姿态到期即回到上下文常驻立绘。
//
//   | 问题类别              | 提问立绘池                          | 资源                |
//   |-----------------------|-------------------------------------|---------------------|
//   | 日常 normal           | 通用好奇（单张）                    | curious             |
//   | 敏感 / 私密 sensitive | meme-broke / meme-cry / meme-heart  | 既有资产，无新增    |
//   | 选择 choice           | meme-no / meme-yes                  | 既有资产，无新增    |
//   | 天气 weather          | 由 core/WeatherRules.h 按类型决定   | weather-* / curious |
//
// 零 Qt 依赖。

#include "core/IRandom.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace whalepet::core {

// 敏感 / 私密问题池（3 张）
inline const char *const *dialogueSensitivePoses(std::size_t &count)
{
    static const char *const kPoses[] = { "meme-broke", "meme-cry", "meme-heart" };
    count = sizeof(kPoses) / sizeof(kPoses[0]);
    return kPoses;
}

// 选择问题池（2 张）
inline const char *const *dialogueChoicePoses(std::size_t &count)
{
    static const char *const kPoses[] = { "meme-no", "meme-yes" };
    count = sizeof(kPoses) / sizeof(kPoses[0]);
    return kPoses;
}

// 日常问题提问立绘
inline constexpr const char *kDialogueNormalPose = "curious";

// ---------------------------------------------------------------------------
// **按题立绘池**（DIALOGUE-CORPUS 扩充，2026-10-10）：对扩充语料（普通 100 题 /
// 私密 15 题，assets/lines/dialogue.txt）逐题登记建议表情，最多 3 张；取用时随机
// 且避免与上一张连号（与类别池共用 `pickDialoguePose` 机制）。
//
// 未登记的题目（8 道操作指引 / 事实说明题：q-engine / q-hotkey / q-contextapi /
// q-settings / q-backup / q-weather-config / q-api-purpose / q-offline）返回 0 →
// 由调用方回落上面的类别池（normal → kDialogueNormalPose 等），既有 11 题行为不变。
//
// 表内 id 与语料逐字一致；test_line_table 的 bundledDialogueQuestionsHavePosePools
// 做双向校验（语料每题必有立绘兜底、池内 id 必须存在于语料）。
// ---------------------------------------------------------------------------
struct DialoguePoseRule {
    const char *id;
    const char *p0;
    const char *p1 = nullptr;
    const char *p2 = nullptr;
};

inline constexpr DialoguePoseRule kDialoguePoseRules[] = {
    { "q-name", "blush", "wink" },
    { "q-species", "wink", "meme-smug" },
    { "q-age", "meme-smug", "blush" },
    { "q-from", "meme-heart", "curious" },
    { "q-weakness", "star", "blush" },
    { "q-hate", "angry", "meme-smile-pain" },
    { "q-good-at", "meme-doge", "star" },
    { "q-day", "afk", "tail-swing" },
    { "q-secret-skill", "meme-peace", "curious" },
    { "q-fishsnack", "daily-eat", "meme-heart" },
    { "q-pat-head", "react-head" },
    { "q-pat-belly", "react-belly" },
    { "q-pat-tail", "react-tail" },
    { "q-triple", "star" },
    { "q-drag", "pick-up" },
    { "q-edge", "home-peek" },
    { "q-feed", "eat" },
    { "q-praise", "meme-worship" },
    { "q-home", "waiting" },
    { "q-hide", "afk" },
    { "q-level", "levelup" },
    { "q-exp", "thinking" },
    { "q-mood-meter", "meme-smile-pain" },
    { "q-affinity", "meme-heart" },
    { "q-hunger", "meme-cry" },
    { "q-bond", "celebrate" },
    { "q-signin", "achievement" },
    { "q-together", "meme-kyun" },
    { "q-achievement", "success" },
    { "q-diary", "daily-painting" },
    { "q-mine", "game-think", "game-lose" },
    { "q-kitten", "meme-wakuwaku" },
    { "q-chess", "game-cheat", "game-think" },
    { "q-token", "daily-picnic" },
    { "q-rice", "sleep" },
    { "q-daily3", "bold" },
    { "q-reward", "game-win" },
    { "q-difficulty", "cool-shades" },
    { "q-cheat", "teasing" },
    { "q-work", "running" },
    { "q-workstate", "meme-peace" },
    { "q-coding", "running" },
    { "q-hotword", "success" },
    { "q-debug", "work-debug" },
    { "q-meeting", "work-meeting" },
    { "q-idle-work", "work-slack" },
    { "q-acp", "work-idea" },
    { "q-morning", "greet" },
    { "q-night", "night" },
    { "q-outdoor", "weather-umbrella" },
    { "q-eat", "daily-eat" },
    { "q-water", "daily-coffee" },
    { "q-sit", "daily-stretch" },
    { "q-latenight-work", "work-deadline" },
    { "q-music", "meme-music" },
    { "q-how-are-you", "meme-wakuwaku" },
    { "q-comfort", "work-pat" },
    { "q-exit", "meme-cry" },
    { "q-tray", "afk" },
    { "q-data", "meme-peace" },
    { "q-uninstall", "meme-cry" },
    { "q-recycle", "sweep" },
    { "q-position", "waiting" },
    { "q-update", "levelup" },
    { "q-are-you-happy", "star" },
    { "q-anger", "angry" },
    { "q-shy", "blush" },
    { "q-jealous", "teasing" },
    { "q-proud", "meme-smug" },
    { "q-sad", "meme-smile-pain" },
    { "q-coy", "daily-melt" },
    { "q-compliment-back", "wink" },
    { "q-ignore", "waiting" },
    { "q-tsundere", "meme-smug" },
    { "q-token-what", "meme-wakuwaku" },
    { "q-kitten-items", "daily-fishing" },
    { "q-stockfish", "cool-shades" },
    { "q-pose-source", "star" },
    { "q-guardian-title", "achievement" },
    { "q-privacy", "meme-peace" },
    { "q-why-default-off", "idle-cute" },
    { "q-egg", "festival-halloween" },
    { "q-accompany-code", "running" },
    { "q-accompany-slack", "work-slack-phone" },
    { "q-accompany-sad", "work-pat" },
    { "q-accompany-late", "night" },
    { "q-accompany-dawn", "greet" },
    { "q-accompany-offwork", "celebrate" },
    { "q-accompany-holiday", "waiting" },
    { "q-accompany-alone", "afk" },
    { "q-accompany-howlong", "meme-heart" },
    { "q-thanks", "meme-kyun" },
    { "q-first-meet", "meme-kyun", "blush" },
    { "q-heartbeat", "meme-heart" },
    { "q-what-am-i", "blush", "meme-heart" },
    { "q-night-thoughts", "daily-pajama" },
    { "q-worry", "meme-smile-pain" },
    { "q-precious", "star" },
    { "q-alone-fear", "meme-cry" },
    { "q-say-love", "blush", "meme-heart" },
    { "q-jealous-secret", "teasing" },
    { "q-comfort-secret", "work-pat" },
    { "q-if-i-leave", "meme-cry" },
    { "q-wish-for-us", "star" },
    { "q-name-me", "meme-wakuwaku", "blush" },
    { "q-how-much", "meme-kyun" },
    { "q-pinky-promise", "meme-yes", "valentine" },
};

inline constexpr std::size_t kDialoguePoseRuleCount =
    sizeof(kDialoguePoseRules) / sizeof(kDialoguePoseRules[0]);

// 查询该题的按题立绘池：命中返回池大小（1–3）并填出 outPoses[0..2]；
// 未命中返回 0（outPoses 不写出）→ 调用方回落类别池。
inline std::size_t dialoguePosesForQuestion(const std::string &id, const char *outPoses[3])
{
    for (const DialoguePoseRule &rule : kDialoguePoseRules) {
        if (id == rule.id) {
            outPoses[0] = rule.p0;
            outPoses[1] = rule.p1;
            outPoses[2] = rule.p2;
            return (rule.p1 == nullptr) ? 1 : ((rule.p2 == nullptr) ? 2 : 3);
        }
    }
    return 0;
}

// 从独立池取一张：尽量避开 avoid（通常是上一次用过的那张）。
// rng 为 nullptr 时取池首张（确定性退化，便于单测）。
// 池中只有一张且正是 avoid 时返回 nullptr —— 由调用方保留上一张，不做任何伪装。
inline const char *pickDialoguePose(const char *const *poses, std::size_t count, IRandom *rng,
                                    const char *avoid)
{
    if (poses == nullptr || count == 0) {
        return nullptr;
    }
    for (std::size_t attempt = 0; attempt < count; ++attempt) {
        const int raw = (rng != nullptr) ? rng->nextInt(static_cast<int>(count)) : 0;
        const std::size_t index = static_cast<std::size_t>(raw) % count;
        const char *pose = poses[index];
        if (avoid == nullptr || std::strcmp(pose, avoid) != 0) {
            return pose;
        }
    }
    return nullptr;
}

} // namespace whalepet::core
