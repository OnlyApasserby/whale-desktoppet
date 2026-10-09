#pragma once

// 小游戏陪玩：**中立状态快照契约**（GameSnapshot）。
//
// 目的（EX4）：让「新增一个小游戏」= 小游戏插件自己把私有状态折算成本契约，
// 陪玩侧只按本契约**通用聚合**，从而**不改动陪玩代码**即可接入。
//
// 中立性：本结构只描述「任何小游戏都能表达」的语义（进行态 / 进度 / 计数 / 危险），
// 不含扫雷 / 找小猫 / 象棋各自的概念（雷数、场景、着法一律不外泄）。
//
// 零 Qt 依赖（仅 C++17 标准库），可脱 UI 单测。
//
// 职责边界：
//   * 本文件只承载「已折算好的只读快照」，不涉及任何 UI / 采集能力；
//   * 无数据一律以 available=false 表达，**绝不伪造**（与 GameSample 同口径）。

#include <cstdint>
#include <string>

namespace whalepet::core {

// 一轮中立读数（由小游戏插件自描述，见 src/minigame/MiniGameCompanionSource.h）。
struct GameSnapshot {
    bool available = false;   // 本帧是否有可用读数（false = 上层如实降级）
    std::string gameId;       // 插件稳定标识（如 "minesweeper"），供诊断 / 投影

    bool running = false;     // 对局是否进行中（true 才占用陪玩态）
    bool finished = false;    // 本局是否已结束
    bool won = false;         // 已结束且通关（配合 finished 形成「通关」边沿）

    int level = 0;            // 阶段 / 层级（语义由插件定义；增大表示进阶）
    int progressDone = 0;     // 进度分子
    int progressTotal = 0;    // 进度分母（<= 0 表示不提供）
    int score = 0;            // 计数型累计峰值（连击 / 吃子等）

    bool danger = false;      // 游戏自述的「危险 / 不利」态（语义由插件定义）

    std::int64_t nowMs = 0;   // 采样时刻（墙钟毫秒，由调用方填充）

    // 「本轮是否拿到可用读数」
    bool isEmpty() const { return !available; }
};

} // namespace whalepet::core
