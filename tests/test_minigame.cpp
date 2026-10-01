// 小游戏（扫雷）结算服务单测：档位奖励数值 / 每日 3 局上限 / 个人最快与跨天清零 / 落库往返。
// 约定同 test_content.cpp：内存库（Database::openMemory）跑服务，headless（QTEST_GUILESS_MAIN）。
// 参考：docs/MINIGAME-INTERFACE.md；数值与上限照搬参考项目 dsh-whale-moe.js settleGame
// 与 whale-moe-core.js applyGrowth(game-*) / GAME.REWARDS_PER_DAY。

#include "core/GrowthRules.h"
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

} // namespace

class TestMiniGame : public QObject {
    Q_OBJECT

private slots:
    void winRewardsMatchReference();
    void drawAndLoseRewardsMatchReference();
    void dailyLimitCapsRewardsAtThree();
    void newRecordTracksPerPresetBest();
    void crossDayResetsRewardCounter();
    void statePersistsAcrossReopen();
};

void TestMiniGame::winRewardsMatchReference()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    const viewmodel::MiniGameReward r = svc.settle(winSummary(), int(MinePreset::Beginner), 30000, noon);
    QCOMPARE(r.grade, MineGrade::Win);
    QVERIFY(r.rewarded);
    QVERIFY(r.newRecord);                                            // 首胜即个人纪录
    QCOMPARE(r.mood, kGameWinMood);                                  // +8
    QCOMPARE(r.affinity, kGameWinAffinity + kGameHighScoreAffinity); // +12，外加纪录 +5
    QCOMPARE(r.rewardsUsedToday, 1);
    QCOMPARE(r.bestMs, 30000);

    // 非通关不发「纪录」加成
    const viewmodel::MiniGameReward draw = svc.settle(loseSummary(40), int(MinePreset::Beginner), 20000, noon);
    QVERIFY(!draw.newRecord);
}

void TestMiniGame::drawAndLoseRewardsMatchReference()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    // 进度过半（>= 半数非雷格）→ 及格档
    const viewmodel::MiniGameReward draw = svc.settle(loseSummary(40), 0, 20000, noon);
    QCOMPARE(draw.grade, MineGrade::Draw);
    QVERIFY(draw.rewarded);
    QVERIFY(!draw.newRecord);
    QCOMPARE(draw.mood, kGameDrawMood);
    QCOMPARE(draw.affinity, kGameDrawAffinity);

    // 进度不足 → 失败档（心情扣减）
    const viewmodel::MiniGameReward lose = svc.settle(loseSummary(10), 0, 8000, noon);
    QCOMPARE(lose.grade, MineGrade::Lose);
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
        const viewmodel::MiniGameReward r = svc.settle(loseSummary(10), 0, 5000, noon);
        QVERIFY(r.rewarded);
        QCOMPARE(r.rewardsUsedToday, i + 1);
    }

    // 第 4 局：只计分不发奖；个人纪录仍会更新（与参考项目一致）
    const viewmodel::MiniGameReward fourth = svc.settle(winSummary(), 0, 25000, noon);
    QVERIFY(!fourth.rewarded);
    QCOMPARE(fourth.mood, 0);
    QCOMPARE(fourth.affinity, 0);
    QCOMPARE(fourth.grade, MineGrade::Win);
    QVERIFY(fourth.newRecord);
    QCOMPARE(fourth.rewardsUsedToday, kGameRewardsPerDay + 1);
}

void TestMiniGame::newRecordTracksPerPresetBest()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    const int beginner = int(MinePreset::Beginner);
    const int intermediate = int(MinePreset::Intermediate);

    QVERIFY(svc.settle(winSummary(), beginner, 40000, noon).newRecord); // 首个纪录
    const viewmodel::MiniGameReward faster = svc.settle(winSummary(), beginner, 30000, noon);
    QVERIFY(faster.newRecord);
    QCOMPARE(faster.bestMs, 30000);

    const viewmodel::MiniGameReward slower = svc.settle(winSummary(), beginner, 50000, noon);
    QVERIFY(!slower.newRecord); // 更慢不刷新
    QCOMPARE(slower.bestMs, 30000);

    // 各难度独立记录：中级首胜也算纪录
    QVERIFY(svc.settle(winSummary(), intermediate, 90000, noon).newRecord);
    QCOMPARE(svc.bestMs(beginner), 30000);
    QCOMPARE(svc.bestMs(intermediate), 90000);
}

void TestMiniGame::crossDayResetsRewardCounter()
{
    auto db = makeDb();
    viewmodel::MiniGameService svc(db.get());
    svc.load();
    const qint64 noon = msAtLocalHour(12);

    for (int i = 0; i < kGameRewardsPerDay; ++i) {
        svc.settle(loseSummary(10), 0, 5000, noon);
    }
    QCOMPARE(svc.rewardsUsedToday(), kGameRewardsPerDay);

    // 跨天：奖励局数清零（个人最快不受影响）
    const qint64 nextDay = noon + kDay;
    const viewmodel::MiniGameReward r = svc.settle(loseSummary(10), 0, 5000, nextDay);
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
        svc.settle(winSummary(), int(MinePreset::Beginner), 30000, noon);
        svc.settle(loseSummary(10), 0, 5000, noon);
        QCOMPARE(svc.rewardsUsedToday(), 2);
    }
    {
        viewmodel::MiniGameService reopened(db.get());
        reopened.load();
        QCOMPARE(reopened.rewardsUsedToday(), 2); // 同日保留
        QCOMPARE(reopened.bestMs(int(MinePreset::Beginner)), 30000);
    }
}

QTEST_GUILESS_MAIN(TestMiniGame)
#include "test_minigame.moc"
