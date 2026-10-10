#include "core/TokenCatch.h"

namespace whalepet::core {

const TokenCatchPresetDef &TokenCatch::presetDef() const
{
    const TokenCatchPresetDef *def = tokenCatchPresetDef(m_preset);
    return def != nullptr ? *def : kTokenCatchPresets[0];
}

void TokenCatch::newGame(TokenCatchPreset preset)
{
    m_preset = preset;
    m_status = TokenCatchStatus::Ready;
    m_end = TokenCatchEnd::None;
    m_items.clear();
    m_catcherColumn = (kTokenCatchCols - kTokenCatchCatcherWidth) / 2;
    m_spawnCooldown = presetDef().spawnTicks;
    m_ticks = 0;
    m_tokensCaught = 0;
    m_tokensMissed = 0;
    m_chain = 0;
    m_maxChain = 0;
}

bool TokenCatch::start()
{
    if (m_status != TokenCatchStatus::Ready) {
        return false;
    }
    m_status = TokenCatchStatus::Playing;
    return true;
}

TokenCatchTick TokenCatch::tick()
{
    TokenCatchTick out;
    if (m_status != TokenCatchStatus::Playing) {
        return out; // 未开局 / 已结束：不接受任何推进（幂等，便于宿主持有窗口期间反复调用）
    }

    out.moved = true;
    ++m_ticks;
    const int levelBefore = level();

    // 1) 下落：整体下移 presetDef().fallRows 行；到达接取行的下落物本帧即出结果
    if (!m_items.empty()) {
        std::vector<FallingItem> kept;
        kept.reserve(m_items.size());
        for (const FallingItem &item : m_items) {
            FallingItem moved = item;
            moved.row += presetDef().fallRows;
            if (moved.row < kTokenCatchRows - 1) {
                kept.push_back(moved);
                continue;
            }
            if (catcherCovers(moved.column)) {
                if (moved.kind == FallingKind::Rice) {
                    out.caughtRice = true; // 接住白饭 → 整局立即结束（下方统一判定）
                    continue;
                }
                ++m_tokensCaught;
                ++m_chain;
                m_maxChain = std::max(m_maxChain, m_chain);
                ++out.caughtTokens;
                continue;
            }
            if (moved.kind == FallingKind::Token) {
                ++m_tokensMissed;
                m_chain = 0; // 漏接只清连击，不扣分
                ++out.missedTokens;
            }
            // 漏接白饭 = 躲开了，无任何影响
        }
        m_items.swap(kept);
    }

    // 2) 节奏档提升（每接满 kTokenCatchTokensPerLevel 个 Token 一档，生成间隔随之缩短）
    if (level() > levelBefore) {
        out.levelUp = true;
    }

    // 3) 生成（列被顶部已有下落物占用时换列重试，避免两物同格导致界面信息丢失）
    if (--m_spawnCooldown <= 0) {
        if (spawnItem()) {
            out.spawned = 1;
        }
        m_spawnCooldown = spawnIntervalTicks();
    }

    // 4) 结束判定：接白饭优先 → 达成目标 → 时限
    if (out.caughtRice) {
        finish(TokenCatchEnd::RiceCaught);
    } else if (m_tokensCaught >= targetTokens()) {
        finish(TokenCatchEnd::TargetReached);
    } else if (m_ticks >= presetDef().durationTicks) {
        finish(TokenCatchEnd::TimeUp);
    }

    out.ended = ended();
    out.end = m_end;
    out.won = won();
    return out;
}

bool TokenCatch::moveCatcher(int delta)
{
    const int maxColumn = kTokenCatchCols - kTokenCatchCatcherWidth;
    const int next = std::clamp(m_catcherColumn + delta, 0, maxColumn);
    if (next == m_catcherColumn) {
        return false;
    }
    m_catcherColumn = next;
    return true;
}

bool TokenCatch::moveCatcherTo(int column)
{
    const int maxColumn = kTokenCatchCols - kTokenCatchCatcherWidth;
    const int next = std::clamp(column - kTokenCatchCatcherWidth / 2, 0, maxColumn);
    if (next == m_catcherColumn) {
        return false;
    }
    m_catcherColumn = next;
    return true;
}

bool TokenCatch::itemAt(int column, int row, FallingKind *kindOut) const
{
    for (const FallingItem &item : m_items) {
        if (item.column == column && item.row == row) {
            if (kindOut != nullptr) {
                *kindOut = item.kind;
            }
            return true;
        }
    }
    return false;
}

int TokenCatch::spawnIntervalTicks() const
{
    return std::max(kTokenCatchMinSpawnTicks, presetDef().spawnTicks - level());
}

std::int64_t TokenCatch::remainingMs() const
{
    const int left = presetDef().durationTicks - m_ticks;
    return left > 0 ? static_cast<std::int64_t>(left) * kTokenCatchTickMs : 0;
}

bool TokenCatch::danger() const
{
    const int threshold = kTokenCatchRows - 1 - kTokenCatchDangerRows;
    for (const FallingItem &item : m_items) {
        if (item.kind == FallingKind::Rice && item.row >= threshold) {
            return true;
        }
    }
    return false;
}

TokenCatchSummary TokenCatch::summary() const
{
    TokenCatchSummary s;
    s.end = m_end;
    s.won = won();
    s.perfect = won() && m_tokensMissed == 0;
    s.tokensCaught = m_tokensCaught;
    s.tokensMissed = m_tokensMissed;
    s.maxChain = m_maxChain;
    s.targetTokens = targetTokens();
    s.level = level();
    return s;
}

void TokenCatch::finish(TokenCatchEnd end)
{
    m_status = TokenCatchStatus::Ended;
    m_end = end;
}

bool TokenCatch::spawnItem()
{
    int column = (m_rng != nullptr) ? m_rng->nextInt(kTokenCatchCols) : 0;
    for (int attempt = 0; attempt < 3 && columnBlockedNearTop(column); ++attempt) {
        column = (m_rng != nullptr) ? m_rng->nextInt(kTokenCatchCols)
                                    : (column + 1) % kTokenCatchCols;
    }
    if (columnBlockedNearTop(column)) {
        return false; // 本次不生成（下一帧再试），不硬塞到已有下落物身上
    }

    FallingKind kind = FallingKind::Token;
    if (m_rng != nullptr && m_rng->next01() < presetDef().riceChance) {
        kind = FallingKind::Rice;
    }
    m_items.push_back(FallingItem{column, 0, kind});
    return true;
}

bool TokenCatch::columnBlockedNearTop(int column) const
{
    for (const FallingItem &item : m_items) {
        if (item.column == column && item.row <= 1) {
            return true;
        }
    }
    return false;
}

int TokenCatch::levelOf(int tokensCaught) const
{
    if (tokensCaught <= 0) {
        return 0;
    }
    return std::min(kTokenCatchMaxLevel, tokensCaught / kTokenCatchTokensPerLevel);
}

} // namespace whalepet::core
