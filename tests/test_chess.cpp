// 小游戏「国际象棋」纯逻辑单测：
//   FEN 往返 / 初始合法着法 / UCI 着法串 / 双步与吃过路兵 / 王车易位（含路径被攻击的拒绝）/
//   兵升变 / 将军·将死·逼和·和棋 / 非法着法拒绝 / 结算折算与难度表。
// 约定同其它 core 用例：零 Qt UI 依赖（QTEST_GUILESS_MAIN，headless 可跑）。
// 参考：docs/MINIGAME-INTERFACE.md；引擎通信见 src/minigame/chess/UciEngine.h（不在本测试内起进程）。

#include "core/Chess.h"

#include <QtTest>

#include <string>
#include <vector>

using namespace whalepet::core;

namespace {

const char *const kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// 判断合法着法列表里是否存在某个 UCI 着法
bool hasUciMove(const std::vector<ChessMove> &moves, const char *uci)
{
    bool ok = false;
    const ChessMove wanted = chessMoveFromUci(uci, &ok);
    if (!ok) {
        return false;
    }
    for (const ChessMove &move : moves) {
        if (move == wanted) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestChess : public QObject {
    Q_OBJECT

private slots:
    void startPositionFenRoundTrips();
    void uciMoveParsingAndFormatting();
    void pawnDoublePushAndEnPassant();
    void castlingKingsideAndQueenside();
    void castlingRejectedWhenPathAttacked();
    void promotionGeneratesFourChoices();
    void checkmateIsDetected();
    void stalemateIsDetected();
    void drawByInsufficientMaterialAndFiftyMove();
    void illegalMoveIsRejected();
    void resultConversionAndCaptureStats();
    void levelTableIsStable();
};

void TestChess::startPositionFenRoundTrips()
{
    ChessGame game;
    QCOMPARE(QString::fromStdString(game.fen()), QString::fromUtf8(kStartFen));
    QVERIFY(game.whiteToMove());
    QCOMPARE(static_cast<int>(game.legalMoves().size()), 20); // 16 兵步 + 4 马步
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(ChessStatus::Playing));

    // 自定义 FEN 解析后回到初始局面
    ChessGame other;
    QVERIFY(other.loadFen(kStartFen));
    QCOMPARE(QString::fromStdString(other.fen()), QString::fromUtf8(kStartFen));

    // 非法 FEN（缺王）必须拒绝，且不改动原局面
    ChessGame invalid;
    QVERIFY(!invalid.loadFen("8/8/8/8/8/8/8/8 w - - 0 1"));
    QCOMPARE(QString::fromStdString(invalid.fen()), QString::fromUtf8(kStartFen));
}

void TestChess::uciMoveParsingAndFormatting()
{
    bool ok = false;
    const ChessMove e2e4 = chessMoveFromUci("e2e4", &ok);
    QVERIFY(ok);
    QCOMPARE(e2e4.from, chessSquareIndex(4, 1)); // e2
    QCOMPARE(e2e4.to, chessSquareIndex(4, 3));   // e4
    QCOMPARE(static_cast<int>(e2e4.promotion), 0);
    QCOMPARE(QString::fromStdString(chessMoveToUci(e2e4)), QStringLiteral("e2e4"));

    const ChessMove promote = chessMoveFromUci("e7e8q", &ok);
    QVERIFY(ok);
    QCOMPARE(static_cast<int>(promote.promotion), static_cast<int>('q'));
    QCOMPARE(QString::fromStdString(chessMoveToUci(promote)), QStringLiteral("e7e8q"));

    // 非法串
    chessMoveFromUci("e2e9", &ok);
    QVERIFY(!ok);
    chessMoveFromUci("e2", &ok);
    QVERIFY(!ok);
    chessMoveFromUci("e7e8x", &ok);
    QVERIFY(!ok);
}

void TestChess::pawnDoublePushAndEnPassant()
{
    ChessGame game;
    QVERIFY(game.makeUciMove("e2e4"));
    // 双步推进后过路兵目标格 = e3
    QCOMPARE(game.enPassantSquare(), chessSquareIndex(4, 2));
    QCOMPARE(QString::fromStdString(game.fen()), QString::fromUtf8(
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"));

    // 直接用哨兵局面验证吃过路兵：白兵 e5、黑兵 d5、过路兵目标 d6
    ChessGame ep;
    QVERIFY(ep.loadFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2"));
    ChessMove capture;
    capture.from = chessSquareIndex(4, 4); // e5
    capture.to = chessSquareIndex(3, 5);   // d6
    QVERIFY(ep.isCaptureMove(capture));    // 目标格为空，但属于吃过路兵
    QVERIFY(ep.makeUciMove("e5d6"));
    QCOMPARE(static_cast<int>(ep.pieceAt(chessSquareIndex(4, 4))), static_cast<int>(' '));
    QCOMPARE(static_cast<int>(ep.pieceAt(chessSquareIndex(3, 4))), static_cast<int>(' ')); // d5 兵被吃
    QCOMPARE(static_cast<int>(ep.pieceAt(chessSquareIndex(3, 5))), static_cast<int>('P')); // 白兵到 d6
    QCOMPARE(ep.enPassantSquare(), -1);                                                    // 目标格已清空
    QCOMPARE(ep.capturedValue(true), 1);                                                   // 吃掉一个兵
}

void TestChess::castlingKingsideAndQueenside()
{
    const char *const fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    const int e1 = chessSquareIndex(4, 0);

    {
        ChessGame game;
        QVERIFY(game.loadFen(fen));
        const std::vector<ChessMove> kingMoves = game.legalMovesFrom(e1);
        QVERIFY(hasUciMove(kingMoves, "e1g1"));
        QVERIFY(hasUciMove(kingMoves, "e1c1"));

        QVERIFY(game.makeUciMove("e1g1")); // 王翼易位
        QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(6, 0))), static_cast<int>('K'));
        QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(5, 0))), static_cast<int>('R'));
        QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(7, 0))), static_cast<int>(' '));
    }
    {
        ChessGame game;
        QVERIFY(game.loadFen(fen));
        QVERIFY(game.makeUciMove("e1c1")); // 后翼易位
        QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(2, 0))), static_cast<int>('K'));
        QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(3, 0))), static_cast<int>('R'));
        QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(0, 0))), static_cast<int>(' '));
    }
}

