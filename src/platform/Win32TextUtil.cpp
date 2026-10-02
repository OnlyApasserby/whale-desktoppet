#include "platform/Win32TextUtil.h"

#include <QString>

namespace whalepet::platform::win32 {

std::string toUtf8(const std::wstring &wide)
{
    if (wide.empty()) {
        return std::string();
    }
    // QString::fromWCharArray 按 UTF-16 解码（MSVC 下 wchar_t 即 UTF-16），
    // toStdString() 输出 UTF-8——中文标题不会乱码。
    const QString text = QString::fromWCharArray(wide.data(), static_cast<int>(wide.size()));
    return text.toStdString();
}

std::string fileNameFromPath(const std::string &path)
{
    if (path.empty()) {
        return std::string();
    }
    const std::size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return path; // 已是纯文件名
    }
    if (slash + 1 >= path.size()) {
        return std::string(); // "C:\dir\" 这类结尾分隔符：没有文件名，不猜
    }
    return path.substr(slash + 1);
}

} // namespace whalepet::platform::win32
