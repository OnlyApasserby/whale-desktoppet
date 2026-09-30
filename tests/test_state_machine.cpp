#include <QtTest>

#include "core/PetStateMachine.h"
#include "core/PoseCatalog.h"
#include "core/PoseNames.h"

using namespace whalepet::core;
// 注意：using namespace 只引入成员，不引入命名空间名本身，
// 所以必须有别名才能在下面写 core::kPoseCount。
namespace core = whalepet::core;

namespace {
constexpr std::int64_t kBase = 1'000'000; // 任意基准时刻（ms）
}

class StateMachineTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 上下文态 ----
    void resetGoesIdle();
    void idleEscalatesWithTime();
    void nightIsSleep();

    // ---- 交互态 ----
    void clickByZone();
    void clickFallsBackWhenPoseMissing();
    void tripleClickGivesStarAndParticles();
    void dragStartAndEnd();
    void menuActions();

    // ---- 时序 ----
    void oneShotExpires();
    void speechThrottleAppliesToProactiveOnly();
    void nightSuppressesProactiveOnly();
    void serialsMarkOnlyNewOutcomes();

    // ---- 概率事件 ----
    void teaseTriggersWithLowRoll();
    void teaseSkippedWithHighRoll();

    // ---- 关键词 ----
    void keywordFallsBackSafely();

    // ---- 清单完整性 ----
    void catalogCoversAllPoses();
    void usedPosesExist();
};

void StateMachineTest::resetGoesIdle()
{
    ScriptedRandom rng({}); // 永不触发逗弄
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    QCOMPARE(QString::fromStdString(sm.current().pose), QStringLiteral("idle-cute"));
}

void StateMachineTest::idleEscalatesWithTime()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    const auto poseAt = [&](std::int64_t idleMs) {
        return QString::fromStdString(
            sm.handle(Event::tick(kBase + idleMs)).pose);
    };

    QCOMPARE(poseAt(kWaitingMs - 1), QStringLiteral("idle-cute"));
    QCOMPARE(poseAt(kWaitingMs), QStringLiteral("waiting"));
    QCOMPARE(poseAt(kThinkingMs), QStringLiteral("thinking"));
    QCOMPARE(poseAt(kAfkMs), QStringLiteral("afk"));
}

void StateMachineTest::nightIsSleep()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    QVERIFY(PetStateMachine::isNight(23));
    QVERIFY(PetStateMachine::isNight(0));
    QVERIFY(PetStateMachine::isNight(5));
    QVERIFY(!PetStateMachine::isNight(6));
    QVERIFY(!PetStateMachine::isNight(22));

    // 时段态优先级高于挂机态
    const PoseResult r = sm.handle(Event::clock(23, kBase));
    QCOMPARE(QString::fromStdString(r.pose), QStringLiteral("sleep"));
}

void StateMachineTest::clickByZone()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::Head, kBase)).pose),
             QStringLiteral("react-head"));
    QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::Belly, kBase + 10000)).pose),
             QStringLiteral("react-belly"));
    QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::Tail, kBase + 20000)).pose),
             QStringLiteral("react-tail"));
}

void StateMachineTest::clickFallsBackWhenPoseMissing()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    // Zone::None/ Body 走默认 curious
    QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::None, kBase)).pose),
             QStringLiteral("curious"));
}

void StateMachineTest::tripleClickGivesStarAndParticles()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    const PoseResult r = sm.handle(Event::simple(EventType::TripleClick, kBase));
    QCOMPARE(QString::fromStdString(r.pose), QStringLiteral("star"));
    QCOMPARE(static_cast<int>(r.fx), static_cast<int>(Fx::Particle));
    QCOMPARE(r.ttlMs, static_cast<int>(kSuccessWindowMs));
}

