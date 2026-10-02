#pragma once

// 小游戏：国际象棋（**纯逻辑，零 Qt 依赖**，可脱 UI 单测）。
//
// 分层边界（与扫雷 / 找小猫一致）：
//   * 本文件只负责**棋盘规则**：局面表示、合法着法生成（含王车易位 / 吃过路兵 /
//     兵升变）、将军与将死 / 逼和 / 和棋判定、FEN 编解码、UCI 着法串互转。
//   * 本层**不自带棋力**：对手由外部 UCI 引擎经 QProcess 驱动（见
//     src/minigame/chess/UciEngine.h）。本层提供「当前局面 FEN」与「落子是否合法」，
//     引擎返回的 bestmove 仍会经本层校验后再落盘，防止非法着法污染棋局。
//   * 一局结果折算为通用契约 core::MiniGameResult，交宿主统一发放奖励 / 上报成就。
//
// 坐标约定：0 = a1，7 = h1，8 = a2，…，63 = h8（file = index % 8，rank = index / 8）。
// 棋子编码沿用 FEN 字符：大写白方 PNBRQK，小写黑方 pnbrqk，' ' 表示空格。

#include "core/MiniGameTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace whalepet::core {

inline constexpr int kChessBoardSize = 64;

char chessPieceUpper(char piece);      // 归一化为大写；空格原样返回
bool chessIsWhitePiece(char piece);    // 大写（含空格外）视为白方

// 一个着法：from / to 为格子索引，promotion 为升变棋子（'q'/'r'/'b'/'n'）或 0。
struct ChessMove {
    int from = -1;
    int to = -1;
    char promotion = 0;
};

bool operator==(const ChessMove &a, const ChessMove &b);
bool operator!=(const ChessMove &a, const ChessMove &b);

// 坐标 / UCI 着法串（"e2e4" / "e7e8q"）。
std::string chessSquareName(int square);              // 0 → "a1"
int chessSquareIndex(int file, int rank);             // (0,0) → 0
std::string chessMoveToUci(const ChessMove &move);
ChessMove chessMoveFromUci(const std::string &uci, bool *ok = nullptr);

// 对局状态（由 ChessGame::status() 给出）。
enum class ChessStatus {
    Playing,   // 对局进行中
    Checkmate, // 该走棋一方被将死（胜负已分）
    Stalemate, // 该走棋一方无着可走但未被将军（逼和）
    Draw,      // 50 回合 / 子力不足等和棋
};

// 引擎棋力档位（同时决定 UCI 的 Skill Level 与思考时间）。
struct ChessLevel {
    const char *id;    // 稳定标识（落库 / 纪录分桶），如 "beginner"
    const char *label; // 展示文案，如 "入门"
    int skill;         // UCI Skill Level（0–20）
    int moveTimeMs;    // go movetime 毫秒
};

inline constexpr int kChessLevelCount = 3;
extern const ChessLevel kChessLevels[kChessLevelCount];
int chessLevelIndexOfId(const std::string &id); // 未知 → 0
ChessLevel chessLevelOfIndex(int index);        // 越界 → 档位 0

// 一局国际象棋：局面 + 规则 + 统计。
class ChessGame {
public:
    ChessGame();

    void reset();                         // 回到初始局面
    bool loadFen(const std::string &fen); // 解析 FEN（失败返回 false，且不改动原局面）
    std::string fen() const;              // 当前局面 FEN（供 UCI position 使用）

    bool whiteToMove() const { return m_whiteToMove; }
    char pieceAt(int square) const;                      // ' ' 表示空
    int enPassantSquare() const { return m_enPassant; }  // -1 表示无

    // 合法着法（已过滤「走后自家王被将军」的非法着法）
    std::vector<ChessMove> legalMoves() const;
    std::vector<ChessMove> legalMovesFrom(int square) const;
    bool isLegalMove(const ChessMove &move) const;
    bool isCaptureMove(const ChessMove &move) const; // 目标格有子或为吃过路兵

    bool makeMove(const ChessMove &move);     // 非法返回 false 且不改动局面
    bool makeUciMove(const std::string &uci); // 同上（引擎 / 界面统一入口）

    bool inCheck(bool white) const;
    ChessStatus status() const;
    bool gameOver() const { return status() != ChessStatus::Playing; }

    // 统计（用于结算折算）
    int capturedValue(bool byWhite) const;  // 该方吃掉的子力点值（P1 N3 B3 R5 Q9）
    int maxCaptureStreak(bool white) const; // 该方连续吃子的最长连击

private:
    void clearBoard();
    void generatePseudoMoves(std::vector<ChessMove> &out) const;
    void legalMovesInto(std::vector<ChessMove> &out) const;
    void applyUnchecked(const ChessMove &move);
    bool isSquareAttacked(int square, bool byWhite) const;
    bool insufficientMaterial() const;
    int kingSquare(bool white) const;

    char m_board[kChessBoardSize];
    bool m_whiteToMove = true;
    bool m_castle[4] = {true, true, true, true}; // 白K / 白Q / 黑K / 黑Q
    int m_enPassant = -1;
    int m_halfmove = 0;
    int m_fullmove = 1;

    int m_capturedByWhite = 0; // 白方吃掉的子力点值
    int m_capturedByBlack = 0;
    int m_streakWhite = 0; // 当前连击（连续吃子，非吃子即清零）
    int m_streakBlack = 0;
    int m_maxStreakWhite = 0;
    int m_maxStreakBlack = 0;
};

// 对局 → 通用结算契约。
//   humanIsWhite：玩家执白与否（决定胜负归属）；
//   levelIndex  ：引擎棋力档位（影响 difficultyId / difficultyLabel / expert）；
//   elapsedMs   ：本局用时（毫秒）。
MiniGameResult chessGameResult(const ChessGame &game, bool humanIsWhite, int levelIndex,
                               std::int64_t elapsedMs);

} // namespace whalepet::core
