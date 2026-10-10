// 小游戏「接 Token」纯逻辑单测：
//   预设与常量 / 开新局与「首次输入才开局」/ 生成节奏与列冲突避让 / 下落与接取 /
//   漏接清零连击 / 漏接白饭无害 / 接到白饭立即结束（危险态与结束原因）/ 达成目标通关与 perfect /
//   时限结束与三档判定（Win / Draw / Lose）/ 接取区移动夹取 / 结算折算（MiniGameResult）/
//   随包台词与立绘自洽。
// 约定同其它 core 用例：零 Qt UI 依赖（QTEST_GUILESS_MAIN），随机源与帧推进全部可注入，
// 不真实等待。枚举比较统一走 static_cast<int>（与 test_minesweeper / test_chess 同口径）。
// 参考：docs/MINIGAME-INTERFACE.md §12。

#include "core/LineTable.h"
#include "core/PoseNames.h"
#include "core/TokenCatch.h"

#include <QtTest>

#include <QFile>
#include <QString>

#include <functional>
#include <string>

using namespace whalepet::core;

namespace {

// 固定随机序列：ScriptedRandom 的 nextInt(bound) = int(值 * bound) % bound、next01() 直接用值，
// 因此每两个值构成一次「生成」：(列、是不是白饭)。
//   {0.0, 0.9}  → 列 0、Token（0.9 ≥ 白饭概率）
//   {0.0, 0.05} → 列 0、白饭（0.05 < 0.20）
ScriptedRandom tokenAtColumnZero()
{
    return ScriptedRandom({0.0, 0.9});
}

ScriptedRandom riceAtColumnZero()
{
    return ScriptedRandom({0.0, 0.05});
}

// 推进直到 done() 为真（或本局结束）；返回 done() 是否成立。
bool tickUntil(TokenCatch &game, const std::function<bool()> &done, int limit = 2000)
{
    for (int i = 0; i < limit; ++i) {
        if (done()) {
            return true;
        }
        if (game.tick().ended) {
            break;
        }
    }
    return done();
}

int firstItemColumn(const TokenCatch &game)
{
    const std::vector<FallingItem> &items = game.items();
    return items.empty() ? -1 : items.front().column;
}

} // namespace

class TokenCatchTest : public QObject {
    Q_OBJECT
private slots:
    void presetsAndConstantsAreConsistent();
    void newGameIsReadyAndCentered();
    void tickBeforeStartDoesNothing();
    void tokenFallsAndIsCaught();
    void missedTokenResetsChain();
    void missedRiceIsHarmless();
    void riceEndsGameImmediately();
    void targetReachedWinsWithPerfect();
    void timeUpEndsWithGradeByProgress();
    void hardPresetFallsFasterAndIsExpert();
    void catcherMovementClampsAtEdges();
    void itemsNeverOverlapAndStopAtCatcherRow();
    void bundledLinesAndPoseAreConsistent();
};