void TestChess::castlingRejectedWhenPathAttacked()
{
    // 黑车在 f2：攻击 f1 —— 白王翼易位路径（f1）被攻击，必须不合法；
    // 后翼路径（d1/c1）不受影响，仍合法。
    ChessGame game;
    QVERIFY(game.loadFen("4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1"));
    const std::vector<ChessMove> kingMoves = game.legalMovesFrom(chessSquareIndex(4, 0));
    QVERIFY(!hasUciMove(kingMoves, "e1g1"));
    QVERIFY(hasUciMove(kingMoves, "e1c1"));
}

void TestChess::promotionGeneratesFourChoices()
{
    ChessGame game;
    QVERIFY(game.loadFen("k7/4P3/8/8/8/8/8/7K w - - 0 1"));
    const int e7 = chessSquareIndex(4, 6);
    const std::vector<ChessMove> moves = game.legalMovesFrom(e7);

    int promotions = 0;
    for (const ChessMove &move : moves) {
        if (move.promotion != 0) {
            ++promotions;
        }
    }
    QCOMPARE(promotions, 4); // 后 / 车 / 象 / 马

    QVERIFY(game.makeUciMove("e7e8n"));
    QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(4, 7))), static_cast<int>('N'));
}

void TestChess::checkmateIsDetected()
{
    // 傻瓜杀（Fool's mate）：1.f3 e5 2.g4 Qh4#，白方被将死。
    ChessGame game;
    QVERIFY(game.loadFen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"));
    QVERIFY(game.whiteToMove()); // 被将死的一方 = 该走棋的一方
    QVERIFY(game.inCheck(true));
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(ChessStatus::Checkmate));
    QVERIFY(game.gameOver());
}

void TestChess::stalemateIsDetected()
{
    // 黑王 h8 无着可走但不被将军 → 逼和。
    ChessGame game;
    QVERIFY(game.loadFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    QVERIFY(!game.inCheck(false));
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(ChessStatus::Stalemate));
}