void StateMachineTest::dragStartAndEnd()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    const PoseResult start = sm.handle(Event::simple(EventType::DragStart, kBase));
    QCOMPARE(QString::fromStdString(start.pose), QStringLiteral("pick-up"));
    QVERIFY(sm.dragging());

    // 拖拽期间 Tick 不应把姿态冲掉
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + 5000)).pose),
             QStringLiteral("pick-up"));

    const PoseResult end = sm.handle(Event::simple(EventType::DragEnd, kBase + 6000));
    QVERIFY(!sm.dragging());
    QVERIFY(end.pose == "idle-cute" || end.pose == "waiting");
}

void StateMachineTest::menuActions()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    QCOMPARE(QString::fromStdString(sm.handle(Event::simple(EventType::Feed, kBase)).pose),
             QStringLiteral("eat"));
    QCOMPARE(QString::fromStdString(sm.handle(Event::simple(EventType::Tease, kBase + 10000)).pose),
             QStringLiteral("angry"));

    const PoseResult praise = sm.handle(Event::simple(EventType::Praise, kBase + 20000));
    QCOMPARE(QString::fromStdString(praise.pose), QStringLiteral("blush"));
    QCOMPARE(static_cast<int>(praise.fx), static_cast<int>(Fx::Heart));
}

void StateMachineTest::oneShotExpires()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    sm.handle(Event::click(Zone::Head, kBase));
    QCOMPARE(QString::fromStdString(sm.current().pose), QStringLiteral("react-head"));

    // 窗口内仍保持
    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::tick(kBase + kCuriousWindowMs - 1)).pose),
             QStringLiteral("react-head"));

    // 到期回落上下文态。注意此刻距上次输入只有 6s（< kWaitingMs=15s），
    // 因此落回的是 idle-cute 而不是 waiting。
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + kCuriousWindowMs)).pose),
             QStringLiteral("idle-cute"));

    // 继续推进：距上次输入满 15s → waiting
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + kWaitingMs)).pose),
             QStringLiteral("waiting"));
}

void StateMachineTest::speechThrottleAppliesToProactiveOnly()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    // 用户交互：间隔远小于 kSpeechGapMs，但每一击都必须有台词
    // （「连点不刷屏」由 Presenter 的序号去重 + 流式打断保证，不能靠吞掉后续操作）
    QVERIFY(!sm.handle(Event::click(Zone::Head, kBase)).lineKey.empty());
    QVERIFY(!sm.handle(Event::click(Zone::Belly, kBase + 1000)).lineKey.empty());
    QVERIFY(!sm.handle(Event::click(Zone::Tail, kBase + 2000)).lineKey.empty());

    // 主动说话：仍受 kSpeechGapMs 节流
    QVERIFY(!sm.handle(Event::simple(EventType::LevelUp, kBase + 3000)).lineKey.empty());
    QVERIFY(sm.handle(Event::simple(EventType::LevelUp, kBase + 3000 + kSpeechGapMs - 1))
                .lineKey.empty());
    QVERIFY(!sm.handle(Event::simple(EventType::AchievementUnlocked, kBase + 3000 + kSpeechGapMs))
                 .lineKey.empty());
}

void StateMachineTest::serialsMarkOnlyNewOutcomes()
{
    // 用永不触发逗弄的 RNG，让 Tick 只做上下文刷新（不产生新表现）
    ScriptedRandom rng({0.9});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    const PoseResult click = sm.handle(Event::click(Zone::Head, kBase));
    QVERIFY(click.lineSerial != 0);
    QCOMPARE(click.fxSerial, std::uint32_t(0)); // 单击没有特效

    // 同一缓存态被 Tick 重放：序号必须保持不变，
    // 否则 Presenter 会把「同一句台词」按 tick 反复重播（每 200ms 换一句）。
    const PoseResult replay1 = sm.handle(Event::tick(kBase + kTickMs));
    const PoseResult replay2 = sm.handle(Event::tick(kBase + kTickMs * 2));
    QCOMPARE(replay1.lineSerial, click.lineSerial);
    QCOMPARE(replay2.lineSerial, click.lineSerial);
    QCOMPARE(QString::fromStdString(replay2.pose), QString::fromStdString(click.pose));

    // 新事件 → 序号自增（Presenter 据此播放新的一次特效/台词）
    const PoseResult triple = sm.handle(Event::simple(EventType::TripleClick, kBase + 1000));
    QVERIFY(triple.fxSerial != 0);
    QCOMPARE(triple.fxSerial, replay2.fxSerial + 1);
    QVERIFY(triple.lineSerial > replay2.lineSerial);

    // 姿态到期回落 → 无新表现，序号保持（不播放、也不重复播放）
    const PoseResult fallback = sm.handle(Event::tick(kBase + kCuriousWindowMs + 1000));
    QCOMPARE(fallback.fxSerial, std::uint32_t(0));
    QCOMPARE(fallback.lineSerial, std::uint32_t(0));
    QVERIFY(fallback.lineKey.empty());
}

