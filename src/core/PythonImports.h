#pragma once

// EX8 学习页的 **import 归位与去重**（工作台「学习」栏的拼接预览口径）——docs/STUDY.md §4。
// 零 Qt 依赖：纯文本处理，可脱离界面单测。
//
// 口径（宁缺毋滥，**不猜测 Python 语义**）：
//   * 只搬「顶层」语句：行首无空白（缩进的 import 属函数 / 类体，一律不动）；
//   * 只搬「单行完整」语句：括号 / 方括号 / 花括号必须自身闭合，且行尾不是反斜杠续行
//     ——跨行语句保持原位，宁可少搬也不破坏语法；
//   * 只搬 `import …` 与 `from … import …` 两类语句；
//   * 归位位置：**注释头之后**（shebang / 编码行 / 生成说明等前导注释与空行留在最上方），
//     即 import 成为文件的第一批语句；
//   * 去重：按「去掉行尾注释 + 折叠空白」后的语句文本比较，**保留首次出现的原文**；
//   * 无 import 时**原样返回**（零改动）；有 import 时结果幂等（再跑一次不变）。

#include <string>

namespace whalepet::core {

// 把顶层 import 语句归位到文件头（注释头之后）并去除重复语句。
std::string hoistImports(const std::string &code);

} // namespace whalepet::core