void TokenCatchTest::presetsAndConstantsAreConsistent()
{
    QCOMPARE(kTokenCatchPresetCount, 3);
    QCOMPARE(kTokenCatchCols, 10);
    QCOMPARE(kTokenCatchRows, 14);
    QCOMPARE(kTokenCatchTickMs, 120);
    QCOMPARE(kTokenCatchCatcherWidth, 2);
    QVERIFY2(kTokenCatchCatcherWidth < kTokenCatchCols, "接取区宽度必须小于列数");
    QVERIFY2(kTokenCatchMinSpawnTicks >= 1, "最小生成间隔必须为正");

    for (int i = 0; i < kTokenCatchPresetCount; ++i) {
        const TokenCatchPresetDef &p = kTokenCatchPresets[i];
        QVERIFY2(p.id != nullptr && std::string(p.id).size() > 0, "难度 id 不能为空");
        QVERIFY2(p.name != nullptr && std::string(p.name).size() > 0, "难度名不能为空");
        QVERIFY2(p.spawnTicks >= kTokenCatchMinSpawnTicks, "生成间隔不得小于最小间隔");
        QVERIFY2(p.fallRows >= 1, "每帧至少下落 1 行");
        QVERIFY2(p.riceChance > 0.0 && p.riceChance < 1.0, "白饭概率必须在 (0,1) 区间");
        QVERIFY2(p.targetTokens > 0, "目标 Token 数必须为正");
        QVERIFY2(p.durationTicks > 0, "时限必须为正");
        for (int j = i + 1; j < kTokenCatchPresetCount; ++j) {
            QVERIFY2(std::string(p.id) != kTokenCatchPresets[j].id, "难度 id 必须唯一");
            QVERIFY2(std::string(p.name) != kTokenCatchPresets[j].name, "难度名必须唯一");
        }
    }

    QCOMPARE(QString::fromLatin1(tokenCatchPresetId(TokenCatchPreset::Easy)),
             QStringLiteral("easy"));
    QCOMPARE(QString::fromLatin1(tokenCatchPresetId(TokenCatchPreset::Hard)),
             QStringLiteral("hard"));
    // 具名文案是 UTF-8 字节串（与扫雷 / 找小猫的 preset 名同一口径），
    // 一律用 fromUtf8 解析，不得用 fromLatin1（否则中文变乱码）。
    QCOMPARE(QString::fromUtf8(tokenCatchPresetName(TokenCatchPreset::Easy)),
             QStringLiteral("初级"));
    QCOMPARE(tokenCatchPresetIndexOf(TokenCatchPreset::Normal), 1);
    QCOMPARE(static_cast<int>(tokenCatchPresetAt(2)),
             static_cast<int>(TokenCatchPreset::Hard));
    QCOMPARE(static_cast<int>(tokenCatchPresetAt(-1)),
             static_cast<int>(TokenCatchPreset::Normal)); // 越界回落中级
    QCOMPARE(static_cast<int>(tokenCatchPresetAt(99)),
             static_cast<int>(TokenCatchPreset::Normal));

    const std::string label = tokenCatchPresetLabel(TokenCatchPreset::Hard);
    QVERIFY2(label.find("30") != std::string::npos, "高级难度文案应含目标 30");
    QVERIFY2(label.find("60") != std::string::npos, "难度文案应含时限 60 秒");
}

void TokenCatchTest::newGameIsReadyAndCentered()
{
    TokenCatch game;
    game.newGame(TokenCatchPreset::Easy);

    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(TokenCatchStatus::Ready));
    QVERIFY(!game.started());
    QVERIFY(!game.ended());
    QCOMPARE(static_cast<int>(game.preset()), static_cast<int>(TokenCatchPreset::Easy));
    QVERIFY(game.items().empty());
    QCOMPARE(game.tokensCaught(), 0);
    QCOMPARE(game.tokensMissed(), 0);
    QCOMPARE(game.maxChain(), 0);
    QCOMPARE(game.level(), 0);
    QCOMPARE(game.targetTokens(), 12);
    QCOMPARE(game.ticks(), 0);
    QCOMPARE(game.elapsedMs(), 0);
    QCOMPARE(game.spawnIntervalTicks(), kTokenCatchPresets[0].spawnTicks);

    // 接取区居中，宽度 2
    QCOMPARE(game.catcherColumn(), (kTokenCatchCols - kTokenCatchCatcherWidth) / 2);
    QVERIFY(game.catcherCovers(game.catcherColumn()));
    QVERIFY(game.catcherCovers(game.catcherColumn() + 1));
    QVERIFY(!game.catcherCovers(game.catcherColumn() - 1));
    QVERIFY(!game.catcherCovers(game.catcherColumn() + kTokenCatchCatcherWidth));
}

void TokenCatchTest::tickBeforeStartDoesNothing()
{
    TokenCatch game;
    ScriptedRandom rng = tokenAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Easy);

    // Ready：推进幂等（宿主可能持有窗口期间反复调用）
    for (int i = 0; i < 50; ++i) {
        const TokenCatchTick step = game.tick();
        QVERIFY(!step.moved);
        QCOMPARE(step.spawned, 0);
    }
    QCOMPARE(game.ticks(), 0);
    QVERIFY2(game.items().empty(), "未开局不得生成下落物");

    QVERIFY(game.start());
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(TokenCatchStatus::Playing));
    QVERIFY(!game.start()); // 已开局：再调无变化
    QVERIFY(game.tick().moved);
    QCOMPARE(game.ticks(), 1);
}

