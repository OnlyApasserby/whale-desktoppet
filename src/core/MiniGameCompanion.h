#pragma once

// 小游戏陪玩：**中立判定规则**（零 Qt，可脱 UI 单测）。
//
// 与 GameCompanionRules（EX1 外部游戏遗留的 RPG 语义：血量→危险）并列：
//   * GameCompanionRules 解释 GameSample（旧 RPG 采样载体），保留以复用既有回归用例；
//   * MiniGameCompanion 解释 GameSnapshot（中立契约），供小游戏陪玩使用。
// 两者共享同一套「置信度阈值 + 最短驻留滞回 + Unknown 立即生效」策略（同常量），
// 但判据完全中立：mood 只由 running / finished / danger 决定，里程碑只由
// level / danger / finished&won 的**相邻两轮边沿**决定。
//
// 下游（PetStateMachine 游戏通道 / PetController::handleGameState / 立绘与台词映射 /
// Context 投影）完全复用，因此新增小游戏不触碰这些环节。

#include "core/GameSnapshot.h"
#include "core/GameState.h"

namespace whalepet::core {

// 归一化：把一轮中立快照归纳为「候选持续态 + 置信度」（不含滞回与阈值过滤）。
//   * 无读数 / 未在对局中 / 已结束 → Unknown（不占用陪玩态）；
//   * danger → Danger；其余进行中 → Normal。
GameCompanionSample miniGameCandidate(const GameSnapshot &snapshot);

// 稳定判定：候选 + 上一次结果 → 实际生效态（策略与 GameCompanionRules::evaluate 一致）。
GameCompanionSample miniGameEvaluate(const GameSnapshot &snapshot, const GameCompanionSample &prev);

// 边沿里程碑：仅比较相邻两轮（任一轮无读数则不产生，避免「启动即播报」误报）。
//   * levelUp  ：level 增大
//   * danger   ：进入危险（danger 由 false → true）
//   * recovered：脱离危险（danger 由 true → false）
//   * clear    ：通关边沿（finished && won 由「非通关」→「通关」）
//   * boss     ：小游戏无 BOSS 语义，恒为 false
GameMilestoneSet miniGameMilestones(const GameSnapshot &cur, const GameSnapshot &prev);

// 把中立快照折算为陪玩管线的采样载体 GameSample（供信号透传 / Context 投影复用）。
// 只填中立字段与 available / nowMs；specialScene 恒为 0（小游戏无外部特殊场景）。
GameSample gameSampleFromSnapshot(const GameSnapshot &snapshot);

} // namespace whalepet::core
