#pragma once

// 小游戏：接 Token（纯逻辑，**零 Qt 依赖**，可脱 UI 单测）。
//
// 玩法（「接元宝」类下落接取玩法的换皮：参考项目与历史提交里都没有「接元宝」，
// 本插件按「收集物 + 危险物」的下落接取语义从零实现，命名按本项目的这套皮）：
//   * 收集物 **Token**（界面字「币」）：接住 +1 分并累计连击；
//   * 危险物 **白饭**（界面字「饭」）：接到**立即结束本局**（未通关）；
//   * 漏接 Token 只清零连击（不扣分）；漏接白饭无影响（躲开即安全）；
//   * 达到目标 Token 数即**通关**（用时越短越好，个人最快纪录按「游戏 + 难度」分桶）；
//   * 到时限（60 秒）仍未达成目标 → 结束（进度过半按既有规则记「及格」档）；
//   * 每接满 kTokenCatchTokensPerLevel 个 Token 提升一档节奏（生成间隔更短）。
//
// 可测性：随机源（IRandom）与帧推进（tick()）都由外部注入 / 驱动，
// 因此可确定性单测（等价于真随机下的行为，不需要真实等待）。
//
// 结算折算：tokenCatchGameResult() → 通用的 core::MiniGameResult，
// 宿主与结算服务只认通用契约，不认识「币 / 饭 / 接取区」这些玩法细节。

#include "core/IRandom.h"
#include "core/MiniGameTypes.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 网格与节奏常量（界面与纯逻辑共用；界面尺寸按 docs/mapinit.md 由此显式计算）
// ---------------------------------------------------------------------------

inline constexpr int kTokenCatchCols = 10;        // 列数
inline constexpr int kTokenCatchRows = 14;        // 行数（最后一行是接取区）
inline constexpr int kTokenCatchTickMs = 120;     // 一帧时长（毫秒）
inline constexpr int kTokenCatchCatcherWidth = 2; // 接取区宽度（格）
inline constexpr int kTokenCatchTokensPerLevel = 5;
inline constexpr int kTokenCatchMaxLevel = 5;
inline constexpr int kTokenCatchMinSpawnTicks = 4; // 提速后的最小生成间隔
inline constexpr int kTokenCatchDangerRows = 4;    // 白饭进入最后 N 行 → danger

// 「接到白饭」结束时的立绘：鲸鱼娘开饭。
// key 对应 core::kPoses 中的 "daily-picnic"（资源 dsh-whale-state-daily-picnic.webp）。
// 该张**刻意不在** PoseLibrary 的 core / warm 档内（core 14 + warm 25 = 39 < 容量 40，
// 加进去会挤掉常驻预算），走按需加载 —— 局末一次性切换，延迟不可感知。
inline constexpr const char *kTokenCatchRicePose = "daily-picnic";

// 台词场景 key（assets/lines/tokencatch.txt；测试校验每个 key 都有候选，避免静默降级）
inline constexpr const char *kTokenCatchSceneStart = "tokencatch.start";
inline constexpr const char *kTokenCatchSceneChain = "tokencatch.chain";
inline constexpr const char *kTokenCatchSceneLevelUp = "tokencatch.levelup";
inline constexpr const char *kTokenCatchSceneRice = "tokencatch.rice";
inline constexpr const char *kTokenCatchSceneWin = "tokencatch.win";
inline constexpr const char *kTokenCatchSceneTimeUp = "tokencatch.timeup";

// ---------------------------------------------------------------------------
// 难度预设
// ---------------------------------------------------------------------------

enum class TokenCatchPreset { Easy, Normal, Hard };

struct TokenCatchPresetDef {
    TokenCatchPreset preset;
    const char *id;    // 稳定标识（结算契约 / 个人最快分桶用）
    const char *name;  // 显示名
    int spawnTicks;    // 基础生成间隔（帧）
    int fallRows;      // 每帧下落行数
    double riceChance; // 每个下落物是白饭的概率
    int targetTokens;  // 通关所需 Token 数
    int durationTicks; // 一局时限（帧）＝ 60 秒
};

