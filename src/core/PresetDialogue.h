#pragma once

// 预设对话（P8）：**用户提问 → 鲸鱼娘回答** 的语料解析（docs/DIALOGUE.md）。
//
// 语料（assets/lines/dialogue.txt）按行声明，不硬编码进 C++：
//
//   q|<id>|<category>|<问题文本>        ← 用户可选的「问题选项」
//   a|<id>|<slot>|<回答文本>            ← 鲸鱼娘的回答（同一 slot 可写多条候选）
//
//   * category ∈ normal / sensitive / choice / weather；
//   * slot 对 normal / sensitive / choice 是 0、1、2（**每个问题三个预设回答**）；
//     对 weather 是天气类型 id（sunny / cloudy / overcast / rain / snow / fog /
//     thunder / hail / unknown，见 core/WeatherRules.h），未命中时回落 unknown；
//   * 同一 (id, slot) 允许**多条**回答：它们登记进**同一个**台词场景 key
//     `dialogue.<id>.<slot>`，由 LineTable::pick 随机取一条（带最近 N 条去重）。
//     因此「三个回答」的随机由 slot 选择（viewmodel）与 LineTable 双重表达。
//
// 选项池（五选一）见 `core/DialogueOptions.h`。
//
// 零 Qt 依赖：可脱界面单测。

#include "core/IRandom.h"

#include <cstddef>
#include <string>
#include <vector>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 类别
// ---------------------------------------------------------------------------

enum class DialogueCategory {
    Normal,    // 日常问题（用户可问）：提问立绘走通用好奇
    Sensitive, // 敏感 / 私密问题：独立立绘池 meme-broke / meme-cry / meme-heart
    Choice,    // 选择类问题（「今天吃火锅吗」）：独立立绘池 meme-no / meme-yes
    Weather    // 天气问题：立绘由彩云天气类型决定
};

// 是否为「普通随机候选」类别（Normal / Choice）：五选一里可随机刷新的那一类
bool dialogueCategoryIsRandom(DialogueCategory category);

const char *dialogueCategoryId(DialogueCategory category);
bool dialogueCategoryFromId(const std::string &id, DialogueCategory &out);

// ---------------------------------------------------------------------------
// 语料
// ---------------------------------------------------------------------------

struct DialogueAnswer {
    std::string slot; // "0" / "1" / "2"，或天气类型 id
    std::string text;
};

struct DialogueQuestion {
    std::string id;
    DialogueCategory category = DialogueCategory::Normal;
    std::string text; // 用户会问的问题（如「今天心情怎么样呀？」）
    std::vector<DialogueAnswer> answers; // 同一 slot 可有多条候选

    std::size_t answerCount() const { return answers.size(); }
    // 取 slot 对应的**第一条**回答；不存在返回 nullptr（向后兼容 / 单测用）
    const std::string *answerFor(const std::string &slot) const;
    // 取 slot 对应的**全部**候选回答（可能多条）；空表示该 slot 不存在
    std::vector<const std::string *> answersFor(const std::string &slot) const;
    // 该问题已登记的 slot 列表（去重、按登记顺序）：普通题为 "0"/"1"/"2"。
    // 注意：**不能叫 `slots`** —— Qt 把 `slots` 定义为关键字宏（`#define slots`），
    // 同名成员函数会在 Qt 头文件之后被展开成空 token 而导致语法错误（见 docs/pitfalls/）。
    std::vector<std::string> answerSlots() const;
    // 全部候选回答文本（按登记顺序）
    std::vector<std::string> answerTexts() const;
};

// 台词场景 key：dialogue.<id>.<slot>
std::string dialogueSceneKey(const std::string &id, const std::string &slot);

class PresetDialogueTable {
public:
    // 解析 `q|` / `a|` 行；空行与 '#' 注释忽略。返回成功解析的**问题数**。
    // 重复 id 的 `a|` 行会合并进已有问题；孤儿 `a|` 行（无对应 `q|`）被丢弃。
    std::size_t loadFromText(const std::string &content);
    // 追加问题内的一条回答（供程序内构造 / 测试）
    void addAnswer(const std::string &id, const std::string &slot, const std::string &text);

    void clear();
    bool empty() const { return m_questions.empty(); }
    std::size_t size() const { return m_questions.size(); }
    const std::vector<DialogueQuestion> &questions() const { return m_questions; }
    const DialogueQuestion *find(const std::string &id) const;
    std::size_t countOf(DialogueCategory category) const;

private:
    std::vector<DialogueQuestion> m_questions;
};

// ---------------------------------------------------------------------------
// 类别判定 / 随机辅助（零 Qt）
// ---------------------------------------------------------------------------

// 从候选列表中随机取一个；rng 为 nullptr 或列表为空 → nullptr
const DialogueQuestion *pickDialogueQuestion(const std::vector<const DialogueQuestion *> &candidates,
                                             IRandom *rng);

} // namespace whalepet::core
