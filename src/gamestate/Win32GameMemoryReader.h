#pragma once

// Windows 只读进程内存读取器（IGameMemoryReader 的具体实现）。
// 归属：docs/ROADMAP-ex1.md §2.6.2、§六 6.2。
// 【红线】仅申请 PROCESS_VM_READ | PROCESS_QUERY_INFORMATION；不提供任何写入能力。
// 非 Windows 平台编译为「明确不支持」的桩（attach 返回 false + 原因），保证工程仍可构建。

#include "gamestate/IGameMemoryReader.h"

namespace whalepet::gamestate {

// Windows 实现：OpenProcess(只读取权限) + ReadProcessMemory + Toolhelp32 模块枚举。
class Win32GameMemoryReader final : public IGameMemoryReader {
public:
    Win32GameMemoryReader();
    ~Win32GameMemoryReader() override;

    Win32GameMemoryReader(const Win32GameMemoryReader &) = delete;
    Win32GameMemoryReader &operator=(const Win32GameMemoryReader &) = delete;

    bool attach(const GameProfile &profile, QString *error) override;
    void detach() override;
    bool attached() const override;
    std::uint64_t moduleBase(const std::string &moduleName) const override;
    bool read(std::uint64_t address, void *buffer, std::size_t size) override;
    QString lastError() const override;
    int processId() const override;

    // 目标是否 32 位（WOW64）；attach 成功后有效
    bool targetIs32Bit() const;
    // 本平台是否具备只读读取能力（Windows 为 true）
    static bool isSupported();

private:
    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace whalepet::gamestate
