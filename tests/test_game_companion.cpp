#include <QtTest>

#include <QCoreApplication>

#include "core/GameState.h"
#include "core/PetStateMachine.h"
#include "core/PoseCatalog.h"
#include "core/WorkState.h"
#include "gamestate/GameProfile.h"
#include "gamestate/IGameStateAdapter.h"
#include "viewmodel/GameCompanionService.h"

#include <memory>

// EX1.4 游戏陪玩：判定规则 + 状态机游戏态通道 + 采样服务编排
// （docs/ROADMAP-ex1.md §2.5/§2.9）。

using whalepet::core::Event;
using whalepet::core::Fx;
using whalepet::core::GameCompanionRules;
using whalepet::core::GameCompanionSample;
using whalepet::core::GameMilestoneSet;
using whalepet::core::GameMood;
using whalepet::core::GameSample;
using whalepet::core::GameSpecialScene;
using whalepet::core::PetStateMachine;
using whalepet::core::PoseResult;
using whalepet::core::WorkState;
using whalepet::gamestate::GameProfile;
using whalepet::gamestate::IGameStateAdapter;
using whalepet::viewmodel::GameCompanionService;

namespace {

GameSample sample(double hp, double hpMax, int level, std::int64_t nowMs)
{
    GameSample s;
    s.available = true;
    s.hp = hp;
    s.hpMax = hpMax;
    s.level = level;
    s.nowMs = nowMs;
    return s;
}

class FakeAdapter : public IGameStateAdapter {
public:
    bool readOk = true;
    bool invalid = false;
    bool attachedFlag = false;
    int reads = 0;
    GameSample sample;

    bool attach(const GameProfile &, QString *) override
    {
        attachedFlag = true;
        return true;
    }
    void detach() override { attachedFlag = false; }
    bool attached() const override { return attachedFlag; }
    bool read(GameSample *out, QString *) override
    {
        ++reads;
        if (!readOk) {
            return false;
        }
        *out = sample;
        out->available = true;
        return true;
    }
    bool invalidated() const override { return invalid; }
};

} // namespace

class GameCompanionTest : public QObject {
    Q_OBJECT
private slots:
    void moodAndSpecialSceneIdsRoundTrip();
    void candidateMapsHpToMood();
    void evaluateAppliesConfidenceAndDwell();
    void evaluateUnknownTakesEffectImmediately();
    void milestonesDetectEdges();
    void poseAndSceneMappingsStayInExistingAssets();
    void machineDrivesPoseAtLowestPriority();
    void machineDoesNotInterruptOneShotForGame();
    void machineMilestonesBroadcast();
    void machineSilentCompanionshipSuppressesSpeech();
    void unknownGameStateKeepsLegacyBehavior();
    void serviceStartStopAndReport();
    void serviceDetectsDangerAndMilestones();
    void serviceStopsWhenAdapterInvalidated();
    void serviceFactoryFailureIsReported();
};

void GameCompanionTest::moodAndSpecialSceneIdsRoundTrip()
{
    for (int i = 0; i <= static_cast<int>(GameMood::Danger); ++i) {
        const auto mood = static_cast<GameMood>(i);
        QCOMPARE(static_cast<int>(whalepet::core::gameMoodFromId(whalepet::core::gameMoodId(mood))), i);
    }
    QCOMPARE(static_cast<int>(whalepet::core::gameMoodFromId("no-such")),
             static_cast<int>(GameMood::Unknown));

    for (int i = 0; i <= static_cast<int>(GameSpecialScene::Dialogue); ++i) {
        const auto scene = static_cast<GameSpecialScene>(i);
        QCOMPARE(static_cast<int>(whalepet::core::gameSpecialSceneFromId(
                     whalepet::core::gameSpecialSceneId(scene))),
                 i);
    }
    QCOMPARE(static_cast<int>(whalepet::core::gameSpecialSceneFromId("no-such")),
             static_cast<int>(GameSpecialScene::None));

    QVERIFY(!whalepet::core::gameSpecialSceneIsSilent(0));
    QVERIFY(whalepet::core::gameSpecialSceneIsSilent(static_cast<int>(GameSpecialScene::Video)));
}

