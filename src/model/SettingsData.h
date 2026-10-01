#pragma once

// settings（单例）行数据 —— 字段与 docs/DATA-MODEL.md §3.3、docs/SETTINGS.md §2 对应。

#include <QString>

namespace whalepet::model {

struct SettingsData {
    // 窗口位置；hasPosition == false 表示库中为 NULL（尚未写入过，需回默认位置）
    bool hasPosition = false;
    int posX = 0;
    int posY = 0;

    int poseSize = 200;
    bool bubbleEnabled = true;
    bool particlesEnabled = true;
    bool keywordAware = false;   // 默认关（CHAT.md）
    bool minigameEnabled = true; // 默认开（扫雷已实现；可关闭以隐藏入口）

    // ---- P6 新增设置项：按 SETTINGS.md §3「新增项写入 json_ext（JSON）」落库 ----
    // 不新建列，随 jsonExt 序列化；读写由 SettingsRepo 统一负责（缺省即默认值）。
    bool petEnabled = true;    // 桌宠显示开关（关闭后仍有唤回入口）
    bool nightQuiet = true;    // 深夜静默（23:00–05:59 不主动发言）
    bool dragInertia = true;   // 拖拽松手惯性滑行

    // 小游戏（扫雷）：上次选择的难度（落库 json_ext）
    int minigamePreset = 0;      // core::MinePreset 整数值（0 初级 / 1 中级 / 2 高级 / 3 自定义）
    int minigameCustomWidth = 9; // 自定义网格宽（仅当 preset == 自定义时使用）
    int minigameCustomHeight = 9;
    int minigameCustomMines = 10;

    // 小游戏（鲸鱼娘找小猫）：上次选择的难度（core::RfkDifficulty 整数值，落库 json_ext）
    int kittenDifficulty = 0; // 0 浅滩 / 1 珊瑚湾 / 2 深海遗迹

    // 向后兼容的扩展项：新增设置不建新列，直接写这里（JSON 字符串）。
    // SettingsRepo::save 会把上面三个 P6 键合并进来，并保留这里已有的其它未知键。
    QString jsonExt;
};

} // namespace whalepet::model
