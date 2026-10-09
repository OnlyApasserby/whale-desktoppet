#include <QtTest>

#include <QCoreApplication>
#include <QList>

#include "core/GameSnapshot.h"
#include "core/GameState.h"
#include "core/MiniGameCompanion.h"
#include "minigame/MiniGameCompanionSource.h"
#include "viewmodel/GameCompanionService.h"
#include "viewmodel/IGameCompanionSource.h"
#include "viewmodel/MiniGameCompanionSource.h"

#include <memory>
#include <string>

// EX4 小游戏陪玩：
//   * 中立契约 core::GameSnapshot 与中立判定 core::MiniGameCompanion（mood / 里程碑边沿 / 滞回）；
//   * 陪玩侧**通用聚合** viewmodel::MiniGameCompanionSource；
//   * 关键性质：**新增小游戏只要实现 IMiniGameCompanionSource 即被接入**，
//     聚合侧与判定侧完全不含具体玩法分支（本文件用一个「全新游戏」替身验证）。

using whalepet::core::GameCompanionSample;
using whalepet::core::GameMilestoneSet;
using whalepet::core::GameMood;
using whalepet::core::GameSnapshot;
using whalepet::viewmodel::GameCompanionService;
using whalepet::viewmodel::IGameCompanionSource;
using whalepet::viewmodel::MiniGameCompanionSource;

namespace {

GameSnapshot snap(bool running, std::int64_t now, const char *id = "fake")
{
    GameSnapshot s;
    s.available = true;
    s.gameId = id;
    s.running = running;
    s.nowMs = now;
    return s;
}

// 「任意新小游戏」的自描述替身：本文件用它证明聚合侧与具体玩法无关。
class FakeGameSource : public whalepet::IMiniGameCompanionSource {
public:
    GameSnapshot snapshot;
    bool ok = true;
    mutable int calls = 0;

    bool companionSnapshot(GameSnapshot *out) const override
    {
        ++calls;
        if (!ok || out == nullptr) {
            return false;
        }
        *out = snapshot;
        return true;
    }
};

whalepet::viewmodel::MiniGameCompanionCandidates candidatesOf(QList<whalepet::IMiniGameCompanionSource *> list)
{
    return [list]() { return list; };
}

} // namespace

class MiniGameCompanionTest : public QObject {
    Q_OBJECT
private slots:
    // ---- 中立判定（core::MiniGameCompanion）----
    void candidateNeutralMoodRules();
    void evaluateAppliesConfidenceAndDwell();
    void evaluateUnknownTakesEffectImmediately();
    void milestonesDetectNeutralEdges();
    void milestonesSkipFirstRoundAndOffline();
    void snapshotFoldsToPipelineSample();

    // ---- 陪玩侧通用聚合（viewmodel::MiniGameCompanionSource）----
    void aggregatePicksFirstAvailableCandidate();
    void aggregateSkipsUnavailableAndNull();
    void aggregateFailsWhenNoCandidate();
    void aggregateAttachRequiresAccessor();
    void aggregateIsGameAgnosticForBrandNewGame();

    // ---- 编排集成（GameCompanionService 走中立快照通道）----
    void serviceUsesSnapshotChannelAndReportsNeutralMood();
    void serviceNeutralPathDetectsMilestonesAndDwell();
    void serviceStaysRunningWhenNoGameVisible();
};

