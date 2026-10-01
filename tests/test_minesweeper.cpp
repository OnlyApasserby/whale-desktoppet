// 小游戏「扫雷」纯逻辑单测：预设与自定义校验 / 首点安全布雷 / 翻格与插旗 / 胜负与连翻。
// 约定同其它 core 用例：零 Qt UI 依赖（QTEST_GUILESS_MAIN），随机源可注入（IRandom）。
// 参考：docs/MINIGAME-INTERFACE.md。

#include "core/IRandom.h"
#include "core/Minesweeper.h"

#include <QtTest>

#include <cstdint>
#include <string>
#include <vector>

using namespace whalepet::core;

namespace {

// 找一个「首点之后仍在进行中」的随机种子，供需要完整对局的用例复用。
// （首点即通关属于极小概率，直接跳过即可。）
int findPlayingSeed(int from = 1, int to = 200)
{
    for (int seed = from; seed < to; ++seed) {
        SystemRandom rng(static_cast<std::uint32_t>(seed));
        Minesweeper game(&rng);
        game.newGame(MineConfig{9, 9, 10});
        game.reveal(0);
        if (game.status() == MineStatus::Playing) {
            return seed;
        }
    }
    return -1;
}

} // namespace

class TestMinesweeper : public QObject {
    Q_OBJECT

private slots:
    void presetTableMatchesRequiredSpec();
    void configValidationAndLabel();
    void newGameClampsInvalidConfig();
    void presetOfConfigRoundTrip();
    void firstClickIsAlwaysSafe();
    void mineCountAndAdjacencyAreConsistent();
    void floodRevealOpensZeroRegion();
    void flagToggleAndGuards();
    void explodeEndsGameAndRevealsMines();
    void winAndPerfectSummary();
    void gradeIsDerivedFromOutcomeAndProgress();
    void chainPeakTracksSafeReveals();
    void deterministicWithScriptedRandom();
};

void TestMinesweeper::presetTableMatchesRequiredSpec()
{
    QCOMPARE(kMinePresetCount, 3);
    // 需求规格：9×9/10、16×16/40、30×16/99
    QCOMPARE(kMinePresets[0].width, 9);
    QCOMPARE(kMinePresets[0].height, 9);
    QCOMPARE(kMinePresets[0].mines, 10);
    QCOMPARE(kMinePresets[1].width, 16);
    QCOMPARE(kMinePresets[1].height, 16);
    QCOMPARE(kMinePresets[1].mines, 40);
    QCOMPARE(kMinePresets[2].width, 30);
    QCOMPARE(kMinePresets[2].height, 16);
    QCOMPARE(kMinePresets[2].mines, 99);
}

void TestMinesweeper::configValidationAndLabel()
{
    QVERIFY(mineConfigValid(MineConfig{9, 9, 10}));
    QVERIFY(mineConfigValid(MineConfig{9, 9, 80}));  // 81 格最多 80 雷
    QVERIFY(!mineConfigValid(MineConfig{9, 9, 81})); // 雷数超上限
    QVERIFY(!mineConfigValid(MineConfig{9, 9, 0}));  // 至少 1 颗雷
    QVERIFY(!mineConfigValid(MineConfig{4, 9, 10}));  // 宽低于下限
    QVERIFY(!mineConfigValid(MineConfig{31, 9, 10})); // 宽超上限
    QVERIFY(!mineConfigValid(MineConfig{9, 25, 10})); // 高超上限

    const QString presetLabel = QString::fromStdString(mineConfigLabel(MineConfig{9, 9, 10}));
    QVERIFY(presetLabel.startsWith(QString::fromUtf8("初级")));
    QVERIFY(presetLabel.contains(QString::fromUtf8("9×9")));
    QVERIFY(presetLabel.contains(QString::fromUtf8("10")));

    const QString customLabel = QString::fromStdString(mineConfigLabel(MineConfig{12, 10, 20}));
    QVERIFY(customLabel.startsWith(QString::fromUtf8("自定义")));
    QVERIFY(customLabel.contains(QString::fromUtf8("12×10")));
    QVERIFY(customLabel.contains(QString::fromUtf8("20")));
}

