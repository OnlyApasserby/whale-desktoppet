#pragma once

// 小游戏：扫雷（纯逻辑，**零 Qt 依赖**，可脱 UI 单测）。
//
// 取代 docs/MINIGAME-INTERFACE.md 原「戳泡泡」预留玩法。设计要点：
//   * 棋盘尺寸 / 雷数由 MineConfig 描述；内置 3 个预设（初级 / 中级 / 高级）+ 自定义；
//   * **首点安全**：首次翻开的格子永不布雷，布雷在首点之后延迟进行；
//   * 随机源可注入（IRandom），便于确定性单测；
//   * 只输出「语义结果」（踩雷 / 连翻 / 胜负），立绘与台词由 View 层决定
//     （对应参考项目 dsh-whale-moe.js 的 showMood / say 表现）。

#include "core/IRandom.h"

#include <string>
#include <vector>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 难度预设与自定义配置
// ---------------------------------------------------------------------------

enum class MinePreset {
    Beginner,     // 初级
    Intermediate, // 中级
    Expert,       // 高级
    Custom,       // 自定义
};

struct MinePresetDef {
    MinePreset preset;
    const char *id;   // 稳定标识（落库 / 测试用）
    const char *name; // 显示名
    int width;
    int height;
    int mines;
};

// 3 个内置预设（规格来自需求：9×9/10、16×16/40、30×16/99）
inline constexpr MinePresetDef kMinePresets[] = {
    {MinePreset::Beginner, "beginner", "初级", 9, 9, 10},
    {MinePreset::Intermediate, "intermediate", "中级", 16, 16, 40},
    {MinePreset::Expert, "expert", "高级", 30, 16, 99},
};
inline constexpr int kMinePresetCount =
    static_cast<int>(sizeof(kMinePresets) / sizeof(kMinePresets[0]));

// 自定义网格约束（同时约束预设合法性）
inline constexpr int kMineMinWidth = 5;
inline constexpr int kMineMaxWidth = 30;
inline constexpr int kMineMinHeight = 5;
inline constexpr int kMineMaxHeight = 24;
inline constexpr int kMineMinMines = 1;

struct MineConfig {
    int width = 9;
    int height = 9;
    int mines = 10;
};

inline int mineCellCount(const MineConfig &c)
{
    return c.width * c.height;
}

// 雷数上限：至少留 1 个非雷格，保证「首点安全」可成立
inline int mineMaxMines(const MineConfig &c)
{
    const int cells = mineCellCount(c);
    return cells > 1 ? cells - 1 : 1;
}

inline bool mineConfigInRange(const MineConfig &c)
{
    return c.width >= kMineMinWidth && c.width <= kMineMaxWidth && c.height >= kMineMinHeight
           && c.height <= kMineMaxHeight;
}

inline bool mineConfigValid(const MineConfig &c)
{
    return mineConfigInRange(c) && c.mines >= kMineMinMines && c.mines <= mineMaxMines(c);
}

inline const MinePresetDef *minePresetDef(MinePreset preset)
{
    for (const MinePresetDef &p : kMinePresets) {
        if (p.preset == preset) {
            return &p;
        }
    }
    return nullptr;
}

inline MineConfig mineConfigOfPreset(MinePreset preset)
{
    const MinePresetDef *p = minePresetDef(preset);
    if (p == nullptr) {
        return MineConfig{};
    }
    return MineConfig{p->width, p->height, p->mines};
}

// 与某个内置预设完全一致 → 返回该预设；否则 Custom
inline MinePreset minePresetOfConfig(const MineConfig &c)
{
    for (const MinePresetDef &p : kMinePresets) {
        if (p.width == c.width && p.height == c.height && p.mines == c.mines) {
            return p.preset;
        }
    }
    return MinePreset::Custom;
}

inline const char *minePresetName(MinePreset preset)
{
    const MinePresetDef *p = minePresetDef(preset);
    return p != nullptr ? p->name : "自定义";
}

// 难度展示文案，如「初级 · 9×9 · 10 雷」/「自定义 · 12×10 · 20 雷」
inline std::string mineConfigLabel(const MineConfig &c)
{
    std::string label = minePresetName(minePresetOfConfig(c));
    label += " · ";
    label += std::to_string(c.width);
    label += "×";
    label += std::to_string(c.height);
    label += " · ";
    label += std::to_string(c.mines);
    label += " 雷";
    return label;
}