void MiniGameCompanionTest::candidateNeutralMoodRules()
{
    using whalepet::core::miniGameCandidate;

    // 无读数 → Unknown（不占用陪玩态）
    GameSnapshot empty;
    empty.nowMs = 1000;
    QCOMPARE(static_cast<int>(miniGameCandidate(empty).mood), static_cast<int>(GameMood::Unknown));

    // 未开局（available 但未 running）→ Unknown
    GameSnapshot idle = snap(false, 1000);
    QCOMPARE(static_cast<int>(miniGameCandidate(idle).mood), static_cast<int>(GameMood::Unknown));

    // 进行中 → Normal（置信度高于阈值）
    const GameCompanionSample normal = miniGameCandidate(snap(true, 1000));
    QCOMPARE(static_cast<int>(normal.mood), static_cast<int>(GameMood::Normal));
    QVERIFY(normal.confidence >= whalepet::core::kGameCompanionMinConfidence);

    // 进行中 + danger → Danger
    GameSnapshot danger = snap(true, 1000);
    danger.danger = true;
    QCOMPARE(static_cast<int>(miniGameCandidate(danger).mood), static_cast<int>(GameMood::Danger));

    // 已结束 → Unknown（通关等事件由里程碑单发一次）
    GameSnapshot finished = snap(false, 1000);
    finished.finished = true;
    finished.won = true;
    QCOMPARE(static_cast<int>(miniGameCandidate(finished).mood), static_cast<int>(GameMood::Unknown));
}

void MiniGameCompanionTest::evaluateAppliesConfidenceAndDwell()
{
    using whalepet::core::miniGameEvaluate;

    const GameCompanionSample first = miniGameEvaluate(snap(true, 1000), GameCompanionSample());
    QCOMPARE(static_cast<int>(first.mood), static_cast<int>(GameMood::Normal));
    QCOMPARE(first.sinceMs, 1000);

    // 立即进入 danger：驻留未满 → 保持 Normal
    GameSnapshot danger = snap(true, 2000);
    danger.danger = true;
    const GameCompanionSample kept = miniGameEvaluate(danger, first);
    QCOMPARE(static_cast<int>(kept.mood), static_cast<int>(GameMood::Normal));
    QCOMPARE(kept.sinceMs, first.sinceMs);

    // 驻留满最短时长 → 切到 Danger
    GameSnapshot dangerLater = snap(true, 1000 + whalepet::core::kGameCompanionMinDwellMs);
    dangerLater.danger = true;
    const GameCompanionSample later = miniGameEvaluate(dangerLater, first);
    QCOMPARE(static_cast<int>(later.mood), static_cast<int>(GameMood::Danger));
}

void MiniGameCompanionTest::evaluateUnknownTakesEffectImmediately()
{
    using whalepet::core::miniGameEvaluate;

    GameCompanionSample prev;
    prev.mood = GameMood::Danger;
    prev.confidence = 0.95;
    prev.sinceMs = 100000;

    GameSnapshot empty;
    empty.nowMs = 100500;
    const GameCompanionSample next = miniGameEvaluate(empty, prev);
    QCOMPARE(static_cast<int>(next.mood), static_cast<int>(GameMood::Unknown));
    QCOMPARE(next.confidence, 0.0);
}

void MiniGameCompanionTest::milestonesDetectNeutralEdges()
{
    using whalepet::core::miniGameMilestones;

    // levelUp
    GameSnapshot up = snap(true, 2000);
    up.level = 2;
    const GameMilestoneSet upSet = miniGameMilestones(up, snap(true, 1000));
    QVERIFY(upSet.levelUp);
    QVERIFY(!upSet.danger);

    // danger / recovered
    GameSnapshot danger = snap(true, 2000);
    danger.danger = true;
    const GameMilestoneSet dangerSet = miniGameMilestones(danger, snap(true, 1000));
    QVERIFY(dangerSet.danger);
    QVERIFY(!dangerSet.recovered);

    GameSnapshot safe = snap(true, 2000);
    const GameMilestoneSet recoveredSet = miniGameMilestones(safe, danger);
    QVERIFY(recoveredSet.recovered);
    QVERIFY(!recoveredSet.danger);

    // clear：通关边沿（非通关 → 通关）
    GameSnapshot won = snap(false, 3000);
    won.finished = true;
    won.won = true;
    GameSnapshot playing = snap(true, 2000);
    const GameMilestoneSet clearSet = miniGameMilestones(won, playing);
    QVERIFY(clearSet.clear);
    QVERIFY(!clearSet.boss); // 小游戏无 BOSS 语义

    // 已通关再采样不得重复触发
    QVERIFY(!miniGameMilestones(won, won).clear);
}