void TestMinesweeper::newGameClampsInvalidConfig()
{
    SystemRandom rng(1);
    Minesweeper game(&rng);

    game.newGame(MineConfig{9, 9, 999}); // 雷数夹取到上限 80
    QCOMPARE(game.width(), 9);
    QCOMPARE(game.height(), 9);
    QCOMPARE(game.mineCount(), 80);

    game.newGame(MineConfig{200, 200, 10}); // 尺寸越界 → 回退初级预设
    QCOMPARE(game.width(), 9);
    QCOMPARE(game.height(), 9);
    QCOMPARE(game.mineCount(), 10);

    QCOMPARE(game.status(), MineStatus::Ready);
    QVERIFY(!game.minesPlaced());
    QCOMPARE(game.flagCount(), 0);
    QCOMPARE(game.revealedSafeCount(), 0);
}

void TestMinesweeper::presetOfConfigRoundTrip()
{
    for (const MinePresetDef &p : kMinePresets) {
        const MineConfig c = mineConfigOfPreset(p.preset);
        QCOMPARE(c.width, p.width);
        QCOMPARE(c.height, p.height);
        QCOMPARE(c.mines, p.mines);
        QCOMPARE(minePresetOfConfig(c), p.preset);
        QVERIFY(mineConfigValid(c));
    }
    QCOMPARE(minePresetOfConfig(MineConfig{12, 10, 20}), MinePreset::Custom);
    QVERIFY(std::string(minePresetName(MinePreset::Custom)) == std::string("自定义"));
    // 未知预设 → 退化为默认 9×9/10
    const MineConfig fallback = mineConfigOfPreset(static_cast<MinePreset>(99));
    QCOMPARE(fallback.width, 9);
    QCOMPARE(fallback.height, 9);
    QCOMPARE(fallback.mines, 10);
}

void TestMinesweeper::firstClickIsAlwaysSafe()
{
    for (int seed = 0; seed < 10; ++seed) {
        SystemRandom rng(static_cast<std::uint32_t>(seed));
        Minesweeper game(&rng);
        game.newGame(MineConfig{9, 9, 10});

        QCOMPARE(game.status(), MineStatus::Ready);
        QVERIFY(!game.minesPlaced());

        const MineMove move = game.reveal(0);
        QVERIFY(game.minesPlaced());
        QVERIFY(!move.exploded);
        QVERIFY(!game.cell(0).mine);
        QVERIFY(game.status() != MineStatus::Lost);
        QVERIFY(move.revealed >= 1);
    }
}

void TestMinesweeper::mineCountAndAdjacencyAreConsistent()
{
    SystemRandom rng(12345);
    Minesweeper game(&rng);
    game.newGame(MineConfig{16, 16, 40});
    game.reveal(5);

    int mines = 0;
    for (int i = 0; i < game.cellCount(); ++i) {
        const MineCell &c = game.cell(i);
        if (c.mine) {
            ++mines;
            continue;
        }
        const int x = i % game.width();
        const int y = i / game.width();
        int expected = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = x + dx;
                const int ny = y + dy;
                if (game.inside(nx, ny) && game.cell(game.indexOf(nx, ny)).mine) {
                    ++expected;
                }
            }
        }
        QCOMPARE(c.adjacent, expected);
    }
    QCOMPARE(mines, 40);
}

