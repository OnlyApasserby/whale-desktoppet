#pragma once

// 小游戏插件：扫雷。
//
// 本类是「扫雷」接入宿主的最小外壳：只提供元数据、视图工厂与配置摘要，
// 玩法逻辑在 core::Minesweeper，界面在 MinesweeperView。
// 新增其它小游戏可完全照此模板实现，无需触碰宿主或结算服务。

#include "minigame/MiniGamePlugin.h"

namespace whalepet {

class MinesweeperPlugin final : public IMiniGamePlugin {
public:
    MiniGameInfo info() const override;
    MiniGameView *createView(const MiniGameContext &ctx, QWidget *parent) override;
    QString configSummary(model::Database *db) const override;
    QList<QPair<QString, QString>> legacyBestRecords() const override;
};

} // namespace whalepet