inline constexpr TokenCatchPresetDef kTokenCatchPresets[] = {
    // 初级：落得慢、生成稀、目标低
    {TokenCatchPreset::Easy, "easy", "初级", 14, 1, 0.20, 12, 500},
    // 中级：节奏与目标各加一档
    {TokenCatchPreset::Normal, "normal", "中级", 10, 1, 0.25, 20, 500},
    // 高级：下落翻倍、生成更密、白饭更多、目标最高
    {TokenCatchPreset::Hard, "hard", "高级", 7, 2, 0.30, 30, 500},
};
inline constexpr int kTokenCatchPresetCount =
    static_cast<int>(sizeof(kTokenCatchPresets) / sizeof(kTokenCatchPresets[0]));

inline const TokenCatchPresetDef *tokenCatchPresetDef(TokenCatchPreset preset)
{
    for (const TokenCatchPresetDef &p : kTokenCatchPresets) {
        if (p.preset == preset) {
            return &p;
        }
    }
    return nullptr;
}

inline int tokenCatchPresetIndexOf(TokenCatchPreset preset)
{
    for (int i = 0; i < kTokenCatchPresetCount; ++i) {
        if (kTokenCatchPresets[i].preset == preset) {
            return i;
        }
    }
    return 0;
}

inline TokenCatchPreset tokenCatchPresetAt(int index)
{
    if (index < 0 || index >= kTokenCatchPresetCount) {
        return TokenCatchPreset::Normal;
    }
    return kTokenCatchPresets[index].preset;
}

inline const char *tokenCatchPresetId(TokenCatchPreset preset)
{
    const TokenCatchPresetDef *def = tokenCatchPresetDef(preset);
    return def != nullptr ? def->id : "normal";
}

inline const char *tokenCatchPresetName(TokenCatchPreset preset)
{
    const TokenCatchPresetDef *def = tokenCatchPresetDef(preset);
    return def != nullptr ? def->name : "中级";
}

// 难度展示文案，如「初级 · 目标 12 · 60 秒」
inline std::string tokenCatchPresetLabel(TokenCatchPreset preset)
{
    const TokenCatchPresetDef *def = tokenCatchPresetDef(preset);
    if (def == nullptr) {
        return std::string();
    }
    std::string label = def->name;
    label += " · 目标 ";
    label += std::to_string(def->targetTokens);
    label += " · ";
    label += std::to_string(def->durationTicks * kTokenCatchTickMs / 1000);
    label += " 秒";
    return label;
}

// ---------------------------------------------------------------------------
// 对局状态
// ---------------------------------------------------------------------------

enum class FallingKind { Token, Rice };

struct FallingItem {
    int column = 0;
    int row = 0;
    FallingKind kind = FallingKind::Token;
};

enum class TokenCatchStatus { Ready, Playing, Ended };

enum class TokenCatchEnd { None, TargetReached, RiceCaught, TimeUp };

// 一帧结果（界面据此播报 / 刷新，不必轮询私有状态）
struct TokenCatchTick {
    bool moved = false;         // 本帧是否推进（Ready / Ended 时为 false）
    int spawned = 0;            // 本帧新生成的下落物数
    int caughtTokens = 0;       // 本帧接住的 Token 数
    bool caughtRice = false;    // 本帧接住了白饭（整局立即结束）
    int missedTokens = 0;       // 本帧漏接的 Token 数
    bool levelUp = false;       // 本帧节奏档提升
    bool ended = false;         // 本帧结束
    bool won = false;           // 结束且达成目标
    TokenCatchEnd end = TokenCatchEnd::None;
};

// 一局结算数据（成就 / 奖励 / 播报用）
struct TokenCatchSummary {
    TokenCatchEnd end = TokenCatchEnd::None;
    bool won = false;
    bool perfect = false; // 通关且**全程未漏接** Token
    int tokensCaught = 0;
    int tokensMissed = 0;
    int maxChain = 0;
    int targetTokens = 0;
    int level = 0;
};

class TokenCatch {
public:
    TokenCatch() = default;

    void setRandom(IRandom *rng) { m_rng = rng; }

