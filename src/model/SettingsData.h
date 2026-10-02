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

    // ---- P7 新增设置项：同样落 json_ext（JSON），不新建列 ----
    // 见 docs/SETTINGS.md §2、docs/CONTEXT-API.md §5。
    bool workAwareEnabled = false;  // 工作状态感知（默认**关**：隐私优先，见 README §5.1）
    bool contextApiEnabled = false; // 本地 Context API 总开关（默认**关**：关闭时不监听任何端口）
    int contextApiPort = 0;         // 监听端口；0 = 由系统分配（仍只绑定 127.0.0.1）
    QString contextApiToken;        // 访问令牌；空 = 不校验（仍仅本机可访问）

    // 向后兼容的扩展项：新增设置不建新列，直接写这里（JSON 字符串）。
    // SettingsRepo::save 会把上面三个 P6 键合并进来，并保留这里已有的其它未知键。
    QString jsonExt;
};

} // namespace whalepet::model
