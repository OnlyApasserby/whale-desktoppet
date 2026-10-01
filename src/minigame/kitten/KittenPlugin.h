#pragma once

// 小游戏插件：鲸鱼娘找小猫（Robot Finds Kitten 风格的地图探索）。
//
// 本类是「找小猫」接入宿主的最小外壳：只提供元数据、视图工厂与配置摘要，
// 玩法逻辑在 core::RfkWorld（零 Qt），界面在 KittenView。
// 完全照扫雷插件（src/minigame/minesweeper/）的模板实现，宿主与结算服务无需改动。

#include "minigame/MiniGamePlugin.h"

namespace whalepet {

class KittenPlugin final : public IMiniGamePlugin {
public:
    MiniGameInfo info() const override;
    MiniGameView *createView(const MiniGameContext &ctx, QWidget *parent) override;
    QString configSummary(model::Database *db) const override;
};

} // namespace whalepet
