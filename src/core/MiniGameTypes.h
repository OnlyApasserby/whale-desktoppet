#pragma once

// 小游戏通用结算契约（**零 Qt 依赖**，可脱 UI 单测）。
//
// 背景：小游戏改为「插件化接入」（见 src/minigame/MiniGamePlugin.h）。为让宿主
// （PetWindow）、结算服务（MiniGameService）与成就系统**不依赖任何具体玩法**，
// 所有插件都把自己的对局结果折算成本文件的 MiniGameResult 上报。
//
// 设计要点：
//   * 契约只描述「宿主需要知道的东西」：是否获胜、是否完美、连击峰值、
//     进度（用于及格档判定）、用时（用于个人最快纪录）、难度标识；
//   * 玩法私有语义（棋盘、雷数、翻格……）一律不出现在这里；
//   * 档位判定 gameGrade 与参考项目 game-win / game-draw / game-lose 三档一一对应。

#include <cstdint>
#include <string>

namespace whalepet::core {

// 结算档位（对应养成奖励表 game-win / game-draw / game-lose）
enum class GameGrade {
    Win,  // 通关
    Draw, // 未通关但进度过半（对照参考项目的「及格」档）
    Lose, // 未通关且进度不足
};

// 一局结算结果（宿主 / 结算服务 / 成就系统的唯一输入）
struct MiniGameResult {
    std::string gameId;          // 插件稳定标识，如 "minesweeper"
    std::string difficultyId;    // 难度稳定标识，如 "beginner"；无难度概念时留空
    std::string difficultyLabel; // 难度显示文案（可选，仅用于展示）

    bool won = false;      // 是否通关
    bool perfect = false;  // 完美通关（玩法自定义语义，如「全对插旗」）
    bool expert = false;   // 是否属于高难档（成就判定用）

    int maxChain = 0;      // 连击 / 连翻峰值（无此概念时为 0）
    int progressDone = 0;  // 进度分子（如已翻开的非雷格数）
    int progressTotal = 0; // 进度分母（如非雷格总数）；<= 0 表示不提供进度

    std::int64_t elapsedMs = 0; // 本局用时（毫秒）；无计时概念时为 0
};

// 档位判定：通关 → Win；未通关但进度过半 → Draw；否则 Lose。
// 纯函数，零依赖，可单测。
inline GameGrade gameGrade(const MiniGameResult &r)
{
    if (r.won) {
        return GameGrade::Win;
    }
    if (r.progressTotal > 0 && r.progressDone * 2 >= r.progressTotal) {
        return GameGrade::Draw;
    }
    return GameGrade::Lose;
}

} // namespace whalepet::core
