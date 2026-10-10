#include "core/Chess.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace whalepet::core {

namespace {

// 初始局面 FEN
const char *const kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

inline bool onBoard(int file, int rank)
{
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}
inline int indexOf(int file, int rank)
{
    return rank * 8 + file;
}
inline int fileOf(int square)
{
    return square % 8;
}
inline int rankOf(int square)
{
    return square / 8;
}
inline char toUpperChar(char c)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}
inline bool isEmpty(char c)
{
    return c == ' ';
}
inline bool isPiece(char c)
{
    return c != ' ';
}

// 子力点值（P1 N3 B3 R5 Q9），用于结算进度与「零失误」判定。
int pieceValue(char piece)
{
    switch (toUpperChar(piece)) {
    case 'P':
        return 1;
    case 'N':
        return 3;
    case 'B':
        return 3;
    case 'R':
        return 5;
    case 'Q':
        return 9;
    default:
        return 0;
    }
}

const int kKnightDf[8] = {1, 2, 2, 1, -1, -2, -2, -1};
const int kKnightDr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
const int kKingDf[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int kKingDr[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDiagonalDf[4] = {1, 1, -1, -1};
const int kDiagonalDr[4] = {1, -1, 1, -1};
const int kStraightDf[4] = {1, -1, 0, 0};
const int kStraightDr[4] = {0, 0, 1, -1};

// 结算进度分母：除双方的之外全部子力点值（8*1 + 2*3 + 2*3 + 2*5 + 9 = 39）。
constexpr int kTotalMaterialValue = 39;

} // namespace

// 六档棋力（对照市面象棋游戏的「多级别」口径）：
//   深度 3 / 4 / 6 / 8 / 11 / 14 —— 最低档深度 3，最高档深度 14（并用 movetime 兜底）。
//   id 保留 beginner / intermediate / expert 三个既有值，使旧纪录分桶（game.best_ms_chess/*）
//   在升级后仍能对上；新增档位使用新 id。
//   expert 标记「高难档」（成就判定用），只挂在最强的两档上。
const ChessLevel kChessLevels[kChessLevelCount] = {
    {"beginner", "入门", 3, 0, 500, false},
    {"casual", "休闲", 4, 5, 800, false},
    {"intermediate", "中级", 6, 10, 1500, false},
    {"advanced", "高级", 8, 15, 2500, false},
    {"expert", "专家", 11, 20, 4000, true},
    {"master", "大师", 14, 20, 6000, true},
};

char chessPieceUpper(char piece)
{
    return isEmpty(piece) ? ' ' : toUpperChar(piece);
}

bool chessIsWhitePiece(char piece)
{
    return piece >= 'A' && piece <= 'Z';
}

bool operator==(const ChessMove &a, const ChessMove &b)
{
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

bool operator!=(const ChessMove &a, const ChessMove &b)
{
    return !(a == b);
}

int chessSquareIndex(int file, int rank)
{
    if (!onBoard(file, rank)) {
        return -1;
    }
    return indexOf(file, rank);
}

std::string chessSquareName(int square)
{
    if (square < 0 || square >= kChessBoardSize) {
        return std::string();
    }
    std::string out;
    out += static_cast<char>('a' + fileOf(square));
    out += static_cast<char>('1' + rankOf(square));
    return out;
}

std::string chessMoveToUci(const ChessMove &move)
{
    if (move.from < 0 || move.to < 0) {
        return std::string();
    }
    std::string out = chessSquareName(move.from) + chessSquareName(move.to);
    if (move.promotion != 0) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(move.promotion)));
    }
    return out;
}

ChessMove chessMoveFromUci(const std::string &uci, bool *ok)
{
    bool valid = (uci.size() == 4 || uci.size() == 5);
    ChessMove move;
    if (valid) {
        const int fromFile = uci[0] - 'a';
        const int fromRank = uci[1] - '1';
        const int toFile = uci[2] - 'a';
        const int toRank = uci[3] - '1';
        if (!onBoard(fromFile, fromRank) || !onBoard(toFile, toRank)) {
            valid = false;
        } else {
            move.from = indexOf(fromFile, fromRank);
            move.to = indexOf(toFile, toRank);
            if (uci.size() == 5) {
                const char promo = static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4])));
                if (promo == 'q' || promo == 'r' || promo == 'b' || promo == 'n') {
                    move.promotion = promo;
                } else {
                    valid = false;
                }
            }
        }
    }
    if (ok != nullptr) {
        *ok = valid;
    }
    return valid ? move : ChessMove{};
}

