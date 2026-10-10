#include "minigame/tokencatch/TokenCatchPlugin.h"

#include "core/TokenCatch.h"
#include "minigame/tokencatch/TokenCatchView.h"

namespace whalepet {

MiniGameInfo TokenCatchPlugin::info() const
{
    MiniGameInfo info;
    info.id = QStringLiteral("tokencatch");
    info.displayName = QStringLiteral("接Token");
    info.menuLabel = QStringLiteral("接Token");
    info.description = QStringLiteral(
        "接住从上方落下的 Token（界面字「币」）+1 分，别接到白饭（界面字「饭」）——"
        "接到白饭本局立即结束，并播放鲸鱼娘野餐立绘（daily-picnic）；"
        "达成目标 Token 数即通关，三档难度目标 12 / 20 / 30，时限 60 秒。");
    return info;
}

MiniGameView *TokenCatchPlugin::createView(const MiniGameContext &ctx, QWidget *parent)
{
    return new TokenCatchView(ctx, parent);
}

QString TokenCatchPlugin::configSummary(model::Database *db) const
{
    Q_UNUSED(db);
    // 本插件不落库难度配置（每次打开按窗口内当前选择开局），这里只给出难度口径说明，
    // 避免设置页出现空白「上次配置」行。
    QString summary = QStringLiteral("三档难度（窗口内切换）：");
    for (int i = 0; i < core::kTokenCatchPresetCount; ++i) {
        if (i > 0) {
            summary += QStringLiteral(" / ");
        }
        summary += QStringLiteral("%1 目标 %2")
                       .arg(QString::fromUtf8(core::kTokenCatchPresets[i].name))
                       .arg(core::kTokenCatchPresets[i].targetTokens);
    }
    return summary;
}

} // namespace whalepet