void TokenCatchTest::tokenFallsAndIsCaught()
{
    TokenCatch game;
    ScriptedRandom rng = tokenAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Easy);
    QVERIFY(game.moveCatcherTo(0));
    QCOMPARE(game.catcherColumn(), 0);
    QVERIFY(game.start());

    // 初级生成间隔 14 帧：第 14 帧出现第一个下落物
    QVERIFY(tickUntil(game, [&game] { return !game.items().empty(); }, 20));
    QCOMPARE(game.ticks(), kTokenCatchPresets[0].spawnTicks);
    QCOMPARE(firstItemColumn(game), 0);
    QVERIFY(game.itemAt(0, 0));

    // 每帧下落 1 行（初级）
    QVERIFY(game.tick().moved);
    QVERIFY2(game.itemAt(0, 1), "下落物应逐行下移");
    QVERIFY2(!game.itemAt(0, 0), "下落物不应残留在原格");

    // 从第 0 行下落 13 帧后到达接取行 → 被接住
    QVERIFY(tickUntil(game, [&game] { return game.tokensCaught() > 0; }, 40));
    QVERIFY2(game.items().empty(), "接住后下落物应移除");
    QCOMPARE(game.tokensCaught(), 1);
    QCOMPARE(game.tokensMissed(), 0);
    QCOMPARE(game.chain(), 1);
    QCOMPARE(game.maxChain(), 1);
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(TokenCatchStatus::Playing));
    QVERIFY(game.elapsedMs() > 0);
    QCOMPARE(game.elapsedMs(), static_cast<std::int64_t>(game.ticks()) * kTokenCatchTickMs);
}

void TokenCatchTest::missedTokenResetsChain()
{
    TokenCatch game;
    ScriptedRandom rng = tokenAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Easy);
    QVERIFY(game.moveCatcherTo(0));
    QVERIFY(game.start());
    QVERIFY(tickUntil(game, [&game] { return game.tokensCaught() > 0; }, 40));
    QCOMPARE(game.chain(), 1);

    // 接取区移到最右（列 8/9），列 0 的 Token 将漏接
    QVERIFY(game.moveCatcherTo(kTokenCatchCols - 1));
    QCOMPARE(game.catcherColumn(), kTokenCatchCols - kTokenCatchCatcherWidth);
    QVERIFY(!game.catcherCovers(0));

    TokenCatchTick last;
    for (int i = 0; i < 60; ++i) {
        last = game.tick();
        if (last.missedTokens > 0) {
            break;
        }
    }
    QCOMPARE(last.missedTokens, 1);
    QCOMPARE(game.tokensMissed(), 1);
    QCOMPARE(game.chain(), 0);        // 漏接只清连击
    QCOMPARE(game.maxChain(), 1);     // 峰值保留
    QCOMPARE(game.tokensCaught(), 1); // 不扣分
}

void TokenCatchTest::missedRiceIsHarmless()
{
    TokenCatch game;
    ScriptedRandom rng = riceAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Easy);
    QVERIFY(game.moveCatcherTo(kTokenCatchCols - 1)); // 列 0 的白饭必然漏接
    QVERIFY(game.start());

    QVERIFY(tickUntil(game, [&game] { return !game.items().empty(); }, 20));
    QCOMPARE(static_cast<int>(game.items().front().kind), static_cast<int>(FallingKind::Rice));
    QCOMPARE(firstItemColumn(game), 0);

    QVERIFY(tickUntil(game, [&game] { return game.items().empty(); }, 40));
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(TokenCatchStatus::Playing));
    QCOMPARE(game.tokensCaught(), 0);
    QCOMPARE(game.tokensMissed(), 0); // 漏接白饭不计入漏接数
}

