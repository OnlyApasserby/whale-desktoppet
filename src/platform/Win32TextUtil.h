#pragma once

// Win32 采集的**纯文本处理**：刻意不含任何 Windows API，故可脱系统单测。
//
// 归属：docs/ROADMAP-P7.md P7.1；隐私边界见 docs/CONTEXT-API.md §5。
//
// 两件事：
//   * 宽字符（UTF-16）→ UTF-8：Windows 的窗口标题与进程路径都是 UTF-16，
//     而 core::EnvSample 统一以 UTF-8 std::string 承载（与台词库/日志一致），
//     含中文标题时**不可**直接窄化（会得到问号乱码）；
//   * 路径取文件名：对外暴露的 env.appId 只给进程名，不泄露安装路径。

#include <string>

namespace whalepet::platform::win32 {

// UTF-16 宽字符串 → UTF-8；空串原样返回
std::string toUtf8(const std::wstring &wide);

// 取路径的文件名部分（同时识别 `\` 与 `/`；空串或**以分隔符结尾**时返回空串）
std::string fileNameFromPath(const std::string &path);

} // namespace whalepet::platform::win32
