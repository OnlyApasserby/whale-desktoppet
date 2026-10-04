#include "core/PresetDialogue.h"

#include <algorithm>
#include <utility>

namespace whalepet::core {

namespace {

std::string trim(const std::string &text)
{
    const char *ws = " \t\r\n";
    const std::size_t begin = text.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(ws);
    return text.substr(begin, end - begin + 1);
}

// 按 '|' 切分（返回前 count 段；段数不足时空段为空串）
std::vector<std::string> split(const std::string &line, std::size_t count)
{
    std::vector<std::string> parts(count);
    std::size_t begin = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i + 1 == count) {
            parts[i] = line.substr(begin);
            break;
        }
        const std::size_t pos = line.find('|', begin);
        if (pos == std::string::npos) {
            parts[i] = line.substr(begin);
            begin = line.size();
            continue;
        }
        parts[i] = line.substr(begin, pos - begin);
        begin = pos + 1;
    }
    for (std::string &part : parts) {
        part = trim(part);
    }
    return parts;
}

} // namespace

// ---------------------------------------------------------------------------
// 类别
// ---------------------------------------------------------------------------

const char *dialogueCategoryId(DialogueCategory category)
{
    switch (category) {
    case DialogueCategory::Normal:
        return "normal";
    case DialogueCategory::Sensitive:
        return "sensitive";
    case DialogueCategory::Choice:
        return "choice";
    case DialogueCategory::Weather:
        return "weather";
    }
    return "normal";
}

bool dialogueCategoryFromId(const std::string &id, DialogueCategory &out)
{
    if (id == "normal") {
        out = DialogueCategory::Normal;
    } else if (id == "sensitive") {
        out = DialogueCategory::Sensitive;
    } else if (id == "choice") {
        out = DialogueCategory::Choice;
    } else if (id == "weather") {
        out = DialogueCategory::Weather;
    } else {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 语料
// ---------------------------------------------------------------------------

const std::string *DialogueQuestion::answerFor(const std::string &slot) const
{
    for (const DialogueAnswer &answer : answers) {
        if (answer.slot == slot) {
            return &answer.text;
        }
    }
    return nullptr;
}

std::vector<const std::string *> DialogueQuestion::answersFor(const std::string &slot) const
{
    std::vector<const std::string *> out;
    for (const DialogueAnswer &answer : answers) {
        if (answer.slot == slot) {
            out.push_back(&answer.text);
        }
    }
    return out;
}

std::vector<std::string> DialogueQuestion::answerSlots() const
{
    std::vector<std::string> out;
    out.reserve(answers.size());
    for (const DialogueAnswer &answer : answers) {
        bool seen = false;
        for (const std::string &slot : out) {
            if (slot == answer.slot) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            out.push_back(answer.slot);
        }
    }
    return out;
}

std::vector<std::string> DialogueQuestion::answerTexts() const
{
    std::vector<std::string> out;
    out.reserve(answers.size());
    for (const DialogueAnswer &answer : answers) {
        out.push_back(answer.text);
    }
    return out;
}

std::string dialogueSceneKey(const std::string &id, const std::string &slot)
{
    return "dialogue." + id + "." + slot;
}

std::size_t PresetDialogueTable::loadFromText(const std::string &content)
{
    std::size_t before = m_questions.size();
    std::size_t begin = 0;
    while (begin <= content.size()) {
        std::size_t end = content.find('\n', begin);
        if (end == std::string::npos) {
            end = content.size();
        }
        std::string line = trim(content.substr(begin, end - begin));
        begin = end + 1;

        if (line.empty() || line[0] == '#') {
            if (end == content.size()) {
                break;
            }
            continue;
        }

        if (line.rfind("q|", 0) == 0) {
            const std::vector<std::string> parts = split(line, 4);
            DialogueCategory category = DialogueCategory::Normal;
            if (parts[1].empty() || parts[3].empty()
                || !dialogueCategoryFromId(parts[2], category)) {
                continue; // 脏行：跳过（不静默产生半个问题）
            }
            if (find(parts[1]) != nullptr) {
                continue; // 重复 id：保留首次声明
            }
            DialogueQuestion question;
            question.id = parts[1];
            question.category = category;
            question.text = parts[3];
            m_questions.push_back(std::move(question));
        } else if (line.rfind("a|", 0) == 0) {
            const std::vector<std::string> parts = split(line, 4);
            if (parts[1].empty() || parts[2].empty() || parts[3].empty()) {
                continue;
            }
            DialogueQuestion *question = nullptr;
            for (DialogueQuestion &candidate : m_questions) {
                if (candidate.id == parts[1]) {
                    question = &candidate;
                    break;
                }
            }
            if (question == nullptr) {
                continue; // 孤儿回答：丢弃（否则会成为永不出现的问题碎片）
            }
            // 同一 (id, slot) **允许**多条：它们共享同一个台词场景 key，
            // 由 LineTable::pick 随机取一条（见头文件说明）。
            question->answers.push_back(DialogueAnswer{parts[2], parts[3]});
        }

        if (end == content.size()) {
            break;
        }
    }
    return m_questions.size() - before;
}

void PresetDialogueTable::addAnswer(const std::string &id, const std::string &slot,
                                    const std::string &text)
{
    DialogueQuestion *question = nullptr;
    for (DialogueQuestion &candidate : m_questions) {
        if (candidate.id == id) {
            question = &candidate;
            break;
        }
    }
    if (question == nullptr) {
        DialogueQuestion fresh;
        fresh.id = id;
        m_questions.push_back(std::move(fresh));
        question = &m_questions.back();
    }
    for (DialogueAnswer &answer : question->answers) {
        if (answer.slot == slot) {
            answer.text = text;
            return;
        }
    }
    question->answers.push_back(DialogueAnswer{slot, text});
}

void PresetDialogueTable::clear()
{
    m_questions.clear();
}

const DialogueQuestion *PresetDialogueTable::find(const std::string &id) const
{
    for (const DialogueQuestion &question : m_questions) {
        if (question.id == id) {
            return &question;
        }
    }
    return nullptr;
}

std::size_t PresetDialogueTable::countOf(DialogueCategory category) const
{
    std::size_t count = 0;
    for (const DialogueQuestion &question : m_questions) {
        if (question.category == category) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// 类别判定 / 随机辅助
// ---------------------------------------------------------------------------

bool dialogueCategoryIsRandom(DialogueCategory category)
{
    return category == DialogueCategory::Normal || category == DialogueCategory::Choice;
}

const DialogueQuestion *pickDialogueQuestion(const std::vector<const DialogueQuestion *> &candidates,
                                             IRandom *rng)
{
    if (candidates.empty()) {
        return nullptr;
    }
    if (rng == nullptr) {
        return candidates.front();
    }
    const std::size_t index =
        static_cast<std::size_t>(rng->nextInt(static_cast<int>(candidates.size()))) % candidates.size();
    return candidates[index];
}

} // namespace whalepet::core
