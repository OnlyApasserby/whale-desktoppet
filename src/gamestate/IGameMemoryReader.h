#pragma once

// 只读进程内存访问抽象：便于「脱系统」单测（假实现）与跨平台优雅降级（非 Windows 桩）。
// 归属：docs/ROADMAP-ex1.md §2.6.2、§六 6.2。
// 【红线】接口本身**不提供任何写入能力**；实现必须只申请读权限。

#include "gamestate/GameProfile.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>

namespace whalepet::gamestate {

class IGameMemoryReader {
public:
    virtual ~IGameMemoryReader() = default;

    // 打开目标进程并建立模块表；失败填充 *error 并返回 false（不抛异常）
    virtual bool attach(const GameProfile &profile, QString *error) = 0;
    virtual void detach() = 0;
    virtual bool attached() const = 0;

    // 模块基址；未找到 / 未 attach 返回 0
    virtual std::uint64_t moduleBase(const std::string &moduleName) const = 0;

    // 读取目标内存；失败返回 false（不抛异常）
    virtual bool read(std::uint64_t address, void *buffer, std::size_t size) = 0;

    virtual QString lastError() const = 0;

    // 目标进程 id；未 attach 返回 0
    virtual int processId() const = 0;
};

} // namespace whalepet::gamestate

// 便于既有消费者（仅 include 本头）继续可见具体实现类；
// 声明体见 Win32GameMemoryReader.h。见 docs/ROADMAP-ex1.md §六 6.2。
#include "gamestate/Win32GameMemoryReader.h"
