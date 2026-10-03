#include "core/GameState.h"

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

// 是否处于「危险」：需要可用的血量上限才能计算比例
bool isDanger(const GameSample &sample, double dangerHpRatio)
{
    if (!sample.available || !(sample.hpMax > 0.0)) {
        return false;
    }
    return (sample.hp / sample.hpMax) <= dangerHpRatio;
}

} // namespace

const char *gameSpecialSceneId(GameSpecialScene scene)
{
    switch (scene) {
    case GameSpecialScene::None:
        return "none";
    case GameSpecialScene::Picture:
        return "picture";
    case GameSpecialScene::Scene:
        return "scene";
    case GameSpecialScene::Video:
        return "video";
    case GameSpecialScene::Dialogue:
        return "dialogue";
    }
    return "none";
}

GameSpecialScene gameSpecialSceneFromId(const std::string &id)
{
    for (int i = 0; i <= static_cast<int>(GameSpecialScene::Dialogue); ++i) {
        const GameSpecialScene scene = static_cast<GameSpecialScene>(i);
        if (id == gameSpecialSceneId(scene)) {
            return scene;
        }
    }
    return GameSpecialScene::None;
}

const char *gameMoodId(GameMood mood)
{
    switch (mood) {
    case GameMood::Unknown:
        return "unknown";
    case GameMood::Normal:
        return "normal";
    case GameMood::Danger:
        return "danger";
    }
    return "unknown";
}

GameMood gameMoodFromId(const std::string &id)
{
    for (int i = 0; i <= static_cast<int>(GameMood::Danger); ++i) {
        const GameMood mood = static_cast<GameMood>(i);
        if (id == gameMoodId(mood)) {
            return mood;
        }
    }
    return GameMood::Unknown;
}

const char *gameMoodPose(GameMood mood)
{
    // 全部复用既有 93 张立绘（assets/poses/*.webp，见 core/PoseNames.h）
    switch (mood) {
    case GameMood::Unknown:
        return nullptr; // 无数据：不改变立绘
    case GameMood::Normal:
        return "game-happy";
    case GameMood::Danger:
        return "meme-shock";
    }
    return nullptr;
}

const char *gameMoodScene(GameMood mood)
{
    switch (mood) {
    case GameMood::Unknown:
        return nullptr; // 不播报
    case GameMood::Normal:
        return "game.normal";
    case GameMood::Danger:
        return "game.danger";
    }
    return nullptr;
}

const char *gameMilestoneScene(const GameMilestoneSet &set)
{
    // 优先级：通关 > BOSS > 升级 > 濒死 > 恢复（同一轮命中多个时只播报最重要的一个）
    if (set.clear) {
        return "game.clear";
    }
    if (set.boss) {
        return "game.boss";
    }
    if (set.levelUp) {
        return "game.levelup";
    }
    if (set.danger) {
        return "game.danger";
    }
    if (set.recovered) {
        return "game.normal";
    }
    return nullptr;
}

const char *gameMilestonePose(const GameMilestoneSet &set)
{
    if (set.clear) {
        return "game-win";
    }
    if (set.boss) {
        return "work-boss";
    }
    if (set.levelUp) {
        return "levelup";
    }
    if (set.danger) {
        return "meme-shock";
    }
    if (set.recovered) {
        return "game-happy";
    }
    return nullptr;
}

GameCompanionRules::GameCompanionRules(GameCompanionParams params)
    : m_params(params)
{
}

GameCompanionSample GameCompanionRules::candidate(const GameSample &sample) const
{
    // 无读数（未开启 / 档案失效 / 桥接断开）→ Unknown，不改变任何既有行为
    if (sample.isEmpty()) {
        return make(GameMood::Unknown, 0.0, sample.nowMs);
    }

    // 拿不到血量上限 → 无法计算危险比例：给低置信度（通常不足以改变现状）
    if (!(sample.hpMax > 0.0)) {
        return make(GameMood::Normal, 0.5, sample.nowMs);
    }

    const double ratio = sample.hp / sample.hpMax;
    if (ratio <= m_params.dangerHpRatio) {
        return make(GameMood::Danger, 0.95, sample.nowMs);
    }
    return make(GameMood::Normal, 0.9, sample.nowMs);
}

GameCompanionSample GameCompanionRules::evaluate(const GameSample &sample,
                                                 const GameCompanionSample &prev) const
{
    const GameCompanionSample cand = candidate(sample);

    if (cand.mood == prev.mood) {
        GameCompanionSample kept = prev;
        kept.confidence = cand.confidence;
        if (kept.sinceMs <= 0) {
            kept.sinceMs = sample.nowMs;
        }
        return kept;
    }

    // 无数据：立即生效（不等驻留、不看置信度），尽快回到既有行为
    if (cand.mood == GameMood::Unknown) {
        return make(GameMood::Unknown, 0.0, sample.nowMs);
    }

    // 置信度不足：保持现状（避免抖动）
    if (cand.confidence < m_params.minConfidence) {
        return prev;
    }

    // 滞回：现状尚未驻留满最短时长则保持（prev 为 Unknown 时不等待）
    if (prev.mood != GameMood::Unknown && prev.sinceMs > 0
        && (sample.nowMs - prev.sinceMs) < m_params.minDwellMs) {
        return prev;
    }

    return make(cand.mood, cand.confidence, sample.nowMs);
}

GameMilestoneSet GameCompanionRules::milestones(const GameSample &cur, const GameSample &prev) const
{
    GameMilestoneSet set;
    // 里程碑是「两轮之间的变化」：任一轮无数据都不产生事件（不伪造）
    if (!cur.available || !prev.available) {
        return set;
    }

    const bool prevDanger = isDanger(prev, m_params.dangerHpRatio);
    const bool curDanger = isDanger(cur, m_params.dangerHpRatio);
    const double ratio = (cur.hpMax > 0.0) ? (cur.hp / cur.hpMax) : 0.0;

    set.levelUp = cur.level > prev.level;
    set.danger = curDanger && !prevDanger;
    set.recovered = prevDanger && !curDanger && ratio >= m_params.recoverHpRatio;
    set.boss = curDanger && prev.hpMax > 0.0
               && cur.hpMax >= prev.hpMax * m_params.bossHpMaxRatio;
    set.clear = prevDanger && !curDanger && set.levelUp;
    return set;
}

} // namespace whalepet::core
