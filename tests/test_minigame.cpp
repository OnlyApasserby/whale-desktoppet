// 小游戏结算服务单测：档位奖励数值 / 每日 3 局上限 / 个人最快与跨天清零 / 落库往返 /
// 按「游戏 + 难度」分桶 / 旧版纪录键迁移。
// 约定同 test_content.cpp：内存库（Database::openMemory）跑服务，headless（QTEST_GUILESS_MAIN）。
// 参考：docs/MINIGAME-INTERFACE.md；数值与上限照搬参考项目 dsh-whale-moe.js settleGame
// 与 whale-moe-core.js applyGrowth(game-*) / GAME.REWARDS_PER_DAY。
//
// 重构说明：结算服务已泛化为只接受通用契约 core::MiniGameResult（插件化小游戏的统一出口），
// 因此本测试用 core::mineGameResult(...) 把扫雷对局折算为通用结果后再结算——
// 断言强度与重构前保持一致，并额外覆盖「不同游戏 / 难度纪录互不干扰」与旧键迁移。

#include "core/GrowthRules.h"
#include "core/MiniGameTypes.h"
#include "core/Minesweeper.h"
#include "model/Database.h"
#include "viewmodel/MiniGameService.h"

#include <QDateTime>
#include <QTime>
#include <QtTest>

#include <memory>

using namespace whalepet;
using namespace whalepet::core;

namespace {

constexpr qint64 kDay = 86400000LL;
const char *const kMineGame = "minesweeper";

std::unique_ptr<model::Database> makeDb()
{
    auto db = std::make_unique<model::Database>();
    const bool ok = db->openMemory();
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return db;
}

// 今天本地 hour 点的毫秒（让「跨天」判定可控、结果与运行时刻无关）
qint64 msAtLocalHour(int hour)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime at(now.date(), QTime(hour, 0, 0));
    return at.toMSecsSinceEpoch();
}

MineSummary winSummary(int maxChain = 8)
{
    MineSummary s;
    s.won = true;
    s.perfect = false;
    s.maxChain = maxChain;
    s.mineCount = 10;
    s.revealedSafe = 71;
    s.totalSafe = 71;
    return s;
}

// 失败盘面：revealedSafe / totalSafe 决定档位（>= 半数 → Draw，否则 Lose）
MineSummary loseSummary(int revealedSafe)
{
    MineSummary s;
    s.won = false;
    s.perfect = false;
    s.maxChain = 3;
    s.mineCount = 10;
    s.revealedSafe = revealedSafe;
    s.totalSafe = 71;
    return s;
}

// 扫雷一局 → 通用契约（与 View 层 finishGame 的折算口径一致）
MiniGameResult mineResult(const MineSummary &s, MinePreset preset, qint64 elapsedMs)
{
    return mineGameResult(s, static_cast<int>(preset), elapsedMs);
}

// 任意玩法的通用结果（用于验证服务与具体玩法解耦）
MiniGameResult genericResult(const std::string &gameId, const std::string &difficultyId, bool won,
                            int done, int total, qint64 elapsedMs)
{
    MiniGameResult r;
    r.gameId = gameId;
    r.difficultyId = difficultyId;
    r.won = won;
    r.progressDone = done;
    r.progressTotal = total;
    r.elapsedMs = elapsedMs;
    return r;
}

} // namespace

class TestMiniGame : public QObject {
    Q_OBJECT

private slots:
    void winRewardsMatchReference();
    void drawAndLoseRewardsMatchReference();
    void dailyLimitCapsRewardsAtThree();
    void newRecordTracksPerDifficulty();
    void recordsAreIsolatedPerGameAndDifficulty();
    void crossDayResetsRewardCounter();
    void statePersistsAcrossReopen();
    void legacyRecordKeysAreMigrated();
};

