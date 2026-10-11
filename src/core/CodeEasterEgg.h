#pragma once

// 代码彩蛋 → P10 迁移说明（docs/ARCHITECTURE.md 附录 D.4）：
//   「戳一戳在源码注释里藏俏皮话」的**旧实现已退役**：原本**改写用户源码工作区**
//   （正则识别注释段 + 追加注释行 + QSaveFile 原子写回）的机制整体删除，迁移为
//   「工作会话内**至多一次** → 写入我们自己生成的 Python 脚本」（见 viewmodel::WorkService）。
//   保留仍要用的三样东西：俏皮话池 codeEggSayings()、幂等标记 kCodeEggMarker、
//   以及把俏皮话渲染成一行注释的 eggCommentLine()。
//   安全性因此提高：不再接触用户既有文件。

#include <cstddef>
#include <string>

namespace whalepet::core {

// 幂等标记：生成的注释行**一定包含**该串（以行尾 "  (whalepet-egg)" 形式附加）
inline constexpr const char *kCodeEggMarker = "whalepet-egg";

// 俏皮话池（鲸鱼娘语气）。审查约束（务必保持）：
//   - 不含换行（一行一条）；
//   - 不含块注释闭合符 "*/"（避免放进任何注释语境时产生歧义）；
//   - 不以反斜杠结尾；
//   - 不含裸的半角引号。
const char *const *codeEggSayings(std::size_t *count);

// 把一句俏皮话渲染成一行 Python 注释（行尾带 kCodeEggMarker）。
// 入参为空时返回空串（调用方据此跳过插入）。
std::string eggCommentLine(const std::string &saying);

} // namespace whalepet::core
