#include "gamestate/Win32GameMemoryReader.h"

#include <QString>

#include <algorithm>
#include <map>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace whalepet::gamestate {

#if defined(Q_OS_WIN)

namespace {

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return value;
}

QString win32ErrorText(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                            | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0,
                                        nullptr);
    QString text;
    if (length != 0 && buffer != nullptr) {
        text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
        LocalFree(buffer);
    }
    if (text.isEmpty()) {
        text = QStringLiteral("Win32 错误 %1").arg(code);
    }
    return text;
}

std::uint32_t findProcessId(const std::string &processName, QString *error)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = QStringLiteral("进程快照失败：%1").arg(win32ErrorText(GetLastError()));
        }
        return 0;
    }
    const std::string wanted = toLowerAscii(processName);
    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    std::uint32_t found = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString name = QString::fromWCharArray(entry.szExeFile);
            if (toLowerAscii(name.toStdString()) == wanted) {
                found = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (found == 0 && error != nullptr) {
        *error = QStringLiteral("未找到进程：%1").arg(QString::fromStdString(processName));
    }
    return found;
}

} // namespace

struct Win32GameMemoryReader::Impl {
    void *handle = nullptr;
    int pid = 0;
    bool is32 = false;
    std::map<std::string, std::uint64_t> modules;
    QString error;
};

Win32GameMemoryReader::Win32GameMemoryReader()
    : m_impl(new Impl)
{
}

Win32GameMemoryReader::~Win32GameMemoryReader()
{
    detach();
    delete m_impl;
}

bool Win32GameMemoryReader::isSupported()
{
    return true;
}

bool Win32GameMemoryReader::attach(const GameProfile &profile, QString *error)
{
    detach();
    m_impl->error.clear();

    if (profile.process.empty()) {
        if (error != nullptr) {
            *error = QStringLiteral("未指定 process");
        }
        return false;
    }

    QString findError;
    const std::uint32_t pid = findProcessId(profile.process, &findError);
    if (pid == 0) {
        m_impl->error = findError;
        if (error != nullptr) {
            *error = findError;
        }
        return false;
    }

    // 【只读】只申请读取 + 查询权限；不申请任何写权限
    const HANDLE handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (handle == nullptr) {
        const QString message = QStringLiteral("打开进程 %1（pid %2）失败：%3")
                                    .arg(QString::fromStdString(profile.process))
                                    .arg(pid)
                                    .arg(win32ErrorText(GetLastError()));
        m_impl->error = message;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }

    BOOL targetWow = FALSE;
    BOOL selfWow = FALSE;
    IsWow64Process(handle, &targetWow);
    IsWow64Process(GetCurrentProcess(), &selfWow);
    const bool target32 = (targetWow != FALSE) && (selfWow == FALSE);
    if (target32) {
        // 32 位（WOW64）目标：本阶段明确不支持，交回提示而非静默错误读取
        CloseHandle(handle);
        const QString message =
            QStringLiteral("暂不支持 32 位（WOW64）目标进程：%1").arg(QString::fromStdString(profile.process));
        m_impl->error = message;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }

    m_impl->handle = handle;
    m_impl->pid = static_cast<int>(pid);
    m_impl->is32 = target32;

    const HANDLE moduleSnapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (moduleSnapshot == INVALID_HANDLE_VALUE) {
        const QString message = QStringLiteral("模块快照失败：%1").arg(win32ErrorText(GetLastError()));
        detach();
        m_impl->error = message;
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }
    MODULEENTRY32W module {};
    module.dwSize = sizeof(module);
    if (Module32FirstW(moduleSnapshot, &module)) {
        do {
            const std::string name = toLowerAscii(QString::fromWCharArray(module.szModule).toStdString());
            m_impl->modules[name] = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(module.modBaseAddr));
        } while (Module32NextW(moduleSnapshot, &module));
    }
    CloseHandle(moduleSnapshot);

    if (!profile.module.empty()) {
        const std::string wanted = toLowerAscii(profile.module);
        if (m_impl->modules.find(wanted) == m_impl->modules.end()) {
            const QString message = QStringLiteral("进程 %1 中未找到模块：%2")
                                        .arg(QString::fromStdString(profile.process),
                                             QString::fromStdString(profile.module));
            detach();
            m_impl->error = message;
            if (error != nullptr) {
                *error = message;
            }
            return false;
        }
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void Win32GameMemoryReader::detach()
{
    if (m_impl == nullptr || m_impl->handle == nullptr) {
        if (m_impl != nullptr) {
            m_impl->pid = 0;
            m_impl->modules.clear();
        }
        return;
    }
    CloseHandle(static_cast<HANDLE>(m_impl->handle));
    m_impl->handle = nullptr;
    m_impl->pid = 0;
    m_impl->is32 = false;
    m_impl->modules.clear();
}

bool Win32GameMemoryReader::attached() const
{
    return m_impl != nullptr && m_impl->handle != nullptr;
}

std::uint64_t Win32GameMemoryReader::moduleBase(const std::string &moduleName) const
{
    if (!attached()) {
        return 0;
    }
    const auto it = m_impl->modules.find(toLowerAscii(moduleName));
    return it == m_impl->modules.end() ? 0 : it->second;
}

bool Win32GameMemoryReader::read(std::uint64_t address, void *buffer, std::size_t size)
{
    if (!attached() || address == 0 || buffer == nullptr || size == 0) {
        return false;
    }
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(static_cast<HANDLE>(m_impl->handle),
                           reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), buffer,
                           static_cast<SIZE_T>(size), &bytesRead)
        || bytesRead != static_cast<SIZE_T>(size)) {
        m_impl->error = QStringLiteral("读取 0x%1（%2 字节）失败：%3")
                            .arg(address, 0, 16)
                            .arg(size)
                            .arg(win32ErrorText(GetLastError()));
        return false;
    }
    return true;
}

QString Win32GameMemoryReader::lastError() const
{
    return m_impl != nullptr ? m_impl->error : QString();
}

int Win32GameMemoryReader::processId() const
{
    return m_impl != nullptr ? m_impl->pid : 0;
}

bool Win32GameMemoryReader::targetIs32Bit() const
{
    return m_impl != nullptr && m_impl->is32;
}

#else // !Q_OS_WIN —— 明确不支持的桩，保证工程跨平台仍可构建

struct Win32GameMemoryReader::Impl {
    QString error;
};

Win32GameMemoryReader::Win32GameMemoryReader()
    : m_impl(new Impl)
{
}

Win32GameMemoryReader::~Win32GameMemoryReader()
{
    delete m_impl;
}

bool Win32GameMemoryReader::isSupported()
{
    return false;
}

bool Win32GameMemoryReader::attach(const GameProfile &, QString *error)
{
    if (error != nullptr) {
        *error = QStringLiteral("当前平台不支持只读进程内存读取");
    }
    return false;
}

void Win32GameMemoryReader::detach() {}
bool Win32GameMemoryReader::attached() const { return false; }
std::uint64_t Win32GameMemoryReader::moduleBase(const std::string &) const { return 0; }
bool Win32GameMemoryReader::read(std::uint64_t, void *, std::size_t) { return false; }
QString Win32GameMemoryReader::lastError() const { return m_impl->error; }
int Win32GameMemoryReader::processId() const { return 0; }
bool Win32GameMemoryReader::targetIs32Bit() const { return false; }

#endif

} // namespace whalepet::gamestate
