#include "core/PythonBlocks.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace whalepet::core {

namespace {

std::string trimmed(const std::string &value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::vector<std::string> splitFields(const std::string &line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            fields.push_back(trimmed(line.substr(start)));
            break;
        }
        fields.push_back(trimmed(line.substr(start, pos - start)));
        start = pos + 1;
    }
    return fields;
}

// 末尾分发器（WhalePet 自动生成）：`python <脚本> <功能名> [该功能自己的参数]`
std::string dispatcher(const std::vector<PythonBlock> &blocks)
{
    std::string out;
    out += "# ==================== 入口分发（WhalePet 自动生成） ====================\n";
    out += "if __name__ == \"__main__\":\n";
    out += "    import sys\n";
    out += "    _commands = {\n";
    for (const PythonBlock &block : blocks) {
        out += "        \"" + block.id + "\": " + blockEntryName(block.id) + ",\n";
    }
    out += "    }\n";
    out += "    if len(sys.argv) < 2 or sys.argv[1] in (\"-h\", \"--help\"):\n";
    out += "        print(\"可用功能：\" + \"、\".join(sorted(_commands)))\n";
    out += "        print(\"用法：python \" + sys.argv[0] + \" <功能名> [参数…]\")\n";
    out += "        raise SystemExit(0)\n";
    out += "    _name = sys.argv[1]\n";
    out += "    if _name not in _commands:\n";
    out += "        raise SystemExit(\"未知功能：\" + _name + \"；可用功能：\" + \"、\".join(sorted(_commands)))\n";
    out += "    sys.argv = [sys.argv[0]] + sys.argv[2:]\n";
    out += "    _commands[_name]()\n";
    return out;
}

std::string ensureTrailingNewline(const std::string &body)
{
    if (body.empty() || body.back() == '\n') {
        return body;
    }
    return body + "\n";
}

} // namespace

bool isValidBlockId(const std::string &id)
{
    if (id.empty() || id.front() == '-') {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string blockEntryName(const std::string &id)
{
    if (!isValidBlockId(id)) {
        return {};
    }
    std::string name = "run_";
    for (const char c : id) {
        name.push_back(c == '-' ? '_' : c);
    }
    return name;
}

bool blockBodyHasEntry(const std::string &body, const std::string &id)
{
    const std::string entry = blockEntryName(id);
    if (entry.empty()) {
        return false;
    }
    return body.find("def " + entry + "(") != std::string::npos;
}

std::vector<PythonBlock> parseBlockIndex(const std::string &text)
{
    std::vector<PythonBlock> blocks;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find('\n', start);
        const std::string rawLine =
            (pos == std::string::npos) ? text.substr(start) : text.substr(start, pos - start);
        start = (pos == std::string::npos) ? text.size() + 1 : pos + 1;

        const std::string line = trimmed(rawLine);
        if (line.empty() || line.front() == '#') {
            continue; // 注释与空行
        }
        const std::vector<std::string> fields = splitFields(line);
        if (fields.size() < 4) {
            continue; // 字段不全：宁缺毋滥（静默跳过，缺失会被资源测试发现）
        }
        PythonBlock block;
        block.id = fields[0];
        block.title = fields[1];
        block.category = fields[2];
        block.summary = fields[3];
        block.requires = (fields.size() >= 5 && !fields[4].empty()) ? fields[4] : "标准库";
        if (!isValidBlockId(block.id) || block.title.empty()) {
            continue;
        }
        blocks.push_back(block);
    }
    return blocks;
}

std::string assembleScript(const std::vector<PythonBlock> &blocks,
                           const std::vector<std::string> &bodies,
                           const std::vector<std::string> &headerLines,
                           const std::string &eggSaying)
{
    std::string out;
    out += "#!/usr/bin/env python3\n";
    out += "# -*- coding: utf-8 -*-\n";
    for (const std::string &line : headerLines) {
        out += "# " + line + "\n";
    }
    out += "#\n";
    out += "# 本脚本由 WhalePet（桌宠工作台）按所选功能块拼接生成，**未经运行验证**；\n";
    out += "# 使用前请自行检查与备份。\n";
    out += "\n";

    const std::size_t count = (blocks.size() < bodies.size()) ? blocks.size() : bodies.size();
    for (std::size_t i = 0; i < count; ++i) {
        const PythonBlock &block = blocks[i];
        out += "# ==================== " + block.title + "（" + block.id + "） ====================\n";
        if (!block.summary.empty()) {
            out += "# " + block.summary + "\n";
        }
        out += "\n";
        out += ensureTrailingNewline(bodies[i]);
        out += "\n";
    }

    out += dispatcher(blocks);

    if (!eggSaying.empty()) {
        const std::string comment = eggCommentLine(eggSaying);
        if (!comment.empty()) {
            out += "\n" + comment + "\n";
        }
    }
    return out;
}

} // namespace whalepet::core