int chessLevelIndexOfId(const std::string &id)
{
    for (int i = 0; i < kChessLevelCount; ++i) {
        if (id == kChessLevels[i].id) {
            return i;
        }
    }
    return 0;
}

ChessLevel chessLevelOfIndex(int index)
{
    if (index < 0 || index >= kChessLevelCount) {
        index = 0;
    }
    return kChessLevels[index];
}

// ---------------------------------------------------------------------------
// ChessGame
// ---------------------------------------------------------------------------

ChessGame::ChessGame()
{
    reset();
}

void ChessGame::clearBoard()
{
    for (int i = 0; i < kChessBoardSize; ++i) {
        m_board[i] = ' ';
    }
}

void ChessGame::reset()
{
    loadFen(kStartFen);
}

bool ChessGame::loadFen(const std::string &fen)
{
    std::istringstream iss(fen);
    std::string boardText;
    std::string side;
    std::string castling;
    std::string ep;
    if (!(iss >> boardText >> side >> castling >> ep)) {
        return false;
    }

    char newBoard[kChessBoardSize];
    for (int i = 0; i < kChessBoardSize; ++i) {
        newBoard[i] = ' ';
    }

    int rank = 7;
    int file = 0;
    bool ok = true;
    for (char c : boardText) {
        if (c == '/') {
            if (file != 8) {
                ok = false;
                break;
            }
            --rank;
            file = 0;
            if (rank < 0) {
                ok = false;
                break;
            }
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) {
                ok = false;
                break;
            }
        } else if (std::string("PNBRQKpnbrqk").find(c) != std::string::npos) {
            if (file > 7 || rank < 0) {
                ok = false;
                break;
            }
            newBoard[indexOf(file, rank)] = c;
            ++file;
        } else {
            ok = false;
            break;
        }
    }
    if (!ok || rank != 0 || file != 8) {
        return false;
    }

    // 双方各恰好一个王
    int whiteKings = 0;
    int blackKings = 0;
    for (int i = 0; i < kChessBoardSize; ++i) {
        if (newBoard[i] == 'K') {
            ++whiteKings;
        } else if (newBoard[i] == 'k') {
            ++blackKings;
        }
    }
    if (whiteKings != 1 || blackKings != 1) {
        return false;
    }

    if (side != "w" && side != "b") {
        return false;
    }

    bool newCastle[4] = {false, false, false, false};
    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
            case 'K':
                newCastle[0] = true;
                break;
            case 'Q':
                newCastle[1] = true;
                break;
            case 'k':
                newCastle[2] = true;
                break;
            case 'q':
                newCastle[3] = true;
                break;
            default:
                return false;
            }
        }
    }

    int newEnPassant = -1;
    if (ep != "-") {
        if (ep.size() != 2) {
            return false;
        }
        const int f = ep[0] - 'a';
        const int r = ep[1] - '1';
        if (!onBoard(f, r)) {
            return false;
        }
        newEnPassant = indexOf(f, r);
    }

    int halfmove = 0;
    int fullmove = 1;
    iss >> halfmove >> fullmove; // 可缺省（默认 0 / 1）

    // 校验通过后再提交，避免解析失败留下半成品局面。
    for (int i = 0; i < kChessBoardSize; ++i) {
        m_board[i] = newBoard[i];
    }
    m_whiteToMove = (side == "w");
    for (int i = 0; i < 4; ++i) {
        m_castle[i] = newCastle[i];
    }
    m_enPassant = newEnPassant;
    m_halfmove = halfmove < 0 ? 0 : halfmove;
    m_fullmove = fullmove < 1 ? 1 : fullmove;

    m_capturedByWhite = 0;
    m_capturedByBlack = 0;
    m_streakWhite = 0;
    m_streakBlack = 0;
    m_maxStreakWhite = 0;
    m_maxStreakBlack = 0;
    return true;
}

