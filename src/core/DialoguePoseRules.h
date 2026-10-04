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