void TestChess::drawByInsufficientMaterialAndFiftyMove()
{
    ChessGame bare;
    QVERIFY(bare.loadFen("8/8/8/4k3/8/8/8/4K3 w - - 0 1"));
    QCOMPARE(static_cast<int>(bare.status()), static_cast<int>(ChessStatus::Draw)); // 单王对单王

    ChessGame fifty;
    QVERIFY(fifty.loadFen("8/8/8/4k3/8/8/8/4K3 w - - 100 51"));
    QCOMPARE(static_cast<int>(fifty.status()), static_cast<int>(ChessStatus::Draw)); // 50 回合规则
}

void TestChess::illegalMoveIsRejected()
{
    ChessGame game;

    QVERIFY(!game.makeUciMove("e2e5")); // 兵一次不能走三格
    QVERIFY(!game.makeUciMove("e2e4 ")); // 含空格的非法串
    QVERIFY(!game.makeUciMove("e2e3q")); // 非升变却带升变字符（兵未到末排）

    // 非法着法不得改动局面
    QCOMPARE(static_cast<int>(game.pieceAt(chessSquareIndex(4, 1))), static_cast<int>('P'));

    // 轮到白方时不能动黑子
    QVERIFY(!game.makeUciMove("e7e5"));
}

void TestChess::resultConversionAndCaptureStats()
{
    // 简洁杀（另一条路线）：用傻瓜杀局面，玩家执黑 → 玩家（黑方）取胜且零失误。
    ChessGame mate;
    QVERIFY(mate.loadFen(
        "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"));

    const MiniGameResult blackWin = chessGameResult(mate, /*humanIsWhite=*/false, /*level=*/2, 12345);
    QCOMPARE(QString::fromStdString(blackWin.gameId), QStringLiteral("chess"));
    QCOMPARE(QString::fromStdString(blackWin.difficultyId), QStringLiteral("expert"));
    QVERIFY(blackWin.won);
    QVERIFY(blackWin.perfect); // 赢且没丢子
    QVERIFY(blackWin.expert);
    QCOMPARE(blackWin.elapsedMs, static_cast<std::int64_t>(12345));
    QCOMPARE(blackWin.progressTotal, 39);

    const MiniGameResult whiteLoss = chessGameResult(mate, /*humanIsWhite=*/true, /*level=*/0, 1);
    QVERIFY(!whiteLoss.won);
    QVERIFY(!whiteLoss.expert);

    // 和棋局面：不算获胜
    ChessGame stalemate;
    QVERIFY(stalemate.loadFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    const MiniGameResult draw = chessGameResult(stalemate, true, 1, 100);
    QVERIFY(!draw.won);

    // 吃子进度：1.e4 d5 2.exd5 → 白方吃掉一个兵（价值 1）
    ChessGame play;
    QVERIFY(play.makeUciMove("e2e4"));
    QVERIFY(play.makeUciMove("d7d5"));
    QVERIFY(play.makeUciMove("e4d5"));
    QCOMPARE(play.capturedValue(true), 1);
    QCOMPARE(play.maxCaptureStreak(true), 1);
    const MiniGameResult mid = chessGameResult(play, true, 0, 500);
    QCOMPARE(mid.progressDone, 1);
    QCOMPARE(mid.progressTotal, 39);
    QVERIFY(!mid.won);
}

void TestChess::levelTableIsStable()
{
    QCOMPARE(kChessLevelCount, 3);
    QCOMPARE(QString::fromUtf8(chessLevelOfIndex(0).id), QStringLiteral("beginner"));
    QCOMPARE(QString::fromUtf8(chessLevelOfIndex(2).id), QStringLiteral("expert"));
    QCOMPARE(chessLevelIndexOfId("intermediate"), 1);
    QCOMPARE(chessLevelIndexOfId("unknown-id"), 0);
    QCOMPARE(QString::fromUtf8(chessLevelOfIndex(99).id), QStringLiteral("beginner")); // 越界兜底
}

QTEST_GUILESS_MAIN(TestChess)
#include "test_chess.moc"