std::string ChessGame::fen() const
{
    std::string out;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const char c = m_board[indexOf(file, rank)];
            if (c == ' ') {
                ++empty;
                continue;
            }
            if (empty > 0) {
                out += static_cast<char>('0' + empty);
                empty = 0;
            }
            out += c;
        }
        if (empty > 0) {
            out += static_cast<char>('0' + empty);
        }
        if (rank > 0) {
            out += '/';
        }
    }
    out += ' ';
    out += m_whiteToMove ? 'w' : 'b';
    out += ' ';

    std::string castling;
    if (m_castle[0]) {
        castling += 'K';
    }
    if (m_castle[1]) {
        castling += 'Q';
    }
    if (m_castle[2]) {
        castling += 'k';
    }
    if (m_castle[3]) {
        castling += 'q';
    }
    out += castling.empty() ? "-" : castling;
    out += ' ';
    out += (m_enPassant >= 0) ? chessSquareName(m_enPassant) : std::string("-");
    out += ' ';
    out += std::to_string(m_halfmove);
    out += ' ';
    out += std::to_string(m_fullmove);
    return out;
}

char ChessGame::pieceAt(int square) const
{
    if (square < 0 || square >= kChessBoardSize) {
        return ' ';
    }
    return m_board[square];
}

int ChessGame::kingSquare(bool white) const
{
    const char king = white ? 'K' : 'k';
    for (int i = 0; i < kChessBoardSize; ++i) {
        if (m_board[i] == king) {
            return i;
        }
    }
    return -1;
}

bool ChessGame::isSquareAttacked(int square, bool byWhite) const
{
    const int f = fileOf(square);
    const int r = rankOf(square);

    // 兵：白兵从目标格「下一 rank」攻击（斜前方），黑兵反之。
    const int pawnRank = byWhite ? r - 1 : r + 1;
    const char pawn = byWhite ? 'P' : 'p';
    for (int df : {-1, 1}) {
        const int nf = f + df;
        if (onBoard(nf, pawnRank) && m_board[indexOf(nf, pawnRank)] == pawn) {
            return true;
        }
    }

    // 马
    const char knight = byWhite ? 'N' : 'n';
    for (int k = 0; k < 8; ++k) {
        const int nf = f + kKnightDf[k];
        const int nr = r + kKnightDr[k];
        if (onBoard(nf, nr) && m_board[indexOf(nf, nr)] == knight) {
            return true;
        }
    }

    // 王
    const char king = byWhite ? 'K' : 'k';
    for (int k = 0; k < 8; ++k) {
        const int nf = f + kKingDf[k];
        const int nr = r + kKingDr[k];
        if (onBoard(nf, nr) && m_board[indexOf(nf, nr)] == king) {
            return true;
        }
    }

    // 斜线：象 / 后
    const char bishop = byWhite ? 'B' : 'b';
    const char queen = byWhite ? 'Q' : 'q';
    for (int k = 0; k < 4; ++k) {
        int nf = f + kDiagonalDf[k];
        int nr = r + kDiagonalDr[k];
        while (onBoard(nf, nr)) {
            const char c = m_board[indexOf(nf, nr)];
            if (isPiece(c)) {
                if (c == bishop || c == queen) {
                    return true;
                }
                break;
            }
            nf += kDiagonalDf[k];
            nr += kDiagonalDr[k];
        }
    }

    // 直线：车 / 后
    const char rook = byWhite ? 'R' : 'r';
    for (int k = 0; k < 4; ++k) {
        int nf = f + kStraightDf[k];
        int nr = r + kStraightDr[k];
        while (onBoard(nf, nr)) {
            const char c = m_board[indexOf(nf, nr)];
            if (isPiece(c)) {
                if (c == rook || c == queen) {
                    return true;
                }
                break;
            }
            nf += kStraightDf[k];
            nr += kStraightDr[k];
        }
    }

    return false;
}

bool ChessGame::inCheck(bool white) const
{
    const int king = kingSquare(white);
    if (king < 0) {
        return false;
    }
    return isSquareAttacked(king, !white);
}

