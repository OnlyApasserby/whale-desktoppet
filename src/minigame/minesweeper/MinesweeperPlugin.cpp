#include "minigame/minesweeper/MinesweeperPlugin.h"

#include "core/Minesweeper.h"
#include "minigame/minesweeper/MinesweeperView.h"
#include "model/Database.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"

namespace whalepet {

MiniGameInfo MinesweeperPlugin::info() const
{
    MiniGameInfo info;
    info.id = QStringLiteral("minesweeper");
    info.displayName = QStringLiteral("扫雷");
    info.menuLabel = QStringLiteral("扫雷");
    info.description = QStringLiteral(
        "扫雷内置三档预设（初级 9×9·10 雷 / 中级 16×16·40 雷 / 高级 30×16·99 雷），"
        "也可自定义尺寸与雷数；难度在游戏窗口内切换，当前难度与参数实时显示。");
    return info;
}

MiniGameView *MinesweeperPlugin::createView(const MiniGameContext &ctx, QWidget *parent)
{
    return new MinesweeperView(ctx, parent);
}

QString MinesweeperPlugin::configSummary(model::Database *db) const
{
    // 上次选择的难度（预设或自定义参数），与游戏窗口内的持久化口径一致
    core::MineConfig cfg = core::mineConfigOfPreset(core::MinePreset::Beginner);
    if (db != nullptr && db->isOpen()) {
        model::SettingsRepo repo(db);
        model::SettingsData data;
        if (repo.load(data)) {
            if (data.minigamePreset == static_cast<int>(core::MinePreset::Custom)) {
                cfg = core::MineConfig{data.minigameCustomWidth, data.minigameCustomHeight,
                                       data.minigameCustomMines};
            } else {
                cfg = core::mineConfigOfPreset(static_cast<core::MinePreset>(data.minigamePreset));
            }
        }
    }
    if (!core::mineConfigValid(cfg)) {
        cfg = core::mineConfigOfPreset(core::MinePreset::Beginner);
    }
    return QStringLiteral("上次难度：%1").arg(QString::fromStdString(core::mineConfigLabel(cfg)));
}

QList<QPair<QString, QString>> MinesweeperPlugin::legacyBestRecords() const
{
    // v0.2.0 的个人最快纪录键为 game.best_ms_<MinePreset 整数值>（0 初级 / 1 中级 / 2 高级 / 3 自定义）；
    // 插件化后键格式改为 game.best_ms_<gameId>/<difficultyId>，此处声明迁移表避免升级丢纪录。
    return {
        {QStringLiteral("0"), QString::fromLatin1(core::minePresetId(core::MinePreset::Beginner))},
        {QStringLiteral("1"),
         QString::fromLatin1(core::minePresetId(core::MinePreset::Intermediate))},
        {QStringLiteral("2"), QString::fromLatin1(core::minePresetId(core::MinePreset::Expert))},
        {QStringLiteral("3"), QString::fromLatin1(core::minePresetId(core::MinePreset::Custom))},
    };
}

} // namespace whalepet
