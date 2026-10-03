#pragma once

// 引擎适配器统一抽象（§2.6）：把「怎么读」的差异全部收敛到 profile → 读数。
// 归属：docs/ROADMAP-ex1.md §2.6、§六 6.2。
// 【红线】读取失败一律 out->available=false + 原因，**绝不伪造 / 不写目标进程**。

#include "core/GameState.h"
#include "gamestate/GameProfile.h"

#include <QString>

#include <memory>

namespace whalepet::gamestate {

class IGameStateAdapter {
public:
    virtual ~IGameStateAdapter() = default;

    virtual bool attach(const GameProfile &profile, QString *error) = 0;
    virtual void detach() = 0;
    virtual bool attached() const = 0;

    // 读取一轮；返回 true 表示本轮有可用读数（out->available=true）
    virtual bool read(core::GameSample *out, QString *error) = 0;

    // 连续失败自检（§4.3）：置位后调用方应停止采样并提示重连/重载。
    // 默认 false（桥接等「失败即可用性下降」的通道无需失效标记）。
    virtual bool invalidated() const { return false; }
};

// 按 profile.engine 构造适配器；未知引擎 / 平台不支持返回 nullptr（调用方据此优雅降级）
std::unique_ptr<IGameStateAdapter> createGameStateAdapter(const GameProfile &profile,
                                                          QString *error);

} // namespace whalepet::gamestate