    // 开新局（Ready：等首次有效输入才真正开始，与其它小游戏一致）
    void newGame(TokenCatchPreset preset);
    // 首次有效输入：Ready → Playing（幂等；其它状态返回 false）
    bool start();
    // 推进一帧（仅 Playing 有效，其余状态返回空结果）
    TokenCatchTick tick();

    // 接取区移动（夹取到界内）；返回是否发生位移
    bool moveCatcher(int delta);
    // 点击某列：接取区中心移到该列（同样夹取到界内）
    bool moveCatcherTo(int column);
    // 接取区是否覆盖某列
    bool catcherCovers(int column) const
    {
        return column >= m_catcherColumn && column < m_catcherColumn + kTokenCatchCatcherWidth;
    }

    TokenCatchPreset preset() const { return m_preset; }
    const TokenCatchPresetDef &presetDef() const;
    TokenCatchStatus status() const { return m_status; }
    bool started() const { return m_status == TokenCatchStatus::Playing; }
    bool ended() const { return m_status == TokenCatchStatus::Ended; }
    bool won() const
    {
        return m_status == TokenCatchStatus::Ended && m_end == TokenCatchEnd::TargetReached;
    }
    TokenCatchEnd endReason() const { return m_end; }

    int catcherColumn() const { return m_catcherColumn; }
    int catcherWidth() const { return kTokenCatchCatcherWidth; }
    const std::vector<FallingItem> &items() const { return m_items; }
    bool itemAt(int column, int row, FallingKind *kindOut = nullptr) const;

    int tokensCaught() const { return m_tokensCaught; }
    int tokensMissed() const { return m_tokensMissed; }
    int chain() const { return m_chain; }
    int maxChain() const { return m_maxChain; }
    int targetTokens() const { return presetDef().targetTokens; }
    int level() const { return levelOf(m_tokensCaught); }
    int spawnIntervalTicks() const;
    int ticks() const { return m_ticks; }
    std::int64_t elapsedMs() const
    {
        return static_cast<std::int64_t>(m_ticks) * kTokenCatchTickMs;
    }
    std::int64_t remainingMs() const;
    // 危险态（陪玩自描述用）：存在白饭已进入最后 kTokenCatchDangerRows 行
    bool danger() const;

    TokenCatchSummary summary() const;

private:
    void finish(TokenCatchEnd end);
    bool spawnItem();
    bool columnBlockedNearTop(int column) const;
    int levelOf(int tokensCaught) const;

    IRandom *m_rng = nullptr;
    TokenCatchPreset m_preset = TokenCatchPreset::Normal;
    TokenCatchStatus m_status = TokenCatchStatus::Ready;
    TokenCatchEnd m_end = TokenCatchEnd::None;

    std::vector<FallingItem> m_items;
    int m_catcherColumn = (kTokenCatchCols - kTokenCatchCatcherWidth) / 2;
    int m_spawnCooldown = 10;
    int m_ticks = 0;
    int m_tokensCaught = 0;
    int m_tokensMissed = 0;
    int m_chain = 0;
    int m_maxChain = 0;
};

// 折算为通用结算契约（宿主 / 结算服务只认 MiniGameResult）：
// gameId = "tokencatch"；difficultyId 取 easy / normal / hard；
// won 仅在「达成目标」时为真（故 elapsedMs 是「达成目标用时」，供个人最快纪录比较）；
// perfect = 通关且零漏接；expert = 高级难度；progress = 已接 Token / 目标 Token。
inline MiniGameResult tokenCatchGameResult(const TokenCatchSummary &summary,
                                           TokenCatchPreset preset, std::int64_t elapsedMs)
{
    MiniGameResult r;
    r.gameId = "tokencatch";
    r.difficultyId = tokenCatchPresetId(preset);
    r.difficultyLabel = tokenCatchPresetName(preset);
    r.won = summary.won;
    r.perfect = summary.perfect;
    r.expert = (preset == TokenCatchPreset::Hard);
    r.maxChain = summary.maxChain;
    r.progressDone = summary.tokensCaught;
    r.progressTotal = summary.targetTokens;
    r.elapsedMs = elapsedMs;
    return r;
}

} // namespace whalepet::core
