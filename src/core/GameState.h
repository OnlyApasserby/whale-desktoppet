#pragma once

// 游戏陪玩状态（game companion state）：**零 Qt 依赖**（仅 C++17 标准库），可脱 UI 单测。
//
// 归属：docs/ROADMAP-ex1.md §2.5 / §2.6.2.1、§六 接口契约（EX1.0 交付物 2）。
//
// 分层与 WorkState（src/core/WorkState.h）保持同构：
//   * 本文件前半段：纯数据类型 + 判定常量 + 无副作用查表函数；
//   * 本文件后半段：`GameCompanionRules`（参数可注入、时钟由采样携带，完全可测）。
//
// 【红线】本文件不涉及任何进程访问能力：只承载「已读出的只读快照」与纯判定逻辑。
// 读取失败一律以 `GameSample::available=false` 表达，**绝不伪造数据**（§4.3）。

#include <cstdint>
#include <string>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 判定常量（禁止在别处再写字面量）
// ---------------------------------------------------------------------------
inline constexpr std::int64_t kGameSampleIntervalMs = 200;   // 高频档采样周期（§2.9）
inline constexpr std::int64_t kGameCompanionMinDwellMs = 5000; // 滞回：状态最短驻留时长
inline constexpr double kGameCompanionMinConfidence = 0.6;     // 低于此置信度不改变现状
inline constexpr double kGameDangerHpRatio = 0.2;              // hp/hpMax ≤ 该值 → 危险
inline constexpr double kGameRecoverHpRatio = 0.9;             // hp/hpMax ≥ 该值 → 脱离危险
inline constexpr double kGameBossHpMaxRatio = 1.5;             // hpMax 环比增幅 → 判 BOSS 出现
inline constexpr int kGameSpecialSceneEnterFrames = 3;         // 特殊场景：连续 N 帧成立才置位
inline constexpr int kGameSpecialSceneExitFrames = 3;          // 特殊场景：连续 M 帧不成立才复位

// ---------------------------------------------------------------------------
// 特殊场景（对齐 §2.6.2.1：0 无 / 1 图片 / 2 专用场景 / 3 影片 / 4 对话演出）
// ---------------------------------------------------------------------------
enum class GameSpecialScene { None = 0, Picture = 1, Scene = 2, Video = 3, Dialogue = 4 };

const char *gameSpecialSceneId(GameSpecialScene scene);
GameSpecialScene gameSpecialSceneFromId(const std::string &id);

// 非 0 → 桌宠进入**静默陪伴**（不弹气泡、不主动播报，仅保留立绘与既有点击交互）
inline bool gameSpecialSceneIsSilent(int specialScene) { return specialScene != 0; }

// ---------------------------------------------------------------------------
// 持续态（滞回）：Unknown 无数据 / Normal 正常 / Danger 危险
// ---------------------------------------------------------------------------
enum class GameMood { Unknown = 0, Normal = 1, Danger = 2 };

const char *gameMoodId(GameMood mood);
GameMood gameMoodFromId(const std::string &id); // 未识别 → Unknown

// 持续态对应的立绘 pose：复用既有 93 张立绘，不新增美术资源（§2.6「不新增依赖」）。
// Unknown 返回 nullptr（调用方保持当前立绘不变）。
const char *gameMoodPose(GameMood mood);

// 持续态对应的台词场景 key（`game.*`）；Unknown 返回 nullptr（不播报）。
const char *gameMoodScene(GameMood mood);

// ---------------------------------------------------------------------------
// 采样数据
// ---------------------------------------------------------------------------

// 一轮原始读数（由 gamestate 层填充；不可用时 available=false）。
struct GameSample {
    bool available = false;         // 目标进程 / 档案是否可用（不可用不伪造）
    double hp = 0.0;                // 当前血量
    double hpMax = 0.0;             // 血量上限（≤0 视为「无法计算比例」）
    long long gold = 0;             // 金币
    int level = 0;                  // 等级
    double posX = 0.0;              // 坐标 X
    double posY = 0.0;              // 坐标 Y
    std::string mapName;            // 地图 / 场景名（可选）
    int specialScene = 0;           // GameSpecialScene 的整数值（0 无）
    std::int64_t nowMs = 0;         // 采样时刻（墙钟毫秒）

    GameSpecialScene scene() const { return static_cast<GameSpecialScene>(specialScene); }

    // 「本轮是否拿到可用读数」
    bool isEmpty() const { return !available; }
};

// 游戏陪玩判定结果（与 WorkStateSample 同构）
struct GameCompanionSample {
    GameMood mood = GameMood::Unknown;
    double confidence = 0.0;   // 0..1
    std::int64_t sinceMs = 0;  // 进入该状态的时刻（滞回用）
};

// 边沿里程碑（相对上一轮快照比较；判据见 §6.4，全部为高置信度事件）
struct GameMilestoneSet {
    bool levelUp = false;    // 升级
    bool boss = false;       // BOSS 出现（hpMax 显著增大且处于危险）
    bool clear = false;      // 通关式恢复（脱离危险且升级）
    bool danger = false;     // 首次进入危险
    bool recovered = false;  // 从危险恢复

    bool any() const { return levelUp || boss || clear || danger || recovered; }
};

// 里程碑的台词场景 key（按 clear > boss > levelUp > danger > recovered 优先级取首个命中）
const char *gameMilestoneScene(const GameMilestoneSet &set);
// 里程碑的立绘 pose key（优先级同上）
const char *gameMilestonePose(const GameMilestoneSet &set);

// ---------------------------------------------------------------------------
// 判定规则（刻意照搬 core::WorkStateRules 的写法，便于逐条单测）
// ---------------------------------------------------------------------------

// 可注入参数（默认值即本文件顶部常量；测试可整套替换以验证边界）
struct GameCompanionParams {
    std::int64_t minDwellMs = kGameCompanionMinDwellMs;
    double minConfidence = kGameCompanionMinConfidence;
    double dangerHpRatio = kGameDangerHpRatio;
    double recoverHpRatio = kGameRecoverHpRatio;
    double bossHpMaxRatio = kGameBossHpMaxRatio;
};

class GameCompanionRules {
public:
    explicit GameCompanionRules(GameCompanionParams params = GameCompanionParams());

    // 归一化：把一轮采样归纳为「候选持续态 + 置信度」（不含滞回与阈值过滤，便于逐条单测）
    GameCompanionSample candidate(const GameSample &sample) const;

    // 稳定判定：候选 + 上一次结果 → 实际生效的持续态（含置信度阈值与最短驻留滞回）。
    //   * Unknown 立即生效（失联时必须尽快退回既有行为，不等驻留）；
    //   * 置信度 < minConfidence → 保持 prev（不抖动）；
    //   * 与 prev 不同且 prev 驻留未满 minDwellMs → 保持 prev（滞回）；
    //   * prev 为 Unknown 时首次判定无需等待。
    GameCompanionSample evaluate(const GameSample &sample, const GameCompanionSample &prev) const;

    // 边沿里程碑：仅比较相邻两轮（启发式判据，见 §6.4）；不成立则不产生。
    GameMilestoneSet milestones(const GameSample &cur, const GameSample &prev) const;

    const GameCompanionParams &params() const { return m_params; }

private:
    GameCompanionParams m_params;
};

} // namespace whalepet::core
