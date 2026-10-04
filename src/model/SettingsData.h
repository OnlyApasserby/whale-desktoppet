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

    // 小游戏（国际象棋）：引擎路径（空 = 回退默认目录）、引擎棋力档位、玩家执白与否
    QString chessEnginePath;      // UCI 引擎可执行文件路径（用户自行准备，落库 json_ext）
    int chessDifficulty = 0;      // core::ChessLevel 整数值（0 入门 / 1 普通 / 2 困难）
    bool chessHumanIsWhite = true; // 玩家是否执白（先手）

    // ---- P7 新增设置项：同样落 json_ext（JSON），不新建列 ----
    // 见 docs/SETTINGS.md §2、docs/CONTEXT-API.md §5。
    bool workAwareEnabled = false;  // 工作状态感知（默认**关**：隐私优先，见 README §5.1）
    bool contextApiEnabled = false; // 本地 Context API 总开关（默认**关**：关闭时不监听任何端口）
    int contextApiPort = 0;         // 监听端口；0 = 由系统分配（仍只绑定 127.0.0.1）
    QString contextApiToken;        // 访问令牌；**HTTP 通道强制非空**（空 = 只启用命名管道，
                                   // 不监听 HTTP 端口；见 docs/CONTEXT-API.md §5.1）

    // P7.5：ACP / IDE 显式信号（默认**关**：隐私优先）
    //   acpSignalPath 为空 = 数据目录下的 acp-signals.jsonl（见 docs/CONTEXT-API.md §6）
    bool acpEnabled = false;
    QString acpSignalPath;

    // P7.6：ACP（Agent Client Protocol）客户端——由 DeepSeek Harness 提供实时 agent 状态
    //   acpDshPath 为空 = **不启动** ACP 子进程（仅保留上面的文件信号源）
    QString acpDshPath;   // dsh 入口（如 <npm-global>/@deepseek-ai/dsh/lib/bin.js）
    QString acpProfile;   // dsh profile 名（空 = "acp"，其 ACP 走 stdio）
    QString acpWorkspace; // 会话工作目录（空 = 数据目录）

    // ---- EX1.4 新增设置项：同样落 json_ext（JSON），不新建列 ----
    // 游戏陪玩：默认**关**（隐私优先）。关闭时不创建适配器、不打开任何进程、不启动采样定时器。
    bool gameCompanionEnabled = false;
    QString gameProfilePath; // 游戏档案 JSON 路径；空 = 数据目录下 game-profile.json（不存在则视为未配置）

    // ---- P8 新增设置项：同样落 json_ext（JSON），不新建列 ----
    // 预设对话（docs/DIALOGUE.md）：默认**开**（低频主动提问，受「静息 + 非深夜 + 气泡空闲」门槛约束）
    bool dialogueEnabled = true;
    // 天气（彩云天气 v3）：**key 或城市为空 = 完全不联网**（隐私优先，同参考项目口径）
    QString weatherKey;      // 彩云天气 API key（个人免费 key）
    QString weatherLocation; // 城市名（如「上海」）或经纬度（如「116.23,39.93」）

    // 向后兼容的扩展项：新增设置不建新列，直接写这里（JSON 字符串）。
    // SettingsRepo::save 会把上面三个 P6 键合并进来，并保留这里已有的其它未知键。
    QString jsonExt;
};

} // namespace whalepet::model