void TestMiniGame::winRewardsMatchReference()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    const viewmodel::MiniGameReward r =
        svc.settle(mineResult(winSummary(), MinePreset::Beginner, 30000), noon);
    QCOMPARE(r.grade, GameGrade::Win);
    QVERIFY(r.rewarded);
    QVERIFY(r.newRecord);                                            // 首胜即个人纪录
    QCOMPARE(r.mood, kGameWinMood);                                  // +8
    QCOMPARE(r.affinity, kGameWinAffinity + kGameHighScoreAffinity); // +12，外加纪录 +5
    QCOMPARE(r.rewardsUsedToday, 1);
    QCOMPARE(r.bestMs, 30000);

    // 非通关不发「纪录」加成
    const viewmodel::MiniGameReward draw =
        svc.settle(mineResult(loseSummary(40), MinePreset::Beginner, 20000), noon);
    QVERIFY(!draw.newRecord);
}

void TestMiniGame::drawAndLoseRewardsMatchReference()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    // 进度过半（>= 半数非雷格）→ 及格档
    const viewmodel::MiniGameReward draw =
        svc.settle(mineResult(loseSummary(40), MinePreset::Beginner, 20000), noon);
    QCOMPARE(draw.grade, GameGrade::Draw);
    QVERIFY(draw.rewarded);
    QVERIFY(!draw.newRecord);
    QCOMPARE(draw.mood, kGameDrawMood);
    QCOMPARE(draw.affinity, kGameDrawAffinity);

    // 进度不足 → 失败档（心情扣减）
    const viewmodel::MiniGameReward lose =
        svc.settle(mineResult(loseSummary(10), MinePreset::Beginner, 8000), noon);
    QCOMPARE(lose.grade, GameGrade::Lose);
    QVERIFY(lose.rewarded);
    QCOMPARE(lose.mood, kGameLoseMood); // -3
    QCOMPARE(lose.affinity, 0);
}

void TestMiniGame::dailyLimitCapsRewardsAtThree()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    QCOMPARE(viewmodel::MiniGameService::rewardLimit(), kGameRewardsPerDay);
    QCOMPARE(kGameRewardsPerDay, 3);

    for (int i = 0; i < kGameRewardsPerDay; ++i) {
        const viewmodel::MiniGameReward r =
            svc.settle(mineResult(loseSummary(10), MinePreset::Beginner, 5000), noon);
        QVERIFY(r.rewarded);
        QCOMPARE(r.rewardsUsedToday, i + 1);
    }

    // 第 4 局：只计分不发奖；个人纪录仍会更新（与参考项目一致）
    const viewmodel::MiniGameReward fourth =
        svc.settle(mineResult(winSummary(), MinePreset::Beginner, 25000), noon);
    QVERIFY(!fourth.rewarded);
    QCOMPARE(fourth.mood, 0);
    QCOMPARE(fourth.affinity, 0);
    QCOMPARE(fourth.grade, GameGrade::Win);
    QVERIFY(fourth.newRecord);
    QCOMPARE(fourth.rewardsUsedToday, kGameRewardsPerDay + 1);
}

void TestMiniGame::newRecordTracksPerDifficulty()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    // 首个纪录
    QVERIFY(svc.settle(mineResult(winSummary(), MinePreset::Beginner, 40000), noon).newRecord);
    const viewmodel::MiniGameReward faster =
        svc.settle(mineResult(winSummary(), MinePreset::Beginner, 30000), noon);
    QVERIFY(faster.newRecord);
    QCOMPARE(faster.bestMs, 30000);

    const viewmodel::MiniGameReward slower =
        svc.settle(mineResult(winSummary(), MinePreset::Beginner, 50000), noon);
    QVERIFY(!slower.newRecord); // 更慢不刷新
    QCOMPARE(slower.bestMs, 30000);

    // 各难度独立记录：中级首胜也算纪录
    QVERIFY(svc.settle(mineResult(winSummary(), MinePreset::Intermediate, 90000), noon).newRecord);
    QCOMPARE(svc.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("beginner")), 30000);
    QCOMPARE(svc.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("intermediate")), 90000);
}