void TokenCatchTest::riceEndsGameImmediately()
{
    TokenCatch game;
    ScriptedRandom rng = riceAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Easy);
    QVERIFY(game.moveCatcherTo(0)); // 接取区覆盖列 0
    QVERIFY(game.start());

    QVERIFY(tickUntil(game, [&game] { return !game.items().empty(); }, 20));
    QCOMPARE(static_cast<int>(game.items().front().kind), static_cast<int>(FallingKind::Rice));
    QVERIFY2(!game.danger(), "刚生成的白饭不构成危险态");

    // 白饭进入最后 kTokenCatchDangerRows 行 → danger（供陪玩自描述使用）
    const int dangerThreshold = kTokenCatchRows - 1 - kTokenCatchDangerRows;
    QVERIFY(tickUntil(game, [&game] { return game.danger(); }, 40));
    QVERIFY2(game.items().front().row >= dangerThreshold, "危险态应在白饭逼近接取区时出现");

    QVERIFY(tickUntil(game, [&game] { return game.ended(); }, 40));

    QCOMPARE(static_cast<int>(game.endReason()), static_cast<int>(TokenCatchEnd::RiceCaught));
    QCOMPARE(static_cast<int>(game.status()), static_cast<int>(TokenCatchStatus::Ended));
    QVERIFY2(!game.won(), "接到白饭不得判定通关");
    QCOMPARE(game.tokensCaught(), 0);

    const TokenCatchSummary summary = game.summary();
    QCOMPARE(static_cast<int>(summary.end), static_cast<int>(TokenCatchEnd::RiceCaught));
    QVERIFY(!summary.won);
    QVERIFY2(!summary.perfect, "接到白饭不可能 perfect");
    QCOMPARE(summary.targetTokens, 12);
    QVERIFY(summary.tokensCaught * 2 < summary.targetTokens); // 0/12 → Lose 档

    const MiniGameResult result = tokenCatchGameResult(summary, game.preset(), game.elapsedMs());
    QCOMPARE(static_cast<int>(gameGrade(result)), static_cast<int>(GameGrade::Lose));
    QCOMPARE(QString::fromStdString(result.gameId), QStringLiteral("tokencatch"));
    QCOMPARE(QString::fromStdString(result.difficultyId), QStringLiteral("easy"));

    // 结束后推进幂等（不会把「下一帧」当成新局）
    const int ticksAtEnd = game.ticks();
    for (int i = 0; i < 10; ++i) {
        QVERIFY(!game.tick().moved);
    }
    QCOMPARE(game.ticks(), ticksAtEnd);
}

void TokenCatchTest::targetReachedWinsWithPerfect()
{
    TokenCatch game;
    ScriptedRandom rng = tokenAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Easy);
    QVERIFY(game.moveCatcherTo(0));
    QVERIFY(game.start());

    QVERIFY2(tickUntil(game, [&game] { return game.ended(); }, 2000), "应在时限内达成目标");
    QCOMPARE(static_cast<int>(game.endReason()),
             static_cast<int>(TokenCatchEnd::TargetReached));
    QVERIFY(game.won());
    QCOMPARE(game.tokensCaught(), 12);
    QCOMPARE(game.tokensMissed(), 0);
    QCOMPARE(game.chain(), 12);
    QCOMPARE(game.maxChain(), 12);
    QCOMPARE(game.level(), 12 / kTokenCatchTokensPerLevel);
    QCOMPARE(game.spawnIntervalTicks(), kTokenCatchPresets[0].spawnTicks - game.level());
    QVERIFY2(game.elapsedMs() < kTokenCatchPresets[0].durationTicks * kTokenCatchTickMs,
             "达成目标应早于时限");

    const TokenCatchSummary summary = game.summary();
    QVERIFY(summary.won);
    QVERIFY2(summary.perfect, "全程未漏接应判定 perfect");
    QCOMPARE(summary.maxChain, 12);

    const MiniGameResult result = tokenCatchGameResult(summary, game.preset(), game.elapsedMs());
    QCOMPARE(static_cast<int>(gameGrade(result)), static_cast<int>(GameGrade::Win));
    QVERIFY(result.won);
    QVERIFY(result.perfect);
    QVERIFY2(!result.expert, "初级难度不算高难档");
    QCOMPARE(result.maxChain, 12);
    QCOMPARE(result.progressDone, 12);
    QCOMPARE(result.progressTotal, 12);
    QCOMPARE(result.elapsedMs, game.elapsedMs());
}