void TestMinesweeper::floodRevealOpensZeroRegion()
{
    SystemRandom rng(777);
    Minesweeper game(&rng);
    game.newGame(MineConfig{9, 9, 10});
    game.reveal(40); // 从中心翻开

    for (int i = 0; i < game.cellCount(); ++i) {
        const MineCell &c = game.cell(i);
        if (!c.revealed || c.mine || c.adjacent != 0) {
            continue;
        }
        // 0 邻格：8 邻域内不应有雷（adjacent==0），且必须全部被翻开
        const int x = i % game.width();
        const int y = i / game.width();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (!game.inside(nx, ny)) {
                    continue;
                }
                const int ni = game.indexOf(nx, ny);
                QVERIFY(!game.cell(ni).mine);
                QVERIFY(game.cell(ni).revealed);
            }
        }
    }
}

void TestMinesweeper::flagToggleAndGuards()
{
    SystemRandom rng(3);
    Minesweeper game(&rng);
    game.newGame(MineConfig{9, 9, 10});

    QCOMPARE(game.flagCount(), 0);
    QCOMPARE(game.remainingMines(), 10);

    QVERIFY(game.toggleFlag(3).changed);
    QCOMPARE(game.flagCount(), 1);
    QCOMPARE(game.remainingMines(), 9);
    QVERIFY(game.toggleFlag(3).changed);
    QCOMPARE(game.flagCount(), 0);

    // 已插旗的格子不能被翻开，也不会触发布雷
    QVERIFY(game.toggleFlag(4).changed);
    QVERIFY(!game.reveal(4).changed);
    QVERIFY(!game.cell(4).revealed);
    QVERIFY(!game.minesPlaced());

    // 越界索引安全
    QVERIFY(!game.toggleFlag(-1).changed);
    QVERIFY(!game.toggleFlag(9999).changed);
    QVERIFY(!game.reveal(9999).changed);

    // 已翻开的格子不能再插旗 / 重复翻开
    QVERIFY(game.reveal(0).changed);
    QVERIFY(!game.toggleFlag(0).changed);
    QVERIFY(!game.reveal(0).changed);
}

void TestMinesweeper::explodeEndsGameAndRevealsMines()
{
    const int seed = findPlayingSeed();
    QVERIFY(seed > 0);

    SystemRandom rng(static_cast<std::uint32_t>(seed));
    Minesweeper game(&rng);
    game.newGame(MineConfig{9, 9, 10});
    game.reveal(0);
    QCOMPARE(game.status(), MineStatus::Playing);

    int mineIndex = -1;
    for (int i = 0; i < game.cellCount(); ++i) {
        if (game.cell(i).mine) {
            mineIndex = i;
            break;
        }
    }
    QVERIFY(mineIndex >= 0);
    QVERIFY(!game.cell(mineIndex).revealed);

    const MineMove move = game.reveal(mineIndex);
    QVERIFY(move.changed);
    QVERIFY(move.exploded);
    QCOMPARE(move.chain, 0); // 踩雷清零连翻
    QCOMPARE(game.status(), MineStatus::Lost);

    // 失败后全部雷被揭示
    for (int i = 0; i < game.cellCount(); ++i) {
        if (game.cell(i).mine) {
            QVERIFY(game.cell(i).revealed);
        }
    }
    // 结束后不再接受操作
    QVERIFY(!game.reveal(1).changed);
    QVERIFY(!game.toggleFlag(2).changed);
}