void TestMiniGame::recordsAreIsolatedPerGameAndDifficulty()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    // 与玩法无关：任意插件只要上报通用结果，即获得同一套纪录逻辑
    QVERIFY(svc.settle(genericResult("bubble", "easy", true, 10, 10, 20000), noon).newRecord);
    QVERIFY(svc.settle(genericResult("minesweeper", "beginner", true, 71, 71, 30000), noon)
                .newRecord);

    QCOMPARE(svc.bestMs(QStringLiteral("bubble"), QStringLiteral("easy")), 20000);
    QCOMPARE(svc.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("beginner")), 30000);

    // 不同游戏的纪录互不干扰
    QVERIFY(!svc.settle(genericResult("bubble", "easy", true, 10, 10, 25000), noon).newRecord);
    QVERIFY(svc.settle(genericResult("bubble", "easy", true, 10, 10, 15000), noon).newRecord);
    QCOMPARE(svc.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("beginner")), 30000);
}

void TestMiniGame::crossDayResetsRewardCounter()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    for (int i = 0; i < kGameRewardsPerDay; ++i) {
        svc.settle(mineResult(loseSummary(10), MinePreset::Beginner, 5000), noon);
    }
    QCOMPARE(svc.rewardsUsedToday(), kGameRewardsPerDay);

    // 跨天：奖励局数清零（个人最快不受影响）
    const qint64 nextDay = noon + kDay;
    const viewmodel::MiniGameReward r =
        svc.settle(mineResult(loseSummary(10), MinePreset::Beginner, 5000), nextDay);
    QVERIFY(r.rewarded);
    QCOMPARE(r.rewardsUsedToday, 1);
    QCOMPARE(svc.rewardsUsedToday(), 1);
}

void TestMiniGame::statePersistsAcrossReopen()
{
    auto db = makeDb();
    const qint64 noon = msAtLocalHour(12);
    {
        viewmodel::MiniGameService svc(db.get());
        svc.load();
        svc.settle(mineResult(winSummary(), MinePreset::Beginner, 30000), noon);
        svc.settle(mineResult(loseSummary(10), MinePreset::Beginner, 5000), noon);
        QCOMPARE(svc.rewardsUsedToday(), 2);
    }
    {
        viewmodel::MiniGameService reopened(db.get());
        reopened.load();
        QCOMPARE(reopened.rewardsUsedToday(), 2); // 同日保留
        QCOMPARE(reopened.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("beginner")),
                 30000);
    }
}

void TestMiniGame::legacyRecordKeysAreMigrated()
{
    auto db = makeDb();
    const qint64 noon = msAtLocalHour(12);

    {
        // 模拟 v0.2.0 的旧键：game.best_ms_0 == 初级个人最快
        viewmodel::MiniGameService svc(db.get());
        svc.load();
        db->setMeta(QStringLiteral("game.best_ms_0"), QStringLiteral("42000"));
        svc.adoptLegacyBest(QString::fromLatin1(kMineGame), QStringLiteral("beginner"),
                            QStringLiteral("0"));
        QCOMPARE(svc.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("beginner")), 42000);

        // 更慢的一局不刷新迁移后的纪录
        const viewmodel::MiniGameReward slower =
            svc.settle(mineResult(winSummary(), MinePreset::Beginner, 50000), noon);
        QVERIFY(!slower.newRecord);
        QCOMPARE(slower.bestMs, 42000);
    }

    // 迁移结果已落库：重新打开仍在（跨会话保留）
    viewmodel::MiniGameService reopened(db.get());
    reopened.load();
    QCOMPARE(reopened.bestMs(QString::fromLatin1(kMineGame), QStringLiteral("beginner")), 42000);
}

QTEST_GUILESS_MAIN(TestMiniGame)
#include "test_minigame.moc"
