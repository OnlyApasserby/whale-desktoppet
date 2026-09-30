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
    bool keywordAware = false;    // 默认关（CHAT.md）
    bool minigameEnabled = false; // 默认关（预留）

    // 向后兼容的扩展项：新增设置不建新列，直接写这里（JSON 字符串）
    QString jsonExt;
};

} // namespace whalepet::model
