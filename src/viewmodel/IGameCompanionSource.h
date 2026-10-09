#pragma once

// EX3：陪玩数据源的中立抽象（取代已归档的 gamestate::IGameStateAdapter）。
//
// 任何「能提供 core::GameSample」的数据源都可实现本接口：
//   - EX4：**进程内小游戏状态源**（读扫雷 / 找小猫 / 象棋的状态对象）；
//   - 若将来需要再接外部进程，可在 dump/src/gamestate（已归档）的基础上另建实现，
//     而**无需改动**判定（GameCompanionRules）、状态机通道或本服务编排。
//
// 【契约】
//   - read() 失败必须返回 false，并把 out->available 置 false（**绝不伪造数据**）；
//   - invalidated() == true 表示「连续失败已判失效」，上层据此自动停用并提示。

#include "core/GameState.h"

#include <QString>

#include <functional>
#include <memory>

namespace whalepet::viewmodel {

class IGameCompanionSource {
public:
    virtual ~IGameCompanionSource() = default;

    virtual bool attach(QString *error) = 0;
    virtual void detach() = 0;
    virtual bool attached() const = 0;
    virtual bool read(core::GameSample *out, QString *error) = 0;

    // 连续读取失败后是否已判定失效（默认永不失效，由具体数据源覆写）。
    virtual bool invalidated() const { return false; }
};

// 数据源工厂：在 GameCompanionService::start() 前注入（测试用假数据源；EX4 注入小游戏源）。
using GameCompanionSourceFactory =
    std::function<std::unique_ptr<IGameCompanionSource>(QString *error)>;

} // namespace whalepet::viewmodel
