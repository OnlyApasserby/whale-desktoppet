#pragma once

// 小游戏插件「陪玩状态自描述」可选接口（**插件侧**唯一新增点）。
//
// 设计目标（EX4）：新增一个小游戏时，陪玩侧**零改动**即可接入。
//   1) 小游戏**自己**把私有状态折算为中立的 core::GameSnapshot（本接口，由视图实现）；
//   2) 陪玩侧按「通用聚合」消费（见 viewmodel::MiniGameCompanionSource），
//      不认识任何具体玩法。
//
// 兼容红线：本接口是**独立可选接口**，不修改 IMiniGamePlugin / MiniGameView /
// MiniGameRegistry / MiniGameContext / MiniGameInfo 的任何签名 —— 既有菜单、设置页、
// 结算链路与全部既有用例零回归（见 docs/PLUGIN-ARCHITECTURE.md §7）。
//
// 由谁实现：小游戏**视图**（状态对象在视图内）；未实现的游戏只是不参与陪玩，
// 其余功能（菜单 / 结算 / 成就）照常。

#include "core/GameSnapshot.h"

namespace whalepet {

class IMiniGameCompanionSource {
public:
    virtual ~IMiniGameCompanionSource() = default;

    // 填充当前陪玩状态。
    //   * 返回 true  ：out 已按中立契约填充（available 表示本帧是否有可用读数）；
    //   * 返回 false ：本帧无可用读数 —— 陪玩侧如实降级，**绝不伪造**。
    // 实现应为**只读**且极轻（陪玩侧约 200ms 采样一次），不得弹窗 / 阻塞。
    virtual bool companionSnapshot(core::GameSnapshot *out) const = 0;
};

} // namespace whalepet
