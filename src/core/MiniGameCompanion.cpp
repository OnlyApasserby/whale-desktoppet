#include "core/MiniGameCompanion.h"

namespace whalepet::core {

namespace {

GameCompanionSample make(GameMood mood, double confidence, std::int64_t nowMs)
{
    GameCompanionSample sample;
    sample.mood = mood;
    sample.confidence = confidence;
    sample.sinceMs = nowMs;
    return sample;
}

} // namespace

GameCompanionSample miniGameCandidate(const GameSnapshot &snapshot)
{
    // 无读数 → Unknown，不改变任何既有行为
    if (snapshot.isEmpty()) {
        return make(GameMood::Unknown, 0.0, snapshot.nowMs);
    }
    // 未开局 / 已结束 → 不占用陪玩态（通关等事件由里程碑单发一次）
    if (!snapshot.running || snapshot.finished) {
        return make(GameMood::Unknown, 0.0, snapshot.nowMs);
    }
    if (snapshot.danger) {
        return make(GameMood::Danger, 0.9, snapshot.nowMs);
    }
    return make(GameMood::Normal, 0.8, snapshot.nowMs);
}

GameCompanionSample miniGameEvaluate(const GameSnapshot &snapshot, const GameCompanionSample &prev)
{
    const GameCompanionSample cand = miniGameCandidate(snapshot);

    // 与上一轮同态：保持 sinceMs（沿用驻留起点），只刷新置信度
    if (cand.mood == prev.mood) {
        GameCompanionSample kept = prev;
        kept.confidence = cand.confidence;
        if (kept.sinceMs <= 0) {
            kept.sinceMs = snapshot.nowMs;
        }
        return kept;
    }

    // 无数据：立即生效（不等驻留、不看置信度），尽快回到既有行为
    if (cand.mood == GameMood::Unknown) {
        return cand;
    }

    // 置信度不足：保持现状（避免抖动）
    if (cand.confidence < kGameCompanionMinConfidence) {
        return prev;
    }

    // 滞回：现状尚未驻留满最短时长则保持（prev 为 Unknown 时不等待）
    if (prev.mood != GameMood::Unknown && prev.sinceMs > 0
        && (snapshot.nowMs - prev.sinceMs) < kGameCompanionMinDwellMs) {
        return prev;
    }

    return cand;
}

GameMilestoneSet miniGameMilestones(const GameSnapshot &cur, const GameSnapshot &prev)
{
    GameMilestoneSet set;
    // 里程碑是「两轮之间的变化」：任一轮无数据都不产生事件（不伪造）
    if (!cur.available || !prev.available) {
        return set;
    }

    set.levelUp = cur.level > prev.level;
    set.danger = cur.danger && !prev.danger;
    set.recovered = prev.danger && !cur.danger;
    set.clear = cur.finished && cur.won && !(prev.finished && prev.won);
    // boss：小游戏无 BOSS 语义，保持 false
    return set;
}

GameSample gameSampleFromSnapshot(const GameSnapshot &snapshot)
{
    GameSample sample;
    sample.available = snapshot.available;
    sample.level = snapshot.level;
    sample.specialScene = 0;
    sample.nowMs = snapshot.nowMs;
    sample.mapName = snapshot.gameId;
    return sample;
}

} // namespace whalepet::core