void GameCompanionTest::candidateMapsHpToMood()
{
    GameCompanionRules rules;

    GameSample empty;
    empty.nowMs = 1000;
    QVERIFY(empty.isEmpty());
    const GameCompanionSample none = rules.candidate(empty);
    QCOMPARE(static_cast<int>(none.mood), static_cast<int>(GameMood::Unknown));
    QCOMPARE(none.confidence, 0.0);

    const GameCompanionSample low = rules.candidate(sample(30.0, 0.0, 1, 1000));
    QCOMPARE(static_cast<int>(low.mood), static_cast<int>(GameMood::Normal));
    QVERIFY(low.confidence < whalepet::core::kGameCompanionMinConfidence);

    const GameCompanionSample danger = rules.candidate(sample(10.0, 100.0, 1, 1000));
    QCOMPARE(static_cast<int>(danger.mood), static_cast<int>(GameMood::Danger));
    QVERIFY(danger.confidence >= whalepet::core::kGameCompanionMinConfidence);

    QCOMPARE(static_cast<int>(rules.candidate(sample(80.0, 100.0, 1, 1000)).mood),
             static_cast<int>(GameMood::Normal));
    QCOMPARE(static_cast<int>(rules.candidate(sample(20.0, 100.0, 1, 1000)).mood),
             static_cast<int>(GameMood::Danger));
}

void GameCompanionTest::evaluateAppliesConfidenceAndDwell()
{
    GameCompanionRules rules;

    const GameCompanionSample first = rules.evaluate(sample(80.0, 100.0, 1, 1000), GameCompanionSample());
    QCOMPARE(static_cast<int>(first.mood), static_cast<int>(GameMood::Normal));
    QCOMPARE(first.sinceMs, 1000);

    const GameCompanionSample kept = rules.evaluate(sample(10.0, 100.0, 1, 2000), first);
    QCOMPARE(static_cast<int>(kept.mood), static_cast<int>(GameMood::Normal));
    QCOMPARE(kept.sinceMs, first.sinceMs);

    const GameCompanionSample later = rules.evaluate(
        sample(10.0, 100.0, 1, 1000 + whalepet::core::kGameCompanionMinDwellMs), first);
    QCOMPARE(static_cast<int>(later.mood), static_cast<int>(GameMood::Danger));

    GameCompanionSample prev;
    prev.mood = GameMood::Normal;
    prev.confidence = 0.9;
    prev.sinceMs = 0;
    const GameCompanionSample weak = rules.evaluate(sample(30.0, 0.0, 1, 500000), prev);
    QCOMPARE(static_cast<int>(weak.mood), static_cast<int>(GameMood::Normal));
}

void GameCompanionTest::evaluateUnknownTakesEffectImmediately()
{
    GameCompanionRules rules;
    GameCompanionSample prev;
    prev.mood = GameMood::Danger;
    prev.confidence = 0.95;
    prev.sinceMs = 100000;

    GameSample empty;
    empty.nowMs = 100500;
    const GameCompanionSample next = rules.evaluate(empty, prev);
    QCOMPARE(static_cast<int>(next.mood), static_cast<int>(GameMood::Unknown));
    QCOMPARE(next.confidence, 0.0);
}

