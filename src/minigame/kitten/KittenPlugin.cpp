#include "minigame/kitten/KittenPlugin.h"

#include "core/RobotKitten.h"
#include "minigame/kitten/KittenView.h"
#include "model/Database.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"

namespace whalepet {

MiniGameInfo KittenPlugin::info() const
{
    MiniGameInfo info;
    info.id = QStringLiteral("kitten");
    info.displayName = QStringLiteral("鲸鱼娘找小猫");
    info.menuLabel = QStringLiteral("找小猫");
    info.description = QStringLiteral(
        "和 Robot Finds Kitten 一样的地图探索：用方向键 / WASD（或点击相邻格）带鲸鱼娘走迷宫，"
        "撞到礁石会被弹回来，走到扇贝、海龟这些物件上会触发专属台词；"
        "顺着海流（>）切换场景，在最深处找到小猫就算通关。"
        "物体列表（assets/maps/kitten_objects.txt）、地图与台词（assets/lines/kitten.txt）"
        "都是外部资源，可自行增删替换。");
    return info;
}

MiniGameView *KittenPlugin::createView(const MiniGameContext &ctx, QWidget *parent)
{
    return new KittenView(ctx, parent);
}

QString KittenPlugin::configSummary(model::Database *db) const
{
    // 上次选择的难度，与游戏窗口内的持久化口径一致（json_ext 的 kitten_difficulty）
    int index = 0;
    if (db != nullptr && db->isOpen()) {
        model::SettingsRepo repo(db);
        model::SettingsData data;
        if (repo.load(data)) {
            index = data.kittenDifficulty;
        }
    }
    const core::RfkDifficulty difficulty = core::rfkDifficultyOfIndex(index);
    return QStringLiteral("上次难度：%1 · %2 个场景")
        .arg(QString::fromUtf8(core::rfkDifficultyName(difficulty)))
        .arg(core::rfkRoomCount(difficulty));
}

} // namespace whalepet
