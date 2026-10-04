#pragma once

// 代码彩蛋（experiment/easter-egg1）
//
// 玩法：对鲸鱼娘「戳一戳」时有 5% 概率，真的在用户工作区的源码里藏一句俏皮话。
//
// 本文件是**零 Qt 依赖的纯逻辑**（可脱 UI 单测），只做两件事：
//   1. 用**正则**识别注释段落（C 系 `//` 行注释段与 `/* */` 多行块注释；Python `#` 行注释段）；
//   2. 在一个选定的注释段末尾**追加一行注释**，内容为俏皮话。
//
// 安全边界（对应需求「不影响代码功能」）：
//   - **只追加注释行，绝不修改 / 删除任何既有代码**（插入位置全部落在已存在的注释段内）；
//   - 找不到注释段落就**不改动**（宁可藏不进去，也不破坏代码）；
//   - 只处理调用方限定的小体积 UTF-8 文本（二进制 / 超大文件由服务层先行排除）；
//   - **幂等**：源码中已含 kCodeEggMarker 时直接返回未改动，同一文件不会被反复塞。
//
// 支持的扩展名见 isCodeEggSupportedExtension()（.py / .c / .cpp / .h）。

#include <cstddef>
#include <string>

namespace whalepet::core {

// 幂等标记：注入的注释行**一定包含**该串（以行尾 "  (whalepet-egg)" 形式附加）；
// 再次注入前先查文件是否已含它，从而保证同一文件不会被反复塞。
inline constexpr const char *kCodeEggMarker = "whalepet-egg";

// 俏皮话池（鲸鱼娘语气）。集中在此便于统一审查安全性：
// 每条都不含换行、不含块注释闭合符 "*/"、不以反斜杠结尾，
// 故放进 `//`、`#`、`/* */` 任一注释里都合法，且不会把后续代码「注释掉」。
const char *const *codeEggSayings(std::size_t *count);

// 扩展名是否受支持；入参形如 ".cpp"（含点），大小写不敏感
bool isCodeEggSupportedExtension(const std::string &extension);

struct CodeEggResult {
    bool changed = false;         // 是否产生了改动
    std::string content;          // 改动后的内容（未改动时 == 原内容）
    int insertedLine = -1;        // 插入行的 0 基行号（-1 = 未插入）
    std::size_t sayingIndex = 0;  // 实际使用的俏皮话下标
};

// 在源码的注释段落里追加一句俏皮话。
//   - source    原文件内容（UTF-8）
//   - extension 扩展名（含点），用于选择注释语法
//   - sayingIndex 调用方（随机源）提供的索引，用于在话池与注释段中选取
// 返回结果；无注释段 / 已含标记 / 扩展名不支持时 changed == false 且 content == source。
CodeEggResult injectCodeEgg(const std::string &source, const std::string &extension,
                            std::size_t sayingIndex);

} // namespace whalepet::core
