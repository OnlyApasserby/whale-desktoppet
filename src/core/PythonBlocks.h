#pragma once

// P10 预制 Python 功能块（工作能力插件的内容层）——**零 Qt 依赖**，可脱 UI 单测。
//
// 契约（docs/ARCHITECTURE.md 附录 D.4）：
//   - 内容**唯一索引** `assets/python/blocks/index.txt`：每行 `id|标题|类别|一句话说明|依赖`；
//     `#` 与空行跳过，字段不全 / id 非法的行**跳过**（宁缺毋滥）；
//   - 块正文 `assets/python/blocks/<id>.txt`：**整块原样落盘**（组装只加注释头、分隔与末尾
//     分发器），因此「生成的脚本语法正确」由块自身保证，本层**不做任何改写**；
//   - 每个块必须定义入口函数 `def run_<id 的 '-' 换 '_'>():`（见 blockEntryName），
//     末尾分发器据此调用；契约由 blockBodyHasEntry() 校验（资源测试逐块断言）；
//   - 组装出的脚本**不执行、不联网、不做运行验证**（脚本头会如实标注）。

#include "core/CodeEasterEgg.h"

#include <string>
#include <vector>

namespace whalepet::core {

struct PythonBlock {
    std::string id;       // 唯一 id（= 正文文件名，不含扩展名）
    std::string title;    // 面板列表标题
    std::string category; // 类别（入门 / 文件 / 数据 / 网络 …）
    std::string summary;  // 一句话说明
    std::string requires; // 依赖说明（如「标准库」/「标准库（联网）」）
};

// 解析唯一索引文本（每行 `id|标题|类别|说明|依赖`；依赖缺省为「标准库」）
std::vector<PythonBlock> parseBlockIndex(const std::string &text);

// id 是否合法：非空、仅 [a-z0-9-]、首字符不是 '-'
bool isValidBlockId(const std::string &id);

// 入口函数名：id 的 '-' → '_' 并加前缀 `run_`；id 非法时返回空串
std::string blockEntryName(const std::string &id);

// 正文是否包含 `def run_<entry>(`（块契约；id 非法返回 false）
bool blockBodyHasEntry(const std::string &body, const std::string &id);

// 组装脚本（见文件头的契约）：
//   - headerLines 逐行加 `# ` 前缀写入文件头（生成时间 / 会话时长 / 块清单 / 免责声明由调用方给）；
//   - blocks 与 bodies 一一对应（同序）；个数不匹配时按较短者处理；
//   - eggSaying 非空时在**文件末尾**追加一行注释（含 kCodeEggMarker，见 eggCommentLine）。
std::string assembleScript(const std::vector<PythonBlock> &blocks,
                           const std::vector<std::string> &bodies,
                           const std::vector<std::string> &headerLines,
                           const std::string &eggSaying);

} // namespace whalepet::core
