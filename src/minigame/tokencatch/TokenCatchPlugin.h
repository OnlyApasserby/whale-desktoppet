#pragma once

// 小游戏插件：接 Token。
//
// 本类是「接 Token」接入宿主的最小外壳：只提供元数据、视图工厂与配置摘要，
// 玩法逻辑在 core::TokenCatch，界面在 TokenCatchView。
// 它同时是 **EX4「新增小游戏零改动陪玩」的实战测试用例**：视图额外实现
// IMiniGameCompanionSource，陪玩侧与宿主一行不改即可被通用聚合接入
// （见 docs/MINIGAME-INTERFACE.md §12）。

#include "minigame/MiniGamePlugin.h"

namespace whalepet {

class TokenCatchPlugin final : public IMiniGamePlugin {
public:
    MiniGameInfo info() const override;
    MiniGameView *createView(const MiniGameContext &ctx, QWidget *parent) override;
    QString configSummary(model::Database *db) const override;
};

} // namespace whalepet