void TokenCatchTest::timeUpEndsWithGradeByProgress()
{
    // A：一个都没接到 → 0/12 → Lose
    {
        TokenCatch game;
        ScriptedRandom rng = tokenAtColumnZero();
        game.setRandom(&rng);
        game.newGame(TokenCatchPreset::Easy);
        QVERIFY(game.moveCatcherTo(kTokenCatchCols - 1)); // 全程避开列 0
        QVERIFY(game.start());

        QVERIFY(tickUntil(game, [&game] { return game.ended(); }, 2000));
        QCOMPARE(static_cast<int>(game.endReason()), static_cast<int>(TokenCatchEnd::TimeUp));
        QVERIFY(!game.won());
        QCOMPARE(game.ticks(), kTokenCatchPresets[0].durationTicks);
        QCOMPARE(game.elapsedMs(), static_cast<std::int64_t>(kTokenCatchPresets[0].durationTicks)
                                       * kTokenCatchTickMs);
        QCOMPARE(game.remainingMs(), 0);

        const MiniGameResult result =
            tokenCatchGameResult(game.summary(), game.preset(), game.elapsedMs());
        QCOMPARE(static_cast<int>(gameGrade(result)), static_cast<int>(GameGrade::Lose));
    }

    // B：接到 6 个后全程躲开 → 6/12（进度过半）→ Draw
    {
        TokenCatch game;
        ScriptedRandom rng = tokenAtColumnZero();
        game.setRandom(&rng);
        game.newGame(TokenCatchPreset::Easy);
        QVERIFY(game.moveCatcherTo(0));
        QVERIFY(game.start());

        for (int want = 1; want <= 6; ++want) {
            QVERIFY2(tickUntil(game, [&game, want] { return game.tokensCaught() >= want; }, 2000),
                     "应能接到前 6 个 Token");
        }
        QCOMPARE(game.tokensCaught(), 6);
        QVERIFY(game.moveCatcherTo(kTokenCatchCols - 1)); // 剩下的全部放弃

        QVERIFY(tickUntil(game, [&game] { return game.ended(); }, 2000));
        QCOMPARE(static_cast<int>(game.endReason()), static_cast<int>(TokenCatchEnd::TimeUp));
        QCOMPARE(game.tokensCaught(), 6);
        QVERIFY(game.tokensMissed() > 0);

        const TokenCatchSummary summary = game.summary();
        QVERIFY(!summary.won);
        QVERIFY(!summary.perfect);
        const MiniGameResult result =
            tokenCatchGameResult(summary, game.preset(), game.elapsedMs());
        QCOMPARE(static_cast<int>(gameGrade(result)), static_cast<int>(GameGrade::Draw));
        QVERIFY(result.progressDone * 2 >= result.progressTotal);
    }
}

void TokenCatchTest::hardPresetFallsFasterAndIsExpert()
{
    TokenCatch game;
    ScriptedRandom rng = tokenAtColumnZero();
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Hard);
    QVERIFY(game.start());

    // 高级每帧下落 2 行：生成后的下一帧应在第 2 行
    QVERIFY(tickUntil(game, [&game] { return !game.items().empty(); }, 20));
    QVERIFY(game.itemAt(0, 0));
    QVERIFY(game.tick().moved);
    QVERIFY2(game.itemAt(0, 2), "高级难度每帧应下落 2 行");

    const MiniGameResult result = tokenCatchGameResult(game.summary(), TokenCatchPreset::Hard, 0);
    QVERIFY2(result.expert, "高级难度应标记 expert");
    QCOMPARE(QString::fromStdString(result.difficultyId), QStringLiteral("hard"));
    QCOMPARE(result.progressTotal, 30);
}