void GameCompanionTest::milestonesDetectEdges()
{
    GameCompanionRules rules;

    GameSample offline;
    QVERIFY(!rules.milestones(sample(10.0, 100.0, 1, 1000), offline).any());
    QVERIFY(!rules.milestones(offline, sample(10.0, 100.0, 1, 1000)).any());

    const GameMilestoneSet up = rules.milestones(sample(50.0, 100.0, 2, 2000), sample(50.0, 100.0, 1, 1000));
    QVERIFY(up.levelUp);
    QVERIFY(!up.clear);

    const GameMilestoneSet danger = rules.milestones(sample(10.0, 100.0, 1, 2000), sample(80.0, 100.0, 1, 1000));
    QVERIFY(danger.danger);
    QVERIFY(!danger.recovered);

    const GameMilestoneSet recovered = rules.milestones(sample(95.0, 100.0, 1, 2000), sample(10.0, 100.0, 1, 1000));
    QVERIFY(recovered.recovered);
    QVERIFY(!recovered.danger);

    GameSample bossCur = sample(30.0, 300.0, 1, 2000);
    const GameMilestoneSet boss = rules.milestones(bossCur, sample(80.0, 100.0, 1, 1000));
    QVERIFY(boss.boss);
    QVERIFY(boss.danger);

    const GameMilestoneSet clear = rules.milestones(sample(95.0, 100.0, 2, 2000), sample(10.0, 100.0, 1, 1000));
    QVERIFY(clear.clear);
    QVERIFY(clear.levelUp);
    QVERIFY(clear.recovered);
}

void GameCompanionTest::poseAndSceneMappingsStayInExistingAssets()
{
    for (int i = 0; i <= static_cast<int>(GameMood::Danger); ++i) {
        const auto mood = static_cast<GameMood>(i);
        const char *pose = whalepet::core::gameMoodPose(mood);
        const char *scene = whalepet::core::gameMoodScene(mood);
        if (mood == GameMood::Unknown) {
            QVERIFY(pose == nullptr);
            QVERIFY(scene == nullptr);
            continue;
        }
        QVERIFY2(pose != nullptr, whalepet::core::gameMoodId(mood));
        QVERIFY2(whalepet::core::poseExists(pose), pose);
        QVERIFY2(scene != nullptr, whalepet::core::gameMoodId(mood));
        QVERIFY(QString::fromLatin1(scene).startsWith(QStringLiteral("game.")));
    }

    GameMilestoneSet set;
    set.levelUp = true;
    set.danger = true;
    set.boss = true;
    set.clear = true;
    QVERIFY(whalepet::core::poseExists(whalepet::core::gameMilestonePose(set)));
    QCOMPARE(QString::fromLatin1(whalepet::core::gameMilestoneScene(set)), QStringLiteral("game.clear"));
    QCOMPARE(QString::fromLatin1(whalepet::core::gameMilestonePose(set)), QStringLiteral("game-win"));

    GameMilestoneSet only;
    only.recovered = true;
    QCOMPARE(QString::fromLatin1(whalepet::core::gameMilestoneScene(only)), QStringLiteral("game.normal"));
}

void GameCompanionTest::machineDrivesPoseAtLowestPriority()
{
    PetStateMachine machine(nullptr);
    machine.reset(0);

    const PoseResult arrived =
        machine.handle(Event::gameStateChanged(static_cast<int>(GameMood::Normal), 0, 1000));
    QCOMPARE(QString::fromStdString(arrived.pose), QStringLiteral("game-happy"));
    QCOMPARE(static_cast<int>(machine.gameMood()), static_cast<int>(GameMood::Normal));

    machine.handle(Event::workStateChanged(static_cast<int>(WorkState::Coding), 2000));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(2100)).pose), QStringLiteral("work-ram"));

    machine.handle(Event::workStateChanged(static_cast<int>(WorkState::Unknown), 3000));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(3100)).pose), QStringLiteral("game-happy"));

    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(600000)).pose), QStringLiteral("afk"));
}

void GameCompanionTest::machineDoesNotInterruptOneShotForGame()
{
    PetStateMachine machine(nullptr);
    machine.reset(0);

    const PoseResult click = machine.handle(Event::click(whalepet::core::Zone::Head, 100));
    QCOMPARE(QString::fromStdString(click.pose), QStringLiteral("react-head"));
    QVERIFY(click.lineSerial != 0);

    const PoseResult during =
        machine.handle(Event::gameStateChanged(static_cast<int>(GameMood::Danger), 0, 200));
    QCOMPARE(QString::fromStdString(during.pose), QStringLiteral("react-head"));
    QCOMPARE(during.lineSerial, click.lineSerial);
    QCOMPARE(static_cast<int>(machine.gameMood()), static_cast<int>(GameMood::Danger));

    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(7000)).pose), QStringLiteral("meme-shock"));
}

