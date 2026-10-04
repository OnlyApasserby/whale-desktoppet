#pragma once

// 五选一选项池（P8 问答系统，docs/DIALOGUE.md §3）：
// **用户提问 → 鲸鱼娘回答**。界面固定呈现五个「可问的问题」槽位：
//
//   | # | 槽位 | 来源 | 可用条件 |
//   |---|------|------|----------|
//   | 1 | 天气题 | category == weather | **必须配置彩云天气 key + 城市**（否则禁用） |
//   | 2 | 敏感 / 私密题 | category == sensitive | **好感度 ≥ kSensitiveUnlockAffinity（5000）** 且当日剩余次数 > 0（每日 kSensitiveDailyLimit 次） |
//   | 3–5 | 随机题 ×3 | category ∈ {normal, choice} | 每次刷新重新随机抽取 |
//
// 槽位**恒定存在**（即使不可用或语料里没有对应题目），保证界面形状稳定为「五选一」；
// 不可用项由 UI 禁用并给出原因文案（见 kDialogueReason*）。
//
// 零 Qt 依赖：可脱界面单测。

#include "core/IRandom.h"
#include "core/PresetDialogue.h"

#include <cstddef>
#include <string>
#include <vector>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 解锁与限额（禁止在别处再写字面量）
// ---------------------------------------------------------------------------

// 敏感 / 私密题的解锁门槛：好感度（affinity，上限 kAffinityMax = 10000）
inline constexpr int kSensitiveUnlockAffinity = 5000;
// 敏感 / 私密题每日可问次数上限
inline constexpr int kSensitiveDailyLimit = 3;
// 五选一的固定槽位数（天气 + 敏感）与随机槽位数
inline constexpr std::size_t kDialogueFixedOptionCount = 2;
inline constexpr std::size_t kDialogueRandomOptionCount = 3;

// 不可用原因（UI tooltip 文案；非 nullptr 时 UI 应禁用该项）
inline constexpr const char *kDialogueReasonWeather =
    "还没有配置天气接口（设置 → 预设对话：填 key 与城市）";
inline constexpr const char *kDialogueReasonSensitiveLocked = "好感度达到 5000 后解锁";
inline constexpr const char *kDialogueReasonSensitiveQuota = "今天的私密话题次数已用完（每日 3 次）";
inline constexpr const char *kDialogueReasonUnavailable = "暂时没有可问的问题（语料缺失）";

// ---------------------------------------------------------------------------
// 选项
// ---------------------------------------------------------------------------

enum class DialogueOptionKind { Weather, Sensitive, Random };

struct DialogueOption {
    const DialogueQuestion *question = nullptr; // nullptr = 语料里没有该类问题
    DialogueOptionKind kind = DialogueOptionKind::Random;
    bool available = false;   // 可点选
    const char *reason = nullptr; // !available 时的原因（kDialogueReason*）
};

// 可用性输入（由 viewmodel / View 层收集，core 只做判定）
struct DialogueOptionRequest {
    bool weatherAvailable = false;    // 彩云 key + 城市已配置
    bool sensitiveUnlocked = false;   // 好感度 ≥ kSensitiveUnlockAffinity
    bool sensitiveQuotaLeft = false;  // 当日已用次数 < kSensitiveDailyLimit
    std::size_t randomCount = kDialogueRandomOptionCount;
};

// 构建五选一选项池。顺序恒为：天气、敏感、随机 × randomCount。
//   * recentIds：上一轮出现过的随机题 id（优先避开，语料不足时允许重复）；
//   * 语言里缺少对应类别的题目时，槽位保留但 question == nullptr 且 available == false。
std::vector<DialogueOption> buildDialogueOptions(const PresetDialogueTable &table, IRandom *rng,
                                                 const DialogueOptionRequest &request,
                                                 const std::vector<std::string> &recentIds = {});

// 选取该问题的回答槽位：
//   * 天气题：`weatherSlotId`（如 "rain"）→ 表里没有该 slot 时回落 "unknown" → 再回落首条；
//   * 其它题：从该题已登记的 slot 中随机取一个（=「从三个预设回答中随机选一个」）。
//   返回空串表示该问题没有任何可用回答（调用方应跳过，不输出文字）。
std::string pickAnswerSlot(const DialogueQuestion &question, IRandom *rng, const char *weatherSlotId);

} // namespace whalepet::core