void MiniGameCompanionTest::milestonesSkipFirstRoundAndOffline()
{
    using whalepet::core::miniGameMilestones;

    GameSnapshot playing = snap(true, 1000);
    GameSnapshot offline; // available=false
    QVERIFY(!miniGameMilestones(playing, offline).any());
    QVERIFY(!miniGameMilestones(offline, playing).any());
}

void MiniGameCompanionTest::snapshotFoldsToPipelineSample()
{
    GameSnapshot s = snap(true, 4321, "some-game");
    s.level = 3;
    const whalepet::core::GameSample sample = whalepet::core::gameSampleFromSnapshot(s);
    QVERIFY(sample.available);
    QCOMPARE(sample.level, 3);
    QCOMPARE(sample.specialScene, 0); // 小游戏无外部特殊场景
    QCOMPARE(sample.nowMs, static_cast<std::int64_t>(4321));
    QCOMPARE(QString::fromStdString(sample.mapName), QStringLiteral("some-game"));
}

void MiniGameCompanionTest::aggregatePicksFirstAvailableCandidate()
{
    FakeGameSource first;
    first.snapshot = snap(true, 0, "first");
    FakeGameSource second;
    second.snapshot = snap(true, 0, "second");

    QList<whalepet::IMiniGameCompanionSource *> list{&first, &second};
    MiniGameCompanionSource source(candidatesOf(list));
    QString error;
    QVERIFY(source.attach(&error));

    whalepet::core::GameSnapshot out;
    QVERIFY(source.readSnapshot(&out, &error));
    QCOMPARE(QString::fromStdString(out.gameId), QStringLiteral("first"));
    QVERIFY(out.nowMs > 0); // 聚合侧填充采样时刻
}

void MiniGameCompanionTest::aggregateSkipsUnavailableAndNull()
{
    FakeGameSource unavailable;
    unavailable.snapshot = snap(true, 0, "unavailable");
    unavailable.ok = false; // 自述「本帧无可用读数」
    FakeGameSource empty;   // available=false
    empty.snapshot = GameSnapshot();
    FakeGameSource good;
    good.snapshot = snap(true, 0, "good");

    QList<whalepet::IMiniGameCompanionSource *> list{nullptr, &unavailable, &empty, &good};
    MiniGameCompanionSource source(candidatesOf(list));
    QString error;
    QVERIFY(source.attach(&error));

    whalepet::core::GameSnapshot out;
    QVERIFY(source.readSnapshot(&out, &error));
    QCOMPARE(QString::fromStdString(out.gameId), QStringLiteral("good"));
}

void MiniGameCompanionTest::aggregateFailsWhenNoCandidate()
{
    // 没有可见小游戏：两个通道都必须如实返回 false（不伪造数据）
    MiniGameCompanionSource source(candidatesOf({}));
    QString error;
    QVERIFY(source.attach(&error));
    QVERIFY(source.attached()); // attach 成功（访问器有效）

    whalepet::core::GameSnapshot snapshot;
    QVERIFY(!source.readSnapshot(&snapshot, &error));
    whalepet::core::GameSample sample;
    QVERIFY(!source.read(&sample, &error));
}

void MiniGameCompanionTest::aggregateAttachRequiresAccessor()
{
    const whalepet::viewmodel::MiniGameCompanionCandidates none; // 空访问器
    MiniGameCompanionSource source(none);
    QString error;
    QVERIFY(!source.attach(&error));
    QVERIFY(!error.isEmpty());
}

void MiniGameCompanionTest::aggregateIsGameAgnosticForBrandNewGame()
{
    // 关键性质：一个「从未出现过的游戏」只要实现 IMiniGameCompanionSource，
    // 就被同一份通用聚合接入，聚合侧与判定侧均无任何改动。
    FakeGameSource brandNew;
    brandNew.snapshot = snap(true, 0, "brand-new-game-v2");
    brandNew.snapshot.progressDone = 4;
    brandNew.snapshot.progressTotal = 10;

    MiniGameCompanionSource source(candidatesOf({&brandNew}));
    QString error;
    QVERIFY(source.attach(&error));

    whalepet::core::GameSnapshot out;
    QVERIFY(source.readSnapshot(&out, &error));
    QCOMPARE(QString::fromStdString(out.gameId), QStringLiteral("brand-new-game-v2"));
    QCOMPARE(out.progressDone, 4);
    QCOMPARE(static_cast<int>(whalepet::core::miniGameCandidate(out).mood),
             static_cast<int>(GameMood::Normal));
}