void ChessGame::generatePseudoMoves(std::vector<ChessMove> &out) const
{
    const bool white = m_whiteToMove;
    const auto own = [white](char c) { return isPiece(c) && chessIsWhitePiece(c) == white; };
    const auto enemy = [white](char c) { return isPiece(c) && chessIsWhitePiece(c) != white; };

    const auto addStep = [&](int from, int f, int r, int df, int dr) {
        const int nf = f + df;
        const int nr = r + dr;
        if (!onBoard(nf, nr)) {
            return;
        }
        const int to = indexOf(nf, nr);
        if (own(m_board[to])) {
            return;
        }
        out.push_back(ChessMove{from, to, 0});
    };

    const auto addSlide = [&](int from, int f, int r, int df, int dr) {
        int nf = f + df;
        int nr = r + dr;
        while (onBoard(nf, nr)) {
            const int to = indexOf(nf, nr);
            const char target = m_board[to];
            if (own(target)) {
                break;
            }
            out.push_back(ChessMove{from, to, 0});
            if (isPiece(target)) {
                break;
            }
            nf += df;
            nr += dr;
        }
    };

    const auto addPawnAdvance = [&](int from, int to, bool promote) {
        if (promote) {
            out.push_back(ChessMove{from, to, 'q'});
            out.push_back(ChessMove{from, to, 'r'});
            out.push_back(ChessMove{from, to, 'b'});
            out.push_back(ChessMove{from, to, 'n'});
        } else {
            out.push_back(ChessMove{from, to, 0});
        }
    };

    // 王车易位（王翼 / 后翼）：需易位权、路径空、王与经过格不被攻击。
    const auto addCastling = [&](bool side) {
        const bool other = !side;
        if (side) {
            if (m_castle[0] && m_board[4] == 'K' && m_board[5] == ' ' && m_board[6] == ' '
                && m_board[7] == 'R' && !isSquareAttacked(4, other) && !isSquareAttacked(5, other)
                && !isSquareAttacked(6, other)) {
                out.push_back(ChessMove{4, 6, 0});
            }
            if (m_castle[1] && m_board[4] == 'K' && m_board[3] == ' ' && m_board[2] == ' '
                && m_board[1] == ' ' && m_board[0] == 'R' && !isSquareAttacked(4, other)
                && !isSquareAttacked(3, other) && !isSquareAttacked(2, other)) {
                out.push_back(ChessMove{4, 2, 0});
            }
        } else {
            if (m_castle[2] && m_board[60] == 'k' && m_board[61] == ' ' && m_board[62] == ' '
                && m_board[63] == 'r' && !isSquareAttacked(60, other)
                && !isSquareAttacked(61, other) && !isSquareAttacked(62, other)) {
                out.push_back(ChessMove{60, 62, 0});
            }
            if (m_castle[3] && m_board[60] == 'k' && m_board[59] == ' ' && m_board[58] == ' '
                && m_board[57] == ' ' && m_board[56] == 'r' && !isSquareAttacked(60, other)
                && !isSquareAttacked(59, other) && !isSquareAttacked(58, other)) {
                out.push_back(ChessMove{60, 58, 0});
            }
        }
    };

    for (int sq = 0; sq < kChessBoardSize; ++sq) {
        const char piece = m_board[sq];
        if (piece == ' ' || chessIsWhitePiece(piece) != white) {
            continue;
        }
        const int f = fileOf(sq);
        const int r = rankOf(sq);
        switch (toUpperChar(piece)) {
        case 'P': {
            const int dir = white ? 1 : -1;
            const int startRank = white ? 1 : 6;
            const int promoRank = white ? 7 : 0;
            const int r1 = r + dir;

            if (onBoard(f, r1) && m_board[indexOf(f, r1)] == ' ') {
                addPawnAdvance(sq, indexOf(f, r1), r1 == promoRank);
                const int r2 = r + 2 * dir;
                if (r == startRank && onBoard(f, r2) && m_board[indexOf(f, r2)] == ' ') {
                    out.push_back(ChessMove{sq, indexOf(f, r2), 0});
                }
            }
            for (int df : {-1, 1}) {
                const int nf = f + df;
                if (!onBoard(nf, r1)) {
                    continue;
                }
                const int to = indexOf(nf, r1);
                const char target = m_board[to];
                if (enemy(target)) {
                    addPawnAdvance(sq, to, r1 == promoRank);
                } else if (to == m_enPassant && target == ' ') {
                    out.push_back(ChessMove{sq, to, 0});
                }
            }
            break;
        }
        case 'N':
            for (int k = 0; k < 8; ++k) {
                addStep(sq, f, r, kKnightDf[k], kKnightDr[k]);
            }
            break;
        case 'B':
            for (int k = 0; k < 4; ++k) {
                addSlide(sq, f, r, kDiagonalDf[k], kDiagonalDr[k]);
            }
            break;
        case 'R':
            for (int k = 0; k < 4; ++k) {
                addSlide(sq, f, r, kStraightDf[k], kStraightDr[k]);
            }
            break;
        case 'Q':
            for (int k = 0; k < 4; ++k) {
                addSlide(sq, f, r, kDiagonalDf[k], kDiagonalDr[k]);
            }
            for (int k = 0; k < 4; ++k) {
                addSlide(sq, f, r, kStraightDf[k], kStraightDr[k]);
            }
            break;
        case 'K':
            for (int k = 0; k < 8; ++k) {
                addStep(sq, f, r, kKingDf[k], kKingDr[k]);
            }
            addCastling(white);
            break;
        default:
            break;
        }
    }
}