void GameCompanionTest::machineMilestonesBroadcast()
{
    PetStateMachine machine(nullptr);
    machine.reset(0);

    GameMilestoneSet up;
    up.levelUp = true;
    const PoseResult r =
        machine.handle(Event::gameStateChanged(static_cast<int>(GameMood::Normal), 0, up, 1000));
    QCOMPARE(QString::fromStdString(r.pose), QStringLiteral("levelup"));
    QCOMPARE(QString::fromStdString(r.lineKey), QStringLiteral("game.levelup"));
    QVERIFY(r.lineSerial != 0);

    // 一次性姿态由 Tick 到期回收（与真实 200ms 采样同频），先推进一拍再送通关里程碑。
    machine.handle(Event::tick(5000));
    GameMilestoneSet clear;
    clear.clear = true;
    const PoseResult r2 =
        machine.handle(Event::gameStateChanged(static_cast<int>(GameMood::Normal), 0, clear, 8000));
    QCOMPARE(QString::fromStdString(r2.pose), QStringLiteral("game-win"));
    QCOMPARE(QString::fromStdString(r2.lineKey), QStringLiteral("game.clear"));
    QCOMPARE(static_cast<int>(r2.fx), static_cast<int>(Fx::Star));
}

void GameCompanionTest::machineSilentCompanionshipSuppressesSpeech()
{
    PetStateMachine machine(nullptr);
    machine.reset(0);

    const PoseResult in = machine.handle(Event::gameStateChanged(
        static_cast<int>(GameMood::Normal), static_cast<int>(GameSpecialScene::Dialogue), 1000));
    QCOMPARE(QString::fromStdString(in.pose), QStringLiteral("game-happy"));
    QVERIFY(machine.gameCompanionSilent());
    QCOMPARE(machine.gameSpecialScene(), static_cast<int>(GameSpecialScene::Dialogue));

    const PoseResult quiet = machine.speak("", "greet.slot1", 0, Event::tick(20000), true);
    QVERIFY2(quiet.lineKey.empty(), "静默陪伴期间不得主动说话");

    GameMilestoneSet clear;
    clear.clear = true;
    const PoseResult ms = machine.handle(Event::gameStateChanged(
        static_cast<int>(GameMood::Normal), static_cast<int>(GameSpecialScene::Dialogue), clear, 30000));
    QVERIFY(ms.lineKey.empty());

    machine.handle(Event::gameStateChanged(static_cast<int>(GameMood::Normal), 0, 40000));
    QVERIFY(!machine.gameCompanionSilent());
    QCOMPARE(machine.gameSpecialScene(), 0);
}

void GameCompanionTest::unknownGameStateKeepsLegacyBehavior()
{
    PetStateMachine machine(nullptr);
    machine.reset(0);
    QCOMPARE(static_cast<int>(machine.gameMood()), static_cast<int>(GameMood::Unknown));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(200)).pose), QStringLiteral("idle-cute"));

    machine.handle(Event::gameStateChanged(static_cast<int>(GameMood::Normal), 0, 1000));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(1100)).pose), QStringLiteral("game-happy"));

    const PoseResult cleared = machine.handle(Event::gameStateChanged(-1, 0, 2000));
    QVERIFY(cleared.lineKey.empty());
    QCOMPARE(static_cast<int>(machine.gameMood()), static_cast<int>(GameMood::Unknown));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(2100)).pose), QStringLiteral("idle-cute"));
}