void MiniGameCompanionTest::serviceUsesSnapshotChannelAndReportsNeutralMood()
{
    auto fake = std::make_shared<FakeGameSource>();
    fake->snapshot = snap(true, 0, "minesweeper");

    GameCompanionService service;
    service.setSourceFactory([fake](QString *) -> std::unique_ptr<IGameCompanionSource> {
        QList<whalepet::IMiniGameCompanionSource *> list{fake.get()};
        return std::make_unique<MiniGameCompanionSource>(candidatesOf(list));
    });

    QString error;
    QVERIFY2(service.start(&error), qPrintable(error));
    // 若服务走的是旧 read()（RPG 语义），GameSample 的血量为 0/0 → 置信度 0.5 不足以改变现状，
    // mood 会停在 Unknown；这里能到 Normal，即证明走的是中立快照通道。
    QTRY_VERIFY_WITH_TIMEOUT(service.sampleCount() > 0, 3000);
    QCOMPARE(static_cast<int>(service.current().mood), static_cast<int>(GameMood::Normal));
    QVERIFY(service.available());
    service.stop();
}

void MiniGameCompanionTest::serviceNeutralPathDetectsMilestonesAndDwell()
{
    GameCompanionService service;
    auto fake = std::make_shared<FakeGameSource>();
    fake->snapshot = snap(true, 0, "kitten");
    service.setSourceFactory([fake](QString *) -> std::unique_ptr<IGameCompanionSource> {
        QList<whalepet::IMiniGameCompanionSource *> list{fake.get()};
        return std::make_unique<MiniGameCompanionSource>(candidatesOf(list));
    });
    QString error;
    QVERIFY2(service.start(&error), qPrintable(error));

    GameMilestoneSet lastMilestones;
    connect(&service, &GameCompanionService::gameStateChanged, this,
            [&](const GameCompanionSample &, const GameMilestoneSet &ms, const whalepet::core::GameSample &,
                qint64) { lastMilestones = ms; });

    // 首轮不产生里程碑（避免「启动即播报」）
    service.onSnapshot(snap(true, 1000), 1000);
    QVERIFY(!lastMilestones.any());

    // 升级（level 增大）→ levelUp
    GameSnapshot levelUp = snap(true, 1200, "kitten");
    levelUp.level = 1;
    service.onSnapshot(levelUp, 1200);
    QVERIFY(lastMilestones.levelUp);

    // 通关边沿 → clear
    GameSnapshot won = snap(false, 2000, "kitten");
    won.finished = true;
    won.won = true;
    service.onSnapshot(won, 2000);
    QVERIFY(lastMilestones.clear);

    service.stop();
}

void MiniGameCompanionTest::serviceStaysRunningWhenNoGameVisible()
{
    // 没有可见小游戏：数据源如实返回「无快照」，服务不得因此停用（invalidated=false）。
    GameCompanionService service;
    service.setSourceFactory([](QString *) -> std::unique_ptr<IGameCompanionSource> {
        return std::make_unique<MiniGameCompanionSource>(
            whalepet::viewmodel::MiniGameCompanionCandidates([]() {
                return QList<whalepet::IMiniGameCompanionSource *>();
            }));
    });
    QString error;
    QVERIFY2(service.start(&error), qPrintable(error));
    QTest::qWait(500); // > 2 个采样周期
    QVERIFY(service.running());
    QVERIFY(!service.available()); // 无读数如实报 false
    service.stop();
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QCoreApplication app(argc, argv);
    MiniGameCompanionTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_minigame_companion.moc"