void TokenCatchTest::catcherMovementClampsAtEdges()
{
    TokenCatch game;
    game.newGame(TokenCatchPreset::Normal);

    QVERIFY(game.moveCatcherTo(0));
    QCOMPARE(game.catcherColumn(), 0);
    QVERIFY2(!game.moveCatcher(-1), "已在最左：不应发生位移");
    QCOMPARE(game.catcherColumn(), 0);

    QVERIFY(game.moveCatcher(1));
    QCOMPARE(game.catcherColumn(), 1);
    QVERIFY2(!game.moveCatcher(0), "零位移应返回 false");

    const int maxColumn = kTokenCatchCols - kTokenCatchCatcherWidth;
    QVERIFY(game.moveCatcherTo(kTokenCatchCols - 1)); // 点到最右列 → 夹取到 maxColumn
    QCOMPARE(game.catcherColumn(), maxColumn);
    QVERIFY2(!game.moveCatcher(5), "已在最右：不应发生位移");
    QCOMPARE(game.catcherColumn(), maxColumn);

    QVERIFY(game.moveCatcherTo(0));
    QCOMPARE(game.catcherColumn(), 0);
    QVERIFY2(!game.moveCatcherTo(-999), "越界点击（已夹取到最左）不产生位移");
    QVERIFY(game.moveCatcherTo(999)); // 越界点击同样夹取到最右
    QCOMPARE(game.catcherColumn(), maxColumn);
    QVERIFY2(!game.moveCatcherTo(999), "已是最右：再次越界点击不产生位移");
}

void TokenCatchTest::itemsNeverOverlapAndStopAtCatcherRow()
{
    // 真随机（固定种子）下跑长局，校验「同格不重叠 / 不出网格 / 不落在接取行」的硬不变量
    TokenCatch game;
    SystemRandom rng(20261009u);
    game.setRandom(&rng);
    game.newGame(TokenCatchPreset::Hard);
    QVERIFY(game.start());

    int ticksRun = 0;
    bool finished = false;
    for (int i = 0; i < 600 && !finished; ++i) {
        const TokenCatchTick step = game.tick();
        ++ticksRun;
        const std::vector<FallingItem> &items = game.items();
        for (std::size_t a = 0; a < items.size(); ++a) {
            QVERIFY2(items[a].column >= 0 && items[a].column < kTokenCatchCols, "下落物列越界");
            QVERIFY2(items[a].row >= 0 && items[a].row <= kTokenCatchRows - 2,
                     "下落物不得停在接取行（到达即判定）");
            for (std::size_t b = a + 1; b < items.size(); ++b) {
                QVERIFY2(!(items[a].column == items[b].column && items[a].row == items[b].row),
                         "两个下落物不得占用同一格");
            }
        }
        finished = step.ended;
    }
    QVERIFY2(ticksRun > 0, "至少推进一帧");
}

void TokenCatchTest::bundledLinesAndPoseAreConsistent()
{
    // 结束立绘：key 必须真实存在于 core::kPoses（即资源 dsh-whale-state-daily-picnic.webp）
    const std::string ricePose = kTokenCatchRicePose;
    QCOMPARE(QString::fromStdString(ricePose), QStringLiteral("daily-picnic"));
    bool poseFound = false;
    for (int i = 0; i < kPoseCount; ++i) {
        if (ricePose == kPoses[i].key) {
            poseFound = true;
            QCOMPARE(QString::fromLatin1(kPoses[i].file),
                     QStringLiteral("dsh-whale-state-daily-picnic"));
            break;
        }
    }
    QVERIFY2(poseFound, "结束立绘 key 未在 core::kPoses 中登记");

#ifdef WHALEPET_LINES_DIR
    QFile file(QString::fromLatin1(WHALEPET_LINES_DIR) + QStringLiteral("/tokencatch.txt"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("无法打开台词文件: %1").arg(file.fileName())));
    LineTable lines;
    QVERIFY(lines.loadFromText(file.readAll().toStdString()) > 0);

    const char *const kScenes[] = {
        kTokenCatchSceneStart, kTokenCatchSceneChain, kTokenCatchSceneLevelUp,
        kTokenCatchSceneRice,  kTokenCatchSceneWin,   kTokenCatchSceneTimeUp,
    };
    for (const char *scene : kScenes) {
        QVERIFY2(lines.hasScene(scene),
                 qPrintable(QStringLiteral("台词库缺少场景: %1").arg(QString::fromLatin1(scene))));
        QVERIFY2(!lines.pick(scene, nullptr).empty(),
                 qPrintable(QString::fromLatin1("场景无候选台词: ") + QString::fromLatin1(scene)));
    }
#else
    QSKIP("未定义 WHALEPET_LINES_DIR，跳过台词一致性检查");
#endif
}

QTEST_GUILESS_MAIN(TokenCatchTest)
#include "test_tokencatch.moc"
