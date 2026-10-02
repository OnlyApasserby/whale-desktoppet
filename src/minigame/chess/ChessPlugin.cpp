#include "minigame/chess/ChessPlugin.h"

#include "core/Chess.h"
#include "minigame/chess/ChessView.h"
#include "minigame/chess/UciEngine.h"
#include "model/Database.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"

#include <QFileInfo>

namespace whalepet {

MiniGameInfo ChessPlugin::info() const
{
    MiniGameInfo info;
    info.id = QStringLiteral("chess");
    info.displayName = QStringLiteral("国际象棋");
    info.menuLabel = QStringLiteral("国际象棋");
    info.description = QStringLiteral(
        "和鲸鱼娘下一局国际象棋：你执白或执黑，对手是**外部 UCI 象棋引擎**"
        "（如 Stockfish，程序不自带棋力）。请把引擎可执行文件放进安装目录的 engine 文件夹，"
        "或在窗口内手动指定引擎路径；难度决定引擎棋力与思考时间。详见 README「国际象棋引擎」。");
    return info;
}

MiniGameView *ChessPlugin::createView(const MiniGameContext &ctx, QWidget *parent)
{
    return new ChessView(ctx, parent);
}

QString ChessPlugin::configSummary(model::Database *db) const
{
    int levelIndex = 0;
    bool humanIsWhite = true;
    QString enginePath;
    if (db != nullptr && db->isOpen()) {
        model::SettingsRepo repo(db);
        model::SettingsData data;
        if (repo.load(data)) {
            levelIndex = data.chessDifficulty;
            humanIsWhite = data.chessHumanIsWhite;
            enginePath = data.chessEnginePath;
        }
    }
    if (levelIndex < 0 || levelIndex >= core::kChessLevelCount) {
        levelIndex = 0;
    }
    if (enginePath.isEmpty()) {
        enginePath = defaultChessEnginePath();
    }

    const core::ChessLevel level = core::chessLevelOfIndex(levelIndex);
    const QString engineText = enginePath.isEmpty()
                                   ? QStringLiteral("未配置（需自行准备 UCI 引擎）")
                                   : QFileInfo(enginePath).fileName();
    return QStringLiteral("棋盘难度：%1 · 你执%2 · 引擎：%3")
        .arg(QString::fromUtf8(level.label),
             humanIsWhite ? QStringLiteral("白") : QStringLiteral("黑"), engineText);
}

} // namespace whalepet