void TestMinesweeper::winAndPerfectSummary()
{
    const int seed = findPlayingSeed();
    QVERIFY(seed > 0);

    // 不插旗通关 → won=true、perfect=false
    {
        SystemRandom rng(static_cast<std::uint32_t>(seed));
        Minesweeper game(&rng);
        game.newGame(MineConfig{9, 9, 10});
        game.reveal(0);
        QCOMPARE(game.status(), MineStatus::Playing);

        for (int i = 0; i < game.cellCount() && game.status() != MineStatus::Won; ++i) {
            if (!game.cell(i).mine && !game.cell(i).revealed) {
                game.reveal(i);
            }
        }
        QCOMPARE(game.status(), MineStatus::Won);
        const MineSummary s = game.summary();
        QVERIFY(s.won);
        QVERIFY(!s.perfect); // 未插旗
        QCOMPARE(s.mineCount, 10);
        // 结算快照带上进度（档位判定与奖励展示依赖它）
        QCOMPARE(s.totalSafe, game.cellCount() - game.mineCount());
        QCOMPARE(s.revealedSafe, s.totalSafe); // 通关时非雷格全开
    }

    // 全对插旗通关 → won=true、perfect=true
    {
        SystemRandom rng(static_cast<std::uint32_t>(seed));
        Minesweeper game(&rng);
        game.newGame(MineConfig{9, 9, 10});
        game.reveal(0);
        QCOMPARE(game.status(), MineStatus::Playing);

        for (int i = 0; i < game.cellCount(); ++i) {
            if (game.cell(i).mine) {
                game.toggleFlag(i);
            }
        }
        QCOMPARE(game.flagCount(), game.mineCount());
        for (int i = 0; i < game.cellCount() && game.status() != MineStatus::Won; ++i) {
            if (!game.cell(i).mine && !game.cell(i).revealed) {
                game.reveal(i);
            }
        }
        QCOMPARE(game.status(), MineStatus::Won);
        const MineSummary s = game.summary();
        QVERIFY(s.won);
        QVERIFY(s.perfect);
    }
}

void TestMinesweeper::gradeIsDerivedFromOutcomeAndProgress()
{
    MineSummary win;
    win.won = true;
    win.totalSafe = 71;
    win.revealedSafe = 71;
    QCOMPARE(mineGrade(win), MineGrade::Win);

    // 通关优先于进度（哪怕进度为 0，也不降级）
    win.revealedSafe = 1;
    QCOMPARE(mineGrade(win), MineGrade::Win);

    // 未通关：已翻开 >= 半数非雷格 → Draw；低于半数 → Lose
    MineSummary half;
    half.totalSafe = 71;
    half.revealedSafe = 36; // 36 * 2 >= 71
    QCOMPARE(mineGrade(half), MineGrade::Draw);

    MineSummary less;
    less.totalSafe = 71;
    less.revealedSafe = 35; // 35 * 2 < 71
    QCOMPARE(mineGrade(less), MineGrade::Lose);

    // 边界：无非雷格时不除零
    MineSummary empty;
    empty.totalSafe = 0;
    empty.revealedSafe = 0;
    QCOMPARE(mineGrade(empty), MineGrade::Lose);
}

void TestMinesweeper::chainPeakTracksSafeReveals()
{
    SystemRandom rng(99);
    Minesweeper game(&rng);
    game.newGame(MineConfig{9, 9, 10});
    const MineMove first = game.reveal(0);

    QVERIFY(!first.exploded);
    QVERIFY(first.revealed >= 1);
    QCOMPARE(first.chain, first.revealed); // 首步连翻 = 本次翻开数
    QCOMPARE(first.chainPeak, game.maxChain());
    QVERIFY(game.maxChain() >= first.revealed);
}

void TestMinesweeper::deterministicWithScriptedRandom()
{
    const std::vector<double> seq = {0.05, 0.15, 0.25, 0.35, 0.45, 0.55, 0.65, 0.75, 0.85, 0.95};

    ScriptedRandom ra(seq);
    Minesweeper a(&ra);
    a.newGame(MineConfig{9, 9, 10});
    a.reveal(0);

    ScriptedRandom rb(seq);
    Minesweeper b(&rb);
    b.newGame(MineConfig{9, 9, 10});
    b.reveal(0);

    for (int i = 0; i < a.cellCount(); ++i) {
        QCOMPARE(a.cell(i).mine, b.cell(i).mine);
        QCOMPARE(a.cell(i).adjacent, b.cell(i).adjacent);
    }
}

QTEST_GUILESS_MAIN(TestMinesweeper)
#include "test_minesweeper.moc"
