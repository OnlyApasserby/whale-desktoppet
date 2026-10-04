#include "core/CodeEasterEgg.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <regex>
#include <string>
#include <vector>

namespace whalepet::core {

namespace {

// 鲸鱼娘的俏皮话池。
// 审查约束（务必保持）：
//   - 不含换行（一行一条）；
//   - 不含块注释闭合符 "*/"（否则会把后续代码「注释」掉）；
//   - 不以反斜杠结尾（避免行注释续行吞掉下一行代码）；
//   - 不含裸的半角引号，避免放进任何语境时产生歧义。
const char *const kSayings[] = {
    "嘘——这是鲸鱼娘偷偷留下的小脚印，被你发现啦。",
    "路过这里的时候顺手藏了颗糖，辛苦写代码也要甜甜的。",
    "鲸鱼娘在这里打了个滚；放心，代码一行都没动。",
    "这一段我只看了一眼，真的没有悄悄改，真的。",
    "这行注释里藏着一小片海，允许你划一会儿水。",
    "主人敲代码的时候最帅了——鲸鱼娘说的，不许反驳。",
    "偷偷说：戳我一下，比戳这行注释有趣多了。",
    "这里没有 bug，只有一只路过的小鲸鱼。",
    "编译快是因为鲸鱼娘帮你吹了口气，别声张。",
    "记得喝水、起身、看看窗外；这既是彩蛋，也是提醒。",
    "鲸鱼娘把温柔存在这里，需要的时候自取。",
    "你发现我啦！奖励你：今天的一切都会顺顺利利。",
    "悄悄留下一个鲸鱼泡泡，戳破它就能继续加油。",
    "被藏了一点甜，禁止删掉——才怪，删了也没关系。",
};

constexpr std::size_t kSayingCount = sizeof(kSayings) / sizeof(kSayings[0]);

bool endsWithNewline(const std::string &s)
{
    return !s.empty() && s.back() == '\n';
}

std::string toLowerAscii(std::string value)
{
    for (char &c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

// 行切分：记录每行的「行首偏移 / 内容末尾偏移 / 下一行行首偏移」。
// 末尾若无换行，会额外产生一个空行，便于统一处理「注释段正好结束在 EOF」。
struct Line {
    std::size_t start = 0; // 行首字节偏移
    std::size_t end = 0;   // 行内容末尾（不含换行符）
    std::size_t next = 0;  // 下一行行首字节偏移
};

std::vector<Line> splitLines(const std::string &s)
{
    std::vector<Line> lines;
    std::size_t i = 0;
    while (i <= s.size()) {
        const std::size_t start = i;
        while (i < s.size() && s[i] != '\n') {
            ++i;
        }
        const std::size_t end = i;
        if (i < s.size()) {
            ++i; // 跳过 '\n'（'\r' 留在内容末尾，不影响正则匹配）
        }
        lines.push_back({start, end, i});
        if (start == s.size()) {
            break; // 源以 '\n' 结尾时，避免无限追加空行
        }
    }
    return lines;
}

// 「正则识别注释段落」的核心：行首（允许前导空白）是否为行注释标记。
bool isLineCommentStart(const std::string &lineText, bool python)
{
    static const std::regex kSlash(R"(^[ \t]*//)");
    static const std::regex kHash(R"(^[ \t]*#)");
    return std::regex_search(lineText, python ? kHash : kSlash);
}

std::string leadingIndent(const std::string &lineText)
{
    std::size_t n = 0;
    while (n < lineText.size() && (lineText[n] == ' ' || lineText[n] == '\t')) {
        ++n;
    }
    return lineText.substr(0, n);
}

std::size_t countOccurrences(const std::string &hay, const std::string &needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// 候选插入点：offset 处插入一行注释（缩进沿用 indent）。
struct Candidate {
    std::size_t offset = 0; // 插入位置（行注释段 = 段后一行的行首；块注释 = 闭合行行首）
    std::string indent;     // 插入行使用的缩进
    bool block = false;     // true = 多行块注释（插入 " * 话"），false = 行注释
};

// 行注释段 [firstLine, pastLastLine) → 一个候选（插入点落在段后一行的行首）
void pushLineRun(std::size_t firstLine, std::size_t pastLastLine,
                 const std::vector<Line> &lines, const std::string &source,
                 std::vector<Candidate> &out)
{
    const std::size_t offset =
        (pastLastLine < lines.size()) ? lines[pastLastLine].start : source.size();
    const std::string text =
        source.substr(lines[firstLine].start, lines[firstLine].end - lines[firstLine].start);
    out.push_back({offset, leadingIndent(text), false});
}

// C 系（.c/.cpp/.h）：识别 `//` 行注释段与多行 `/* ... */` 块注释。
// 用一次逐字符状态机区分「代码 / 行注释 / 块注释 / 字符串 / 字符字面量」，
// 从而避免把字符串里的 `//`、或块注释内部误当成行注释段——保证插入点一定在注释里。
std::vector<Candidate> candidatesForCLike(const std::string &source,
                                          const std::vector<Line> &lines)
{
    enum class St { Code, Line, Block, Str, Chr };

    std::vector<Candidate> out;
    St st = St::Code;
    std::size_t blockStartLine = 0;
    bool inRun = false;
    std::size_t runStart = 0;

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const Line &ln = lines[li];
        const std::string text = source.substr(ln.start, ln.end - ln.start);
        const bool enteredInCode = (st == St::Code);

        for (std::size_t k = ln.start; k < ln.end; ++k) {
            const char c = source[k];
            switch (st) {
            case St::Code:
                if (c == '/' && k + 1 < ln.end && source[k + 1] == '/') {
                    st = St::Line;
                    ++k;
                } else if (c == '/' && k + 1 < ln.end && source[k + 1] == '*') {
                    st = St::Block;
                    blockStartLine = li;
                    ++k;
                } else if (c == '"') {
                    st = St::Str;
                } else if (c == '\'') {
                    st = St::Chr;
                }
                break;
            case St::Line:
                break; // 行注释延续到行尾，行结束时统一复位
            case St::Block:
                if (c == '*' && k + 1 < ln.end && source[k + 1] == '/') {
                    // 闭合行开头必须是纯空白（` */` 形态），否则放弃该块以免破坏排版
                    const std::string before = source.substr(ln.start, k - ln.start);
                    if (li > blockStartLine && before.find_first_not_of(" \t") == std::string::npos) {
                        out.push_back({ln.start, before, true});
                    }
                    st = St::Code;
                    ++k;
                }
                break;
            case St::Str:
                if (c == '\\') {
                    if (k + 1 < ln.end) {
                        ++k; // 跳过被转义字符
                    }
                } else if (c == '"') {
                    st = St::Code;
                }
                break;
            case St::Chr:
                if (c == '\\') {
                    if (k + 1 < ln.end) {
                        ++k;
                    }
                } else if (c == '\'') {
                    st = St::Code;
                }
                break;
            }
        }

        // 行结束：行注释复位；未闭合字符串（如续行宏）保守复位，避免状态泄漏到后续行
        if (st == St::Line || st == St::Str || st == St::Chr) {
            st = St::Code;
        }

        const bool isCommentLine = enteredInCode && isLineCommentStart(text, false);
        if (isCommentLine) {
            if (!inRun) {
                inRun = true;
                runStart = li;
            }
        } else if (inRun) {
            pushLineRun(runStart, li, lines, source, out);
            inRun = false;
        }
    }
    if (inRun) {
        pushLineRun(runStart, lines.size(), lines, source, out);
    }
    return out;
}

// Python（.py）：识别 `#` 行注释段；跳过三引号字符串（docstring）内部的伪注释行。
std::vector<Candidate> candidatesForPython(const std::string &source,
                                           const std::vector<Line> &lines)
{
    std::vector<Candidate> out;
    bool inTriple = false;
    char tripleChar = '"';
    bool inRun = false;
    std::size_t runStart = 0;

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const Line &ln = lines[li];
        const std::string text = source.substr(ln.start, ln.end - ln.start);

        if (inTriple) {
            const std::string delim(3, tripleChar);
            if (text.find(delim) != std::string::npos) {
                inTriple = false; // 简化：行内出现闭合符即视为闭合（够用且安全）
            }
            if (inRun) {
                pushLineRun(runStart, li, lines, source, out);
                inRun = false;
            }
            continue;
        }

        if (isLineCommentStart(text, true)) {
            if (!inRun) {
                inRun = true;
                runStart = li;
            }
            continue; // 注释行里的三引号一律忽略
        }

        if (inRun) {
            pushLineRun(runStart, li, lines, source, out);
            inRun = false;
        }

        // 判断该行是否开启了未闭合的三引号字符串（奇数出现即视为开启）
        if (countOccurrences(text, "\"\"\"") % 2 == 1) {
            inTriple = true;
            tripleChar = '"';
        } else if (countOccurrences(text, "'''") % 2 == 1) {
            inTriple = true;
            tripleChar = '\'';
        }
    }
    if (inRun) {
        pushLineRun(runStart, lines.size(), lines, source, out);
    }
    return out;
}

std::size_t lineIndexOfOffset(const std::string &source, std::size_t offset)
{
    std::size_t line = 0;
    const std::size_t limit = std::min(offset, source.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (source[i] == '\n') {
            ++line;
        }
    }
    return line;
}

} // namespace

const char *const *codeEggSayings(std::size_t *count)
{
    if (count != nullptr) {
        *count = kSayingCount;
    }
    return kSayings;
}

bool isCodeEggSupportedExtension(const std::string &extension)
{
    const std::string ext = toLowerAscii(extension);
    return ext == ".py" || ext == ".c" || ext == ".cpp" || ext == ".h";
}

CodeEggResult injectCodeEgg(const std::string &source, const std::string &extension,
                            std::size_t sayingIndex)
{
    CodeEggResult result;
    result.content = source;
    result.sayingIndex = sayingIndex % kSayingCount;

    // 幂等：已经藏过就不动（同一文件不会被反复塞）
    if (source.find(kCodeEggMarker) != std::string::npos) {
        return result;
    }
    if (!isCodeEggSupportedExtension(extension)) {
        return result;
    }

    const std::string ext = toLowerAscii(extension);
    const bool python = (ext == ".py");
    const std::vector<Line> lines = splitLines(source);
    const std::vector<Candidate> candidates =
        python ? candidatesForPython(source, lines) : candidatesForCLike(source, lines);

    if (candidates.empty()) {
        return result; // 没有注释段落 → 不改动（宁可藏不进去，也不破坏代码）
    }

    const Candidate &cand = candidates[result.sayingIndex % candidates.size()];
    // 行尾缀上幂等标记：下次注入前据此判断「这行已经藏过了」
    const std::string saying =
        std::string(kSayings[result.sayingIndex]) + "  (" + kCodeEggMarker + ")";
    const std::string eol = (source.find("\r\n") != std::string::npos) ? "\r\n" : "\n";

    std::string inserted;
    if (cand.block) {
        // 多行块注释：在闭合行之前插入 " * 话"
        inserted = cand.indent + "* " + saying + eol;
    } else {
        const std::string prefix = python ? "# " : "// ";
        if (cand.offset >= source.size() && !endsWithNewline(source)) {
            inserted = eol + cand.indent + prefix + saying;
        } else {
            inserted = cand.indent + prefix + saying + eol;
        }
    }

    result.content = source.substr(0, cand.offset) + inserted + source.substr(cand.offset);
    result.changed = true;
    result.insertedLine = static_cast<int>(lineIndexOfOffset(source, cand.offset));
    return result;
}

} // namespace whalepet::core
