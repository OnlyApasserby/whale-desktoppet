#include "core/PythonImports.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace whalepet::core {

namespace {

bool isSpaceChar(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string trimmed(const std::string &value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && isSpaceChar(value[begin])) {
        ++begin;
    }
    while (end > begin && isSpaceChar(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

// 按 '\n' 切行（结果不含行尾换行符；末尾换行由调用方按原文还原）
std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find('\n', start);
        if (pos == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return lines;
}

std::string joinLines(const std::vector<std::string> &lines)
{
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) {
            out += '\n';
        }
    }
    return out;
}

// 行尾注释切片：import 语句内不含字符串字面量，故直接截断第一个 '#' 即可（口径见头注释）
std::string stripTrailingComment(const std::string &line)
{
    const std::size_t pos = line.find('#');
    return (pos == std::string::npos) ? line : line.substr(0, pos);
}

// 去重键：去掉行尾注释 → 折叠空白 → 去首尾
std::string dedupKey(const std::string &line)
{
    const std::string body = trimmed(stripTrailingComment(line));
    std::string out;
    bool pendingSpace = false;
    for (const char c : body) {
        if (isSpaceChar(c)) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(c);
    }
    return out;
}

bool isCommentOrBlank(const std::string &line)
{
    const std::string body = trimmed(line);
    return body.empty() || body.front() == '#';
}

// 是否「可整体搬走的顶层 import 语句」（口径见头注释）
bool isHoistableImport(const std::string &line)
{
    if (line.empty() || isSpaceChar(line.front())) {
        return false; // 缩进 = 不在顶层
    }
    const std::string body = trimmed(stripTrailingComment(line));
    const bool isImport = (body.rfind("import ", 0) == 0);
    const bool isFrom = (body.rfind("from ", 0) == 0);
    if (!isImport && !isFrom) {
        return false;
    }
    if (isFrom && body.find(" import") == std::string::npos) {
        return false; // `from … import …` 才算完整语句
    }
    if (body.back() == '\\') {
        return false; // 反斜杠续行
    }
    int depth = 0;
    for (const char c : body) {
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
            if (depth < 0) {
                return false;
            }
        }
    }
    return depth == 0; // 括号未闭合 = 跨行语句，保持原位
}

} // namespace

std::string hoistImports(const std::string &code)
{
    if (code.empty()) {
        return code;
    }
    const std::vector<std::string> lines = splitLines(code);

    // 1) 注释头：前导的注释 / 空行（shebang、编码行与生成说明留在原处）
    std::size_t headEnd = 0;
    while (headEnd < lines.size() && isCommentOrBlank(lines[headEnd])) {
        ++headEnd;
    }

    // 2) 收集顶层 import（按首次出现顺序；重复语句只删不搬）
    std::vector<std::string> imports;
    std::vector<std::string> seenKeys;
    std::vector<bool> removed(lines.size(), false);
    for (std::size_t i = headEnd; i < lines.size(); ++i) {
        if (!isHoistableImport(lines[i])) {
            continue;
        }
        const std::string key = dedupKey(lines[i]);
        if (key.empty()) {
            continue;
        }
        removed[i] = true;
        if (std::find(seenKeys.begin(), seenKeys.end(), key) != seenKeys.end()) {
            continue; // 重复：删掉这一行
        }
        seenKeys.push_back(key);
        imports.push_back(lines[i]); // 保留原文（含行尾注释）
    }
    if (imports.empty()) {
        return code; // 无 import：零改动（同时保证幂等）
    }

    // 3) 其余行：删掉已归位的 import，并去掉由此产生的开头空行
    std::vector<std::string> rest;
    for (std::size_t i = headEnd; i < lines.size(); ++i) {
        if (!removed[i]) {
            rest.push_back(lines[i]);
        }
    }
    std::size_t first = 0;
    while (first < rest.size() && trimmed(rest[first]).empty()) {
        ++first;
    }
    rest.erase(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(first));

    // 4) 组装：注释头 + import 块 + 空行 + 其余
    std::vector<std::string> out(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(headEnd));
    out.insert(out.end(), imports.begin(), imports.end());
    if (!rest.empty()) {
        out.push_back(std::string());
        out.insert(out.end(), rest.begin(), rest.end());
    }

    std::string result = joinLines(out);
    if (code.back() == '\n') {
        result += '\n';
    }
    return result;
}

} // namespace whalepet::core
