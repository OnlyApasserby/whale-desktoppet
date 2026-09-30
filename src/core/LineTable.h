#pragma once

// 台词表：解析外部台词资源（docs/CHAT.md §1「不硬编码进 C++」）。
//
// P2 只实现最小可用子集：场景 key → 若干候选台词；随机取一条并做「最近 N 条去重」。
// 心情/羁绊分层（CHAT.md §3）与关键词感知（§4）留待 P4 扩展。
//
// 零 Qt 依赖：只处理 std::string，便于单测。

#include "core/IRandom.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace whalepet::core {

class LineTable {
public:
    // 单行格式：`sceneKey|台词文本`；空行与以 '#' 开头的行忽略。
    // 返回成功解析的条数。
    std::size_t loadFromText(const std::string &content);

    // 追加一条（供程序内构造/测试）
    void addLine(const std::string &key, const std::string &text);

    // 场景 key 下随机取一条台词；无候选返回空串。
    // rng 为 nullptr 时取第一条。返回结果会进入「最近」窗口，避免连续重复。
    std::string pick(const std::string &key, IRandom *rng);

    bool empty() const { return m_lines.empty(); }
    std::size_t size() const { return m_total; }
    bool hasScene(const std::string &key) const;

    // 清空「最近」去重窗口
    void clearRecent() { m_recent.clear(); }

private:
    static constexpr std::size_t kRecentWindow = 8;

    std::unordered_map<std::string, std::vector<std::string>> m_lines;
    std::vector<std::string> m_recent;
    std::size_t m_total = 0;
};

} // namespace whalepet::core
