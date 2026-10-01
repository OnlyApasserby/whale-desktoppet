#include "viewmodel/ChatService.h"

#include "core/ChatRules.h"

namespace whalepet::viewmodel {

namespace {
// 羁绊场景档位（与 core::bondSceneKey 的阈值一致：3 / 5 / 7）
int bondTier(int level)
{
    if (level >= 7) {
        return 3;
    }
    if (level >= 5) {
        return 2;
    }
    if (level >= 3) {
        return 1;
    }
    return 0;
}
} // namespace

ChatService::ChatService(core::LineTable *lines, QObject *parent)
    : QObject(parent)
    , m_lines(lines)
{
}

void ChatService::reset()
{
    m_lastGreetSlot = -1;
    m_lastMoodTier = -1;
    m_lastBondTier = -1;
}

std::string ChatService::matchText(const QString &text) const
{
    if (!m_keywordAware || text.isEmpty()) {
        return {};
    }
    // 自定义热词优先于内置触发词（see core/ChatRules.h::matchKeyword）
    const char *id = core::matchKeyword(text.toStdString(), m_hotwords);
    return id != nullptr ? std::string(id) : std::string{};
}

void ChatService::setCustomHotwords(const std::vector<core::CustomHotword> &hotwords)
{
    m_hotwords = hotwords;
}

std::string ChatService::matchHotword(const QString &text) const
{
    if (text.isEmpty()) {
        return {};
    }
    // 刻意不检查 m_keywordAware：显式录入是用户主动动作（见头文件说明）
    const char *id = core::matchKeyword(text.toStdString(), m_hotwords);
    return id != nullptr ? std::string(id) : std::string{};
}

std::string ChatService::greetScene(int hour)
{
    const core::GreetSlot slot = core::greetSlot(hour);
    const int slotIndex = static_cast<int>(slot);
    if (slotIndex == m_lastGreetSlot) {
        return {}; // 同一时段（如 9/10/11 点同属 Forenoon）只问候一次
    }
    m_lastGreetSlot = slotIndex;

    const char *scene = core::greetSceneKey(slot);
    if (scene == nullptr) {
        return {}; // 深夜静默
    }
    const std::string key(scene);
    return sceneAvailable(key) ? key : std::string{};
}

std::string ChatService::moodSceneFor(int mood)
{
    const core::MoodTier tier = core::moodTier(mood);
    const int tierIndex = static_cast<int>(tier);
    const bool firstObservation = (m_lastMoodTier < 0);
    const bool changed = (tierIndex != m_lastMoodTier);
    m_lastMoodTier = tierIndex;

    if (firstObservation || !changed) {
        return {}; // 首次观察只建立基线（避免启动即报心情），未跨档也不播报
    }
    const char *scene = core::moodSceneKey(tier);
    if (scene == nullptr) {
        return {}; // 中性心情不做替换
    }
    const std::string key(scene);
    return sceneAvailable(key) ? key : std::string{};
}

std::string ChatService::bondSceneFor(int level)
{
    const int tier = bondTier(level);
    const bool firstObservation = (m_lastBondTier < 0);
    const bool changed = (tier != m_lastBondTier);
    m_lastBondTier = tier;

    if (firstObservation || !changed) {
        return {};
    }
    const char *scene = core::bondSceneKey(level);
    if (scene == nullptr) {
        return {};
    }
    const std::string key(scene);
    return sceneAvailable(key) ? key : std::string{};
}

bool ChatService::sceneAvailable(const std::string &scene) const
{
    return m_lines != nullptr && m_lines->hasScene(scene);
}

} // namespace whalepet::viewmodel