void ChessGame::legalMovesInto(std::vector<ChessMove> &out) const
{
    std::vector<ChessMove> pseudo;
    generatePseudoMoves(pseudo);
    const bool moverWhite = m_whiteToMove;
    for (const ChessMove &move : pseudo) {
        ChessGame copy = *this;
        copy.applyUnchecked(move);
        // applyUnchecked 会翻转走子方；此处校验「刚走子的一方」是否被将军。
        if (!copy.inCheck(moverWhite)) {
            out.push_back(move);
        }
    }
}

std::vector<ChessMove> ChessGame::legalMoves() const
{
    std::vector<ChessMove> out;
    legalMovesInto(out);
    return out;
}

std::vector<ChessMove> ChessGame::legalMovesFrom(int square) const
{
    std::vector<ChessMove> out;
    if (square < 0 || square >= kChessBoardSize) {
        return out;
    }
    for (const ChessMove &move : legalMoves()) {
        if (move.from == square) {
            out.push_back(move);
        }
    }
    return out;
}

bool ChessGame::isLegalMove(const ChessMove &move) const
{
    if (move.from < 0 || move.from >= kChessBoardSize || move.to < 0 || move.to >= kChessBoardSize) {
        return false;
    }
    const char piece = m_board[move.from];
    if (piece == ' ' || chessIsWhitePiece(piece) != m_whiteToMove) {
        return false;
    }
    for (const ChessMove &legal : legalMoves()) {
        if (legal == move) {
            return true;
        }
    }
    return false;
}

bool ChessGame::isCaptureMove(const ChessMove &move) const
{
    if (move.from < 0 || move.from >= kChessBoardSize || move.to < 0 || move.to >= kChessBoardSize) {
        return false;
    }
    if (isPiece(m_board[move.to])) {
        return true;
    }
    // 吃过路兵：目标格为空，但目标格恰为过路兵格且是斜向的兵着法。
    const char piece = m_board[move.from];
    return toUpperChar(piece) == 'P' && move.to == m_enPassant
           && fileOf(move.from) != fileOf(move.to);
}

bool ChessGame::makeMove(const ChessMove &move)
{
    if (!isLegalMove(move)) {
        return false;
    }
    applyUnchecked(move);
    return true;
}

bool ChessGame::makeUciMove(const std::string &uci)
{
    bool ok = false;
    const ChessMove move = chessMoveFromUci(uci, &ok);
    if (!ok) {
        return false;
    }
    return makeMove(move);
}

void ChessGame::applyUnchecked(const ChessMove &move)
{
    const char piece = m_board[move.from];
    const bool white = chessIsWhitePiece(piece);
    const char type = toUpperChar(piece);
    const int previousEnPassant = m_enPassant;

    bool capture = false;
    char capturedPiece = ' ';

    if (type == 'P' && move.to == previousEnPassant && m_board[move.to] == ' '
        && fileOf(move.from) != fileOf(move.to)) {
        // 吃过路兵：被吃兵在目标格「后方」。
        const int capturedSquare = white ? (move.to - 8) : (move.to + 8);
        capturedPiece = m_board[capturedSquare];
        capture = isPiece(capturedPiece);
        m_board[capturedSquare] = ' ';
    } else if (isPiece(m_board[move.to])) {
        capturedPiece = m_board[move.to];
        capture = true;
    }

    // 王车易位：王横移两格时同步挪车。
    if (type == 'K' && std::abs(move.to - move.from) == 2) {
        if (move.to == move.from + 2) {
            m_board[move.from + 1] = m_board[move.from + 3];
            m_board[move.from + 3] = ' ';
        } else {
            m_board[move.from - 1] = m_board[move.from - 4];
            m_board[move.from - 4] = ' ';
        }
    }

    m_board[move.from] = ' ';
    m_board[move.to] = piece;

    // 兵升变（默认升后）
    if (type == 'P' && (rankOf(move.to) == 7 || rankOf(move.to) == 0)) {
        const char promo = (move.promotion != 0) ? move.promotion : 'q';
        m_board[move.to] =
            white ? toUpperChar(promo) : static_cast<char>(std::tolower(static_cast<unsigned char>(promo)));
    }

    // 易位权：王移动清两侧；车离开原位清该侧；车被吃清该侧。
    if (type == 'K') {
        m_castle[white ? 0 : 2] = false;
        m_castle[white ? 1 : 3] = false;
    }
    if (type == 'R') {
        if (move.from == 0) {
            m_castle[1] = false;
        }
        if (move.from == 7) {
            m_castle[0] = false;
        }
        if (move.from == 56) {
            m_castle[3] = false;
        }
        if (move.from == 63) {
            m_castle[2] = false;
        }
    }
    if (move.to == 0) {
        m_castle[1] = false;
    }
    if (move.to == 7) {
        m_castle[0] = false;
    }
    if (move.to == 56) {
        m_castle[3] = false;
    }
    if (move.to == 63) {
        m_castle[2] = false;
    }

    // 过路兵目标格：仅由兵的双步推进产生。
    m_enPassant = -1;
    if (type == 'P' && std::abs(move.to - move.from) == 16) {
        m_enPassant = (move.from + move.to) / 2;
    }

    if (type == 'P' || capture) {
        m_halfmove = 0;
    } else {
        ++m_halfmove;
    }
    if (!white) {
        ++m_fullmove;
    }

    // 吃子统计与连击
    const int value = capture ? pieceValue(capturedPiece) : 0;
    if (white) {
        if (capture) {
            m_capturedByWhite += value;
            ++m_streakWhite;
            m_maxStreakWhite = std::max(m_maxStreakWhite, m_streakWhite);
        } else {
            m_streakWhite = 0;
        }
    } else {
        if (capture) {
            m_capturedByBlack += value;
            ++m_streakBlack;
            m_maxStreakBlack = std::max(m_maxStreakBlack, m_streakBlack);
        } else {
            m_streakBlack = 0;
        }
    }

    m_whiteToMove = !m_whiteToMove;
}

