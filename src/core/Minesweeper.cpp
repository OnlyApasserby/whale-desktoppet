#include "core/Minesweeper.h"

#include <algorithm>
#include <cstddef>

namespace whalepet::core {

namespace {

// 8 邻域方向（行、列偏移）
constexpr int kNeighborDirs[8][2] = {
    {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1},
};
constexpr int kNeighborMax = 8;

} // namespace

bool Minesweeper::inside(int x, int y) const
{
    return x >= 0 && x < m_config.width && y >= 0 && y < m_config.height;
}

void Minesweeper::newGame(const MineConfig &config)
{
    m_config = config;
    if (!mineConfigInRange(m_config)) {
        m_config = mineConfigOfPreset(MinePreset::Beginner);
    }
    const int maxMines = mineMaxMines(m_config);
    if (m_config.mines < kMineMinMines) {
        m_config.mines = kMineMinMines;
    }
    if (m_config.mines > maxMines) {
        m_config.mines = maxMines;
    }

    m_cells.assign(static_cast<std::size_t>(m_config.width * m_config.height), MineCell{});
    m_status = MineStatus::Ready;
    m_placed = false;
    m_flagCount = 0;
    m_revealedSafe = 0;
    m_chain = 0;
    m_maxChain = 0;
    m_clicks = 0;
}

void Minesweeper::neighborIndices(int index, int *out, int *count) const
{
    const int x = index % m_config.width;
    const int y = index / m_config.width;
    int n = 0;
    for (int d = 0; d < kNeighborMax; ++d) {
        const int nx = x + kNeighborDirs[d][0];
        const int ny = y + kNeighborDirs[d][1];
        if (inside(nx, ny)) {
            out[n] = ny * m_config.width + nx;
            ++n;
        }
    }
    if (count != nullptr) {
        *count = n;
    }
}

int Minesweeper::neighborMines(int index) const
{
    int neighbors[kNeighborMax] = {0};
    int n = 0;
    neighborIndices(index, neighbors, &n);
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (m_cells[static_cast<std::size_t>(neighbors[i])].mine) {
            ++count;
        }
    }
    return count;
}

void Minesweeper::placeMines(int safeIndex)
{
    const int cells = cellCount();
    std::vector<int> pool;
    pool.reserve(static_cast<std::size_t>(cells));
    for (int i = 0; i < cells; ++i) {
        if (i != safeIndex) {
            pool.push_back(i);
        }
    }

    const int mines = m_config.mines;
    const int total = static_cast<int>(pool.size());
    // 部分 Fisher–Yates：前 mines 个位置即雷格
    for (int k = 0; k < mines && k < total; ++k) {
        const int remaining = total - k;
        int j = k;
        if (m_rng != nullptr && remaining > 1) {
            j = k + m_rng->nextInt(remaining);
        }
        std::swap(pool[static_cast<std::size_t>(k)], pool[static_cast<std::size_t>(j)]);
        m_cells[static_cast<std::size_t>(pool[static_cast<std::size_t>(k)])].mine = true;
    }

    computeAdjacent();
    m_placed = true;
}

void Minesweeper::computeAdjacent()
{
    const int cells = cellCount();
    for (int i = 0; i < cells; ++i) {
        m_cells[static_cast<std::size_t>(i)].adjacent = neighborMines(i);
    }
}

void Minesweeper::floodReveal(int start, int *revealedOut)
{
    int revealed = 0;
    std::vector<int> stack;
    stack.push_back(start);
    while (!stack.empty()) {
        const int index = stack.back();
        stack.pop_back();
        MineCell &c = m_cells[static_cast<std::size_t>(index)];
        if (c.revealed || c.flagged || c.mine) {
            continue;
        }
        c.revealed = true;
        ++revealed;
        if (c.adjacent != 0) {
            continue;
        }
        int neighbors[kNeighborMax] = {0};
        int n = 0;
        neighborIndices(index, neighbors, &n);
        for (int i = 0; i < n; ++i) {
            const MineCell &nb = m_cells[static_cast<std::size_t>(neighbors[i])];
            if (!nb.revealed && !nb.flagged && !nb.mine) {
                stack.push_back(neighbors[i]);
            }
        }
    }
    if (revealedOut != nullptr) {
        *revealedOut = revealed;
    }
}

bool Minesweeper::isWin() const
{
    return m_revealedSafe >= cellCount() - m_config.mines;
}

void Minesweeper::revealAllMines()
{
    for (MineCell &c : m_cells) {
        if (c.mine) {
            c.revealed = true;
        }
    }
}

MineMove Minesweeper::reveal(int index)
{
    MineMove move;
    if (index < 0 || index >= cellCount()) {
        return move;
    }
    if (m_status == MineStatus::Won || m_status == MineStatus::Lost) {
        return move;
    }

    MineCell &c = m_cells[static_cast<std::size_t>(index)];
    if (c.revealed || c.flagged) {
        return move;
    }

    if (!m_placed) {
        placeMines(index); // 首点安全：布雷时排除该格
        m_status = MineStatus::Playing;
    }
    ++m_clicks;

    if (c.mine) {
        c.revealed = true;
        m_status = MineStatus::Lost;
        m_chain = 0;
        revealAllMines();
        move.changed = true;
        move.exploded = true;
        move.chainPeak = m_maxChain;
        return move;
    }

    int revealed = 0;
    floodReveal(index, &revealed);
    m_revealedSafe += revealed;
    m_chain += revealed;
    if (m_chain > m_maxChain) {
        m_maxChain = m_chain;
    }

    move.changed = revealed > 0;
    move.revealed = revealed;
    move.chain = m_chain;
    move.chainPeak = m_maxChain;

    if (isWin()) {
        m_status = MineStatus::Won;
        move.won = true;
    }
    return move;
}

MineMove Minesweeper::toggleFlag(int index)
{
    MineMove move;
    if (index < 0 || index >= cellCount()) {
        return move;
    }
    if (m_status == MineStatus::Won || m_status == MineStatus::Lost) {
        return move;
    }

    MineCell &c = m_cells[static_cast<std::size_t>(index)];
    if (c.revealed) {
        return move;
    }

    c.flagged = !c.flagged;
    m_flagCount += c.flagged ? 1 : -1;
    move.changed = true;
    return move;
}

MineSummary Minesweeper::summary() const
{
    MineSummary s;
    s.won = (m_status == MineStatus::Won);
    s.maxChain = m_maxChain;
    s.mineCount = m_config.mines;
    s.revealedSafe = m_revealedSafe;
    s.totalSafe = cellCount() - m_config.mines;
    // 胜利时所有非雷格均已翻开，插旗格必然是雷 → 插旗数 == 雷数即「全对插旗」
    s.perfect = s.won && m_flagCount == m_config.mines;
    return s;
}

} // namespace whalepet::core
