#pragma once

// 小游戏插件：国际象棋。
//
// 本类是「国际象棋」接入宿主的最小外壳：只提供元数据、视图工厂与配置摘要。
// 玩法规则在 core::ChessGame（零 Qt），引擎通信用 UciEngine（QProcess + UCI），
// 界面在 ChessView。宿主（PetWindow / SettingsDialog）与结算服务无需任何改动。

#include "minigame/MiniGamePlugin.h"

namespace whalepet {

class ChessPlugin final : public IMiniGamePlugin {
public:
    MiniGameInfo info() const override;
    MiniGameView *createView(const MiniGameContext &ctx, QWidget *parent) override;
    QString configSummary(model::Database *db) const override;
};

} // namespace whalepet
