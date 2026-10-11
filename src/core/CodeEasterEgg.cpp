#include "core/CodeEasterEgg.h"

#include <cstddef>
#include <string>

namespace whalepet::core {

namespace {

// 鲸鱼娘的俏皮话池（P10 起由工作会话使用：产出脚本时在文件末尾插入一行注释）。
// 审查约束（务必保持）：
//   - 不含换行（一行一条）；
//   - 不含块注释闭合符 "*/"；
//   - 不以反斜杠结尾（避免任何行注释续行吞掉下一行）；
//   - 不含裸的半角引号。
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

} // namespace

const char *const *codeEggSayings(std::size_t *count)
{
    if (count != nullptr) {
        *count = kSayingCount;
    }
    return kSayings;
}

std::string eggCommentLine(const std::string &saying)
{
    if (saying.empty()) {
        return {};
    }
    return "# " + saying + "  (" + kCodeEggMarker + ")";
}

} // namespace whalepet::core
