#include "core/DialogueOptions.h"

#include <algorithm>
#include <utility>

namespace whalepet::core {

namespace {

void shuffleQuestions(std::vector<const DialogueQuestion *> &items, IRandom *rng)
{
    if (rng == nullptr) {
        return; // 无随机源：保持登记顺序（确定性退化，便于单测）
    }
    for (std::size_t i = items.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(rng->nextInt(static_cast<int>(i))) % i;
        std::swap(items[i - 1], items[j]);
    }
}

bool containsId(const std::vector<std::string> &ids, const std::string &id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// 收集某一类别的全部题目（跳过无文本 / 无回答的脏数据）
std::vector<const DialogueQuestion *> questionsOf(const PresetDialogueTable &table,
                                                  bool (*accept)(DialogueCategory))
{
    std::vector<const DialogueQuestion *> out;
    for (const DialogueQuestion &question : table.questions()) {
        if (question.text.empty() || question.answerCount() == 0) {
            continue;
        }
        if (accept(question.category)) {
            out.push_back(&question);
        }
    }
    return out;
}

bool isWeather(DialogueCategory category)
{
    return category == DialogueCategory::Weather;
}

bool isSensitive(DialogueCategory category)
{
    return category == DialogueCategory::Sensitive;
}

bool isRandom(DialogueCategory category)
{
    return dialogueCategoryIsRandom(category);
}

} // namespace

std::vector<DialogueOption> buildDialogueOptions(const PresetDialogueTable &table, IRandom *rng,
                                                 const DialogueOptionRequest &request,
                                                 const std::vector<std::string> &recentIds)
{
    std::vector<DialogueOption> options;
    options.reserve(kDialogueFixedOptionCount + request.randomCount);

    // ---- 槽位 1：天气题（可用性由「是否配置彩云 key + 城市」决定）----
    DialogueOption weather;
    weather.kind = DialogueOptionKind::Weather;
    weather.question = pickDialogueQuestion(questionsOf(table, isWeather), rng);
    if (weather.question == nullptr) {
        weather.reason = kDialogueReasonUnavailable;
    } else if (!request.weatherAvailable) {
        weather.reason = kDialogueReasonWeather;
    } else {
        weather.available = true;
    }
    options.push_back(weather);

    // ---- 槽位 2：敏感 / 私密题（好感度门槛 + 每日次数）----
    DialogueOption sensitive;
    sensitive.kind = DialogueOptionKind::Sensitive;
    sensitive.question = pickDialogueQuestion(questionsOf(table, isSensitive), rng);
    if (sensitive.question == nullptr) {
        sensitive.reason = kDialogueReasonUnavailable;
    } else if (!request.sensitiveUnlocked) {
        sensitive.reason = kDialogueReasonSensitiveLocked;
    } else if (!request.sensitiveQuotaLeft) {
        sensitive.reason = kDialogueReasonSensitiveQuota;
    } else {
        sensitive.available = true;
    }
    options.push_back(sensitive);

    // ---- 槽位 3..N：随机题（每次刷新重新抽取；优先避开上一轮出现过的）----
    std::vector<const DialogueQuestion *> candidates = questionsOf(table, isRandom);
    std::vector<const DialogueQuestion *> fresh;
    std::vector<const DialogueQuestion *> repeated;
    for (const DialogueQuestion *question : candidates) {
        if (containsId(recentIds, question->id)) {
            repeated.push_back(question);
        } else {
            fresh.push_back(question);
        }
    }
    shuffleQuestions(fresh, rng);
    shuffleQuestions(repeated, rng);

    std::vector<const DialogueQuestion *> picked;
    for (const DialogueQuestion *question : fresh) {
        if (picked.size() >= request.randomCount) {
            break;
        }
        picked.push_back(question);
    }
    for (const DialogueQuestion *question : repeated) {
        if (picked.size() >= request.randomCount) {
            break;
        }
        picked.push_back(question); // 语料不足时才重复
    }

    for (std::size_t i = 0; i < request.randomCount; ++i) {
        DialogueOption option;
        option.kind = DialogueOptionKind::Random;
        if (i < picked.size()) {
            option.question = picked[i];
            option.available = true;
        } else {
            option.reason = kDialogueReasonUnavailable;
        }
        options.push_back(option);
    }

    return options;
}

std::string pickAnswerSlot(const DialogueQuestion &question, IRandom *rng, const char *weatherSlotId)
{
    if (question.answerCount() == 0) {
        return {};
    }

    if (question.category == DialogueCategory::Weather) {
        const char *kind = (weatherSlotId != nullptr) ? weatherSlotId : "";
        if (!question.answersFor(kind).empty()) {
            return kind;
        }
        if (!question.answersFor("unknown").empty()) {
            return "unknown"; // 未识别的天气类型统一落到 unknown 槽位
        }
        return question.answers.front().slot; // 最后兜底：首条回答的槽位
    }

    // 普通 / 敏感 / 选择：从已登记的 slot 中随机取一个（三个预设回答随机其一）
    const std::vector<std::string> slots = question.answerSlots();
    if (slots.empty()) {
        return {};
    }
    if (rng == nullptr) {
        return slots.front();
    }
    const std::size_t index =
        static_cast<std::size_t>(rng->nextInt(static_cast<int>(slots.size()))) % slots.size();
    return slots[index];
}

} // namespace whalepet::core