void StateMachineTest::nightSuppressesProactiveOnly()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    sm.handle(Event::clock(23, kBase)); // 进入深夜

    // 主动事件（升级）深夜静默
    const PoseResult lv = sm.handle(Event::simple(EventType::LevelUp, kBase + 10000));
    QCOMPARE(QString::fromStdString(lv.pose), QStringLiteral("levelup"));
    QVERIFY(lv.lineKey.empty());

    // 用户主动交互仍可回应
    const PoseResult click = sm.handle(Event::click(Zone::Head, kBase + 20000));
    QVERIFY(!click.lineKey.empty());
}

void StateMachineTest::teaseTriggersWithLowRoll()
{
    ScriptedRandom rng({0.001}); // < kTeaseChance
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    const PoseResult r = sm.handle(Event::tick(kBase + 1000));
    QCOMPARE(QString::fromStdString(r.pose), QStringLiteral("teasing"));
}

void StateMachineTest::teaseSkippedWithHighRoll()
{
    ScriptedRandom rng({0.9}); // > kTeaseChance
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    // 连续多次 tick 都不该触发
    for (int i = 1; i <= 20; ++i) {
        const PoseResult r = sm.handle(Event::tick(kBase + i * 100));
        QVERIFY(QString::fromStdString(r.pose) != QStringLiteral("teasing"));
    }
}

void StateMachineTest::keywordFallsBackSafely()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    // 不存在的关键词 → meme-omg 兜底，不产生空姿态
    const PoseResult r = sm.handle(Event::keywordHit("no-such-keyword", kBase));
    QCOMPARE(QString::fromStdString(r.pose), QStringLiteral("meme-omg"));
}

void StateMachineTest::catalogCoversAllPoses()
{
    QCOMPARE(core::kPoseCount, 92);
    for (int i = 0; i < core::kPoseCount; ++i) {
        const char *key = core::kPoses[i].key;
        QVERIFY2(key != nullptr && *key != '\0', "pose key 不能为空");
        QVERIFY2(core::poseExists(key), key);
        QVERIFY2(core::poseFile(key) != nullptr, key);
    }
    // 唯一性
    for (int i = 0; i < core::kPoseCount; ++i) {
        for (int j = i + 1; j < core::kPoseCount; ++j) {
            QVERIFY2(QString::fromUtf8(core::kPoses[i].key)
                         != QString::fromUtf8(core::kPoses[j].key),
                     "pose key 重复");
        }
    }
}

void StateMachineTest::usedPosesExist()
{
    // 状态机可能输出的姿态必须都在清单内（防止改名后静默退化）
    const char *used[] = {
        "idle-cute", "waiting", "thinking", "afk", "sleep",
        "react-head", "react-belly", "react-tail", "curious",
        "star", "eat", "angry", "blush", "pick-up", "teasing",
        "levelup", "achievement", "success", "meme-omg",
    };
    for (const char *p : used) {
        QVERIFY2(core::poseExists(p), p);
    }
}

// 纯逻辑测试：不需要 GUI/显示器，用 QCoreApplication 即可（CI 无桌面也能跑）
QTEST_GUILESS_MAIN(StateMachineTest)
#include "test_state_machine.moc"
