#include "core/LineTable.h"

#include <algorithm>

namespace whalepet::core {

namespace {
// 去掉首尾空白（含 CR，兼容 Windows 换行）
std::string trim(const std::string &s)
{
    const char *ws = " \t\r\n";
    const std::size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}
} // namespace

std::size_t LineTable::loadFromText(const std::string &content)
{
    std::size_t parsed = 0;
    std::size_t pos = 0;
    while (pos <= content.size()) {
        const std::size_t nl = content.find('\n', pos);
        const std::string raw = (nl == std::string::npos)
                                    ? content.substr(pos)
                                    : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;

        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t sep = line.find('|');
        if (sep == std::string::npos) {
            continue; // 格式不符的行静默跳过，不让整份语料失效
        }
        const std::string key = trim(line.substr(0, sep));
        const std::string text = trim(line.substr(sep + 1));
        if (key.empty() || text.empty()) {
            continue;
        }
        addLine(key, text);
        ++parsed;
    }
    return parsed;
}

void LineTable::addLine(const std::string &key, const std::string &text)
{
    if (key.empty() || text.empty()) {
        return;
    }
    m_lines[key].push_back(text);
    ++m_total;
}

bool LineTable::hasScene(const std::string &key) const
{
    const auto it = m_lines.find(key);
    return it != m_lines.end() && !it->second.empty();
}

std::string LineTable::pick(const std::string &key, IRandom *rng)
{
    const auto it = m_lines.find(key);
    if (it == m_lines.end() || it->second.empty()) {
        return {};
    }
    const std::vector<std::string> &candidates = it->second;

    // 最近 N 条去重（CHAT.md §5）：能避免重复就避免，实在只剩重复条目则照用。
    std::vector<const std::string *> fresh;
    for (const std::string &line : candidates) {
        if (std::find(m_recent.begin(), m_recent.end(), line) == m_recent.end()) {
            fresh.push_back(&line);
        }
    }

    const std::string *chosen = nullptr;
    if (fresh.empty()) {
        const int idx = (rng != nullptr) ? rng->nextInt(static_cast<int>(candidates.size())) : 0;
        chosen = &candidates[static_cast<std::size_t>(idx)];
    } else {
        const int idx = (rng != nullptr) ? rng->nextInt(static_cast<int>(fresh.size())) : 0;
        chosen = fresh[static_cast<std::size_t>(idx)];
    }

    m_recent.push_back(*chosen);
    while (m_recent.size() > kRecentWindow) {
        m_recent.erase(m_recent.begin());
    }
    return *chosen;
}

} // namespace whalepet::core