// ---------------------------------------------------------------------------
// 棋盘
// ---------------------------------------------------------------------------

enum class MineStatus { Ready, Playing, Won, Lost };

struct MineCell {
    bool mine = false;
    bool revealed = false;
    bool flagged = false;
    int adjacent = 0; // 相邻雷数（仅对非雷格有意义）
};

// 一次操作的结果（View 据此决定立绘 / 台词表现）
struct MineMove {
    bool changed = false;  // 棋盘是否发生变化
    bool exploded = false; // 本次踩雷
    int revealed = 0;      // 本次新翻开的格子数
    int chain = 0;         // 当前累计「连续安全翻开」格数（踩雷清零）
    int chainPeak = 0;     // 本局峰值
    bool won = false;      // 本次操作导致胜利
};

// 一局结算数据（成就 / 奖励 / 播报用）
struct MineSummary {
    bool won = false;
    bool perfect = false; // 全对插旗通关（通关时插旗数与雷数一致）
    int maxChain = 0;
    int mineCount = 0;
    int revealedSafe = 0; // 已翻开的非雷格数
    int totalSafe = 0;    // 非雷格总数
};

// 结算档位（对应参考项目 gameGrade 的三档 → 养成奖励表 game-win/draw/lose）
enum class MineGrade {
    Win,  // 通关
    Draw, // 未通关但已完成过半（对照参考项目的「及格」档）
    Lose, // 未通关且进度不足
};

// 档位判定：通关 = Win；未通关但已翻开 ≥ 半数非雷格 = Draw；否则 Lose。
// 纯函数，零依赖，可单测。
inline MineGrade mineGrade(const MineSummary &summary)
{
    if (summary.won) {
        return MineGrade::Win;
    }
    if (summary.totalSafe > 0 && summary.revealedSafe * 2 >= summary.totalSafe) {
        return MineGrade::Draw;
    }
    return MineGrade::Lose;
}

class Minesweeper {
public:
    Minesweeper() = default;
    explicit Minesweeper(IRandom *rng)
        : m_rng(rng)
    {
    }

    void setRandom(IRandom *rng) { m_rng = rng; }

    // 开新局（清空棋盘，等待首点布雷）。配置非法时夹取到合法范围。
    void newGame(const MineConfig &config);

    const MineConfig &config() const { return m_config; }
    int width() const { return m_config.width; }
    int height() const { return m_config.height; }
    int mineCount() const { return m_config.mines; }
    int cellCount() const { return m_config.width * m_config.height; }
    MineStatus status() const { return m_status; }
    int flagCount() const { return m_flagCount; }
    int revealedSafeCount() const { return m_revealedSafe; }
    int remainingMines() const { return m_config.mines - m_flagCount; }
    int maxChain() const { return m_maxChain; }
    int clickCount() const { return m_clicks; }
    bool minesPlaced() const { return m_placed; }

    bool inside(int x, int y) const;
    int indexOf(int x, int y) const { return y * m_config.width + x; }
    const std::vector<MineCell> &cells() const { return m_cells; }
    const MineCell &cell(int index) const { return m_cells[static_cast<std::size_t>(index)]; }

    // 左键：翻开。首次翻开会延迟布雷并保证该格不是雷（首点安全）。
    MineMove reveal(int index);
    // 右键：插旗 / 取消插旗（不会误翻；已翻开的格子无效）
    MineMove toggleFlag(int index);
    // 失败后揭示全部雷（供界面展示），不改变胜负判定
    void revealAllMines();

    // 结算（仅在 Won / Lost 后有意义）
    MineSummary summary() const;

private:
    void placeMines(int safeIndex);
    void computeAdjacent();
    int neighborMines(int index) const;
    void floodReveal(int start, int *revealedOut);
    bool isWin() const;
    void neighborIndices(int index, int *out, int *count) const;

    IRandom *m_rng = nullptr;
    MineConfig m_config;
    std::vector<MineCell> m_cells;
    MineStatus m_status = MineStatus::Ready;
    bool m_placed = false;
    int m_flagCount = 0;
    int m_revealedSafe = 0;
    int m_chain = 0;
    int m_maxChain = 0;
    int m_clicks = 0;
};

} // namespace whalepet::core