void GameCompanionTest::serviceStartStopAndReport()
{
    GameCompanionService service;
    auto fake = std::make_shared<FakeAdapter>();
    fake->sample = sample(50.0, 100.0, 1, 0);
    service.setAdapterFactory([fake](const GameProfile &, QString *) -> std::unique_ptr<IGameStateAdapter> {
        return std::unique_ptr<IGameStateAdapter>(new FakeAdapter(*fake));
    });

    QVERIFY(!service.running());
    QVERIFY(!service.available());

    QString error;
    QVERIFY(service.start(GameProfile{}, &error));
    QVERIFY(service.running());

    int changes = 0;
    GameCompanionSample lastStable;
    connect(&service, &GameCompanionService::gameStateChanged, this,
            [&](const GameCompanionSample &stable, const GameMilestoneSet &, const GameSample &, qint64) {
                ++changes;
                lastStable = stable;
            });

    service.onSample(sample(50.0, 100.0, 1, 1000), 1000);
    QCOMPARE(changes, 1);
    QCOMPARE(static_cast<int>(lastStable.mood), static_cast<int>(GameMood::Normal));
    QCOMPARE(service.sampleCount(), 1);
    QCOMPARE(service.changeCount(), 1);

    service.onSample(sample(50.0, 100.0, 1, 1200), 1200);
    QCOMPARE(changes, 2);
    QCOMPARE(service.changeCount(), 1); // 持续态未变，不计变化

    service.stop();
    QVERIFY(!service.running());
}

void GameCompanionTest::serviceDetectsDangerAndMilestones()
{
    GameCompanionService service;
    auto fake = std::make_shared<FakeAdapter>();
    service.setAdapterFactory([fake](const GameProfile &, QString *) -> std::unique_ptr<IGameStateAdapter> {
        return std::unique_ptr<IGameStateAdapter>(new FakeAdapter(*fake));
    });
    QString error;
    QVERIFY(service.start(GameProfile{}, &error));

    GameMilestoneSet lastMilestones;
    connect(&service, &GameCompanionService::gameStateChanged, this,
            [&](const GameCompanionSample &, const GameMilestoneSet &ms, const GameSample &, qint64) {
                lastMilestones = ms;
            });

    service.onSample(sample(50.0, 100.0, 1, 1000), 1000);
    QVERIFY(!lastMilestones.any()); // 首轮不产生里程碑（避免「启动即播报」）

    service.onSample(sample(50.0, 100.0, 2, 1200), 1200);
    QVERIFY(lastMilestones.levelUp);

    service.onSample(sample(10.0, 100.0, 2, 10000), 10000);
    QCOMPARE(static_cast<int>(service.current().mood), static_cast<int>(GameMood::Danger));
    QVERIFY(lastMilestones.danger);

    service.stop();
}

void GameCompanionTest::serviceStopsWhenAdapterInvalidated()
{
    GameCompanionService service;
    auto fake = std::make_shared<FakeAdapter>();
    fake->readOk = false;
    fake->invalid = true;
    service.setAdapterFactory([fake](const GameProfile &, QString *) -> std::unique_ptr<IGameStateAdapter> {
        return std::unique_ptr<IGameStateAdapter>(new FakeAdapter(*fake));
    });

    int stopped = 0;
    connect(&service, &GameCompanionService::companionStopped, this, [&] { ++stopped; });

    QString error;
    QVERIFY(service.start(GameProfile{}, &error));
    QVERIFY(service.running());

    // 定时采样（200ms 档）读到连续失败 → 适配器失效 → 自动停用并通知上层
    QTRY_COMPARE_WITH_TIMEOUT(stopped, 1, 2000);
    QVERIFY(!service.running());
    QVERIFY(!service.available());
}

void GameCompanionTest::serviceFactoryFailureIsReported()
{
    GameCompanionService service;
    service.setAdapterFactory([](const GameProfile &, QString *error) -> std::unique_ptr<IGameStateAdapter> {
        if (error != nullptr) {
            *error = QStringLiteral("no adapter");
        }
        return nullptr;
    });

    QString error;
    QVERIFY(!service.start(GameProfile{}, &error));
    QVERIFY(!service.running());
    QCOMPARE(error, QStringLiteral("no adapter"));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QCoreApplication app(argc, argv);
    GameCompanionTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_game_companion.moc"