bool ChessGame::insufficientMaterial() const
{
    int whiteMinors = 0;
    int blackMinors = 0;
    for (int i = 0; i < kChessBoardSize; ++i) {
        switch (m_board[i]) {
        case 'P':
        case 'p':
        case 'R':
        case 'r':
        case 'Q':
        case 'q':
            return false; // 还有兵 / 车 / 后 → 不算子力不足
        case 'N':
        case 'B':
            ++whiteMinors;
            break;
        case 'n':
        case 'b':
            ++blackMinors;
            break;
        default:
            break;
        }
    }
    return whiteMinors <= 1 && blackMinors <= 1;
}

ChessStatus ChessGame::status() const
{
    std::vector<ChessMove> moves;
    legalMovesInto(moves);
    if (!moves.empty()) {
        if (m_halfmove >= 100) {
            return ChessStatus::Draw; // 50 回合规则
        }
        if (insufficientMaterial()) {
            return ChessStatus::Draw;
        }
        return ChessStatus::Playing;
    }
    return inCheck(m_whiteToMove) ? ChessStatus::Checkmate : ChessStatus::Stalemate;
}

int ChessGame::capturedValue(bool byWhite) const
{
    return byWhite ? m_capturedByWhite : m_capturedByBlack;
}

int ChessGame::maxCaptureStreak(bool white) const
{
    return white ? m_maxStreakWhite : m_maxStreakBlack;
}

MiniGameResult chessGameResult(const ChessGame &game, bool humanIsWhite, int levelIndex,
                               std::int64_t elapsedMs)
{
    if (levelIndex < 0 || levelIndex >= kChessLevelCount) {
        levelIndex = 0;
    }
    const ChessLevel level = kChessLevels[levelIndex];

    MiniGameResult result;
    result.gameId = "chess";
    result.difficultyId = level.id;
    result.difficultyLabel = level.label;
    result.elapsedMs = elapsedMs;
    result.expert = level.expert;

    // 进度 = 玩家吃掉的子力点值 / 全部子力点值，用于「未胜但过半 → 及格档」判定。
    result.progressDone = game.capturedValue(humanIsWhite);
    result.progressTotal = kTotalMaterialValue;
    result.maxChain = game.maxCaptureStreak(humanIsWhite);

    const ChessStatus status = game.status();
    if (status == ChessStatus::Checkmate) {
        // 被将死的一方 = 当前该走棋的一方。
        result.won = (game.whiteToMove() != humanIsWhite);
        // 零失误：赢了且一个子都没被吃掉。
        result.perfect = result.won && game.capturedValue(!humanIsWhite) == 0;
    } else {
        result.won = false;
        result.perfect = false;
    }
    return result;
}

} // namespace whalepet::core
