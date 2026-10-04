#include <QtTest>

#include "core/FestivalRules.h"
#include "core/IdleRules.h"
#include "core/PetStateMachine.h"
#include "core/PoseCatalog.h"
#include "core/PoseNames.h"

#include <ctime>

using namespace whalepet::core;
// 注意：using namespace 只引入成员，不引入命名空间名本身，
// 所以必须有别名才能在下面写 core::kPoseCount。
namespace core = whalepet::core;

namespace {
constexpr std::int64_t kBase = 1'000'000; // 任意基准时刻（ms）

// 某日**本地**正午的时间戳（ms）。节日判定按本地时区，用 mktime 构造可跨时区稳定复现。
std::int64_t localNoonMs(int year, int month, int day)
{
    std::tm tmv{};
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = 12;
    tmv.tm_isdst = -1;
    const std::time_t t = std::mktime(&tmv);
    return static_cast<std::int64_t>(t) * 1000;
}
}

class StateMachineTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 上下文态 ----
    void resetGoesIdle();
    void idleEscalatesWithTime();
    // P8：时段常驻立绘（傍晚 night / 深夜 pajama；2026-10-04 重构：深夜无唤醒态）
    void daySlotsAndLateNightPersistent();

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

    // ---- 节日换装（静息态，见 docs/STATE-MACHINE.md §5.1）----
    void festivalSwapsRestingPoses();
    void festivalCoversAllFiveDays();
    void festivalYieldsToWorkNightAndAway();

    // ---- 清单完整性 ----
    void catalogCoversAllPoses();
    void usedPosesExist();

    // ---- 立绘激活（2026-10-04）----
    void idlePoolPlaysInDay();
    void idlePoolWinkGatedByAffinity();
    void sleepLoopAfterLongIdle();
    void vitalsFullShowsTailSwing();
    void vitalsFullYieldsToTimeSlots();
    void lateNightIsIndependentStage();
    void lateNightClicksTriggerWeakPose();
    void workErrorShowsFailure();
    void questDoneShowsDailyDone();
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

void StateMachineTest::daySlotsAndLateNightPersistent()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    // 时段划分（core/DaySlotRules.h）
    QCOMPARE(static_cast<int>(core::daySlotOf(7)), static_cast<int>(core::DaySlot::Day));
    QCOMPARE(static_cast<int>(core::daySlotOf(17)), static_cast<int>(core::DaySlot::Day));
    QCOMPARE(static_cast<int>(core::daySlotOf(18)), static_cast<int>(core::DaySlot::Evening));
    QCOMPARE(static_cast<int>(core::daySlotOf(22)), static_cast<int>(core::DaySlot::Evening));
    QCOMPARE(static_cast<int>(core::daySlotOf(23)), static_cast<int>(core::DaySlot::LateNight));
    QCOMPARE(static_cast<int>(core::daySlotOf(6)), static_cast<int>(core::DaySlot::LateNight));

    // 深夜静默口径不变（isNight 仍供 makeLine 使用）
    QVERIFY(PetStateMachine::isNight(23));
    QVERIFY(PetStateMachine::isNight(0));
    QVERIFY(!PetStateMachine::isNight(6));

    // 傍晚（18:00–22:59）→ night，且**不**作为日间待机池候选
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(18, kBase)).pose),
             QStringLiteral("night"));
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(22, kBase + 100)).pose),
             QStringLiteral("night"));

    // 深夜（23:00–06:59）空闲 → 睡衣
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(23, kBase + 200)).pose),
             QStringLiteral("daily-pajama"));
    // 深夜优先级高于挂机态：长期无输入仍是睡衣，不会退到 afk
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + kAfkMs + 1000)).pose),
             QStringLiteral("daily-pajama"));

    // 2026-10-04 重构：深夜**已无唤醒态** —— 点击不切常驻立绘：
    // 一轮 10 次以内（此处 9 次）恒为 daily-pajama（立绘不切换）。
    const std::int64_t t0 = kBase + kAfkMs + 2000;
    for (int i = 0; i < kLateNightWeakClickCount - 1; ++i) {
        QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::Head, t0 + i * 100)).pose),
                 QStringLiteral("daily-pajama"));
    }

    // 修复「唤醒窗口跨时段泄漏」：22:59 的交互不再续期；跨 23:00 **立即刷新**为睡衣
    PetStateMachine leak(&rng);
    leak.reset(kBase);
    leak.handle(Event::clock(22, kBase)); // 傍晚
    const PoseResult pressed = leak.handle(Event::click(Zone::Head, kBase + 100)); // 22:59 交互
    QCOMPARE(QString::fromStdString(pressed.pose), QStringLiteral("react-head"));
    // 跨 23:00：检测到即**立即刷新** —— 常驻立刻变睡衣（旧实现会被上一时段交互
    // 泄漏出的唤醒窗口拖住，最长 60s 才切换）；残留的一次性姿态也一并被清除。
    QCOMPARE(QString::fromStdString(leak.handle(Event::clock(23, kBase + 300)).pose),
             QStringLiteral("daily-pajama"));
    QCOMPARE(QString::fromStdString(leak.handle(Event::tick(kBase + 400)).pose),
             QStringLiteral("daily-pajama"));

    // 回到日间 → 不再有时段立绘，走静息链（idle-cute / waiting）
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(7, kBase)).pose),
             QStringLiteral("idle-cute"));
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

void StateMachineTest::festivalSwapsRestingPoses()
{
    ScriptedRandom rng({0.9}); // 永不触发逗弄
    PetStateMachine sm(&rng);

    const std::int64_t noon = localNoonMs(2026, 12, 25); // 圣诞节
    QCOMPARE(QString::fromLatin1(core::festivalPoseOf(noon)), QStringLiteral("festival-christmas"));

    sm.reset(noon);
    // 静息第一档（默认待机）→ 节日立绘
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(noon + 200)).pose),
             QStringLiteral("festival-christmas"));
    // 静息第二档（等待，距上次输入 ≥ kWaitingMs）仍是节日立绘（不脱落换装）
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(noon + kWaitingMs)).pose),
             QStringLiteral("festival-christmas"));

    // 非节日日期回落默认待机立绘
    const std::int64_t plain = localNoonMs(2026, 12, 26);
    QVERIFY(core::festivalPoseOf(plain) == nullptr);
    sm.reset(plain);
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(plain + 200)).pose),
             QStringLiteral("idle-cute"));

    // 公历固定日不受年份限制（10-31 / 12-25 / 02-14）
    QCOMPARE(QString::fromLatin1(core::festivalPoseOf(localNoonMs(2031, 10, 31))),
             QStringLiteral("festival-halloween"));
    QCOMPARE(QString::fromLatin1(core::festivalPoseOf(localNoonMs(2030, 12, 25))),
             QStringLiteral("festival-christmas"));
    QCOMPARE(QString::fromLatin1(core::festivalPoseOf(localNoonMs(2029, 2, 14))),
             QStringLiteral("valentine"));
}

void StateMachineTest::festivalCoversAllFiveDays()
{
    // 覆盖范围与参考项目 festivalKey 逐条一致：3 个公历固定日 + 2 个农历（小表）
    struct Case {
        int y, m, d;
        const char *pose;
    };
    const Case cases[] = {
        {2026, 10, 31, "festival-halloween"},
        {2026, 12, 25, "festival-christmas"},
        {2026, 2, 14, "valentine"},
        {2026, 2, 17, "festival-spring"},      // 春节（农历，表驱动）
        {2026, 9, 25, "festival-mid-autumn"},  // 中秋（农历，表驱动）
        {2027, 2, 6, "festival-spring"},
        {2027, 9, 15, "festival-mid-autumn"},
    };
    for (const Case &c : cases) {
        const char *pose = core::festivalPoseOf(localNoonMs(c.y, c.m, c.d));
        QVERIFY2(pose != nullptr, core::festivalDateKey(localNoonMs(c.y, c.m, c.d)).c_str());
        QCOMPARE(QString::fromLatin1(pose), QString::fromLatin1(c.pose));
        // 换装资源必须都在既有立绘清单内（防改名后静默退化）
        QVERIFY2(core::poseExists(pose), pose);
    }

    // 表内没有的年份只是「不换装」，不会误判为其它节日
    QVERIFY(core::festivalPoseOf(localNoonMs(2028, 2, 17)) == nullptr);
    QVERIFY(core::festivalPoseOf(1'000'000) == nullptr); // 1970-01-01
}

void StateMachineTest::festivalYieldsToWorkNightAndAway()
{
    ScriptedRandom rng({0.9});
    PetStateMachine sm(&rng);
    const std::int64_t noon = localNoonMs(2026, 12, 25);
    sm.reset(noon);

    // 1) 编程工作（busy）态优先，节日让位；P8 起编程族常驻 running
    sm.handle(Event::workStateChanged(static_cast<int>(WorkState::Coding), noon + 100));
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(noon + 200)).pose),
             QStringLiteral("running"));
    sm.handle(Event::workStateChanged(static_cast<int>(WorkState::Unknown), noon + 300));

    // 2) WorkState::Idle（在电脑前但未产出）属未工作态 → 回静息，节日换装生效
    PetStateMachine idleSm(&rng);
    idleSm.reset(noon);
    idleSm.handle(Event::workStateChanged(static_cast<int>(WorkState::Idle), noon + 100));
    QCOMPARE(QString::fromStdString(idleSm.handle(Event::tick(noon + 200)).pose),
             QStringLiteral("festival-christmas"));

    // 3) 未工作的其它细分态各有专属立绘，不换装
    PetStateMachine browseSm(&rng);
    browseSm.reset(noon);
    browseSm.handle(Event::workStateChanged(static_cast<int>(WorkState::Browsing), noon + 100));
    QCOMPARE(QString::fromStdString(browseSm.handle(Event::tick(noon + 200)).pose),
             QStringLiteral("curious"));

    // 4) 深夜 → 睡衣（节日不换装）
    PetStateMachine nightSm(&rng);
    nightSm.reset(noon);
    nightSm.handle(Event::clock(23, noon + 100));
    QCOMPARE(QString::fromStdString(nightSm.handle(Event::tick(noon + 200)).pose),
             QStringLiteral("daily-pajama"));

    // 5) 思考 / 离开 → 不换装
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(noon + kThinkingMs)).pose),
             QStringLiteral("thinking"));
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(noon + kAfkMs)).pose),
             QStringLiteral("afk"));

    // 6) 一次性互动优先：点击期间保持互动立绘，到期后回到节日立绘
    PetStateMachine clickSm(&rng);
    clickSm.reset(noon);
    QCOMPARE(QString::fromStdString(clickSm.handle(Event::click(Zone::Head, noon)).pose),
             QStringLiteral("react-head"));
    QCOMPARE(QString::fromStdString(clickSm.handle(Event::tick(noon + kCuriousWindowMs)).pose),
             QStringLiteral("festival-christmas"));
}

void StateMachineTest::catalogCoversAllPoses()
{
    // 93 = 89 张 state-* + home-peek / home-bottom / settings-peek / workbench-peek
    QCOMPARE(core::kPoseCount, 93);
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
        // 静息换装（5 个节日）
        "festival-spring", "festival-mid-autumn", "festival-halloween",
        "festival-christmas", "valentine",
        // P8：时段常驻立绘 + 编程常驻 + 工作立绘池
        "night", "daily-pajama", "running",
        "work-boss", "work-celebrate", "work-deadline", "work-debug", "work-deploy",
        "work-idea", "work-meeting", "work-pat", "work-ram", "work-review",
        "work-slack", "work-slack-phone", "work-sleep",
        // 2026-10-04 立绘激活：日间待机池 / 睡眠循环 / 满值与一次性表现
        "daily-coffee", "daily-cooking", "daily-eat", "daily-fishing", "daily-painting",
        "daily-picnic", "daily-shower", "cool-shades", "meme-music", "wink",
        "daily-stretch", "tail-swing", "failure", "daily-done",
    };
    for (const char *p : used) {
        QVERIFY2(core::poseExists(p), p);
    }
}

// ---- 立绘激活（2026-10-04）----

void StateMachineTest::idlePoolPlaysInDay()
{
    ScriptedRandom rng({}); // 不触发逗弄；待机池随机取首张（daily-coffee）
    PetStateMachine sm(&rng);
    sm.reset(kBase); // m_hour 默认 12 → 日间

    // 首次满足条件只武装计时，不立即播放
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + 1000)).pose),
             QStringLiteral("idle-cute"));

    // 15s 到点：随机播一张（rng 固定 → 池首张 daily-coffee）
    const std::int64_t fireAt = kBase + 1000 + core::kIdlePoolIntervalMs;
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(fireAt)).pose),
             QStringLiteral("daily-coffee"));

    // 维持 3s 内保持该立绘
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(fireAt + 2000)).pose),
             QStringLiteral("daily-coffee"));

    // 3s 到期后回落常驻（此时待机 19s → waiting）
    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::tick(fireAt + core::kIdlePoolHoldMs)).pose),
             QStringLiteral("waiting"));
}

void StateMachineTest::idlePoolWinkGatedByAffinity()
{
    // wink 固定在池末尾，门槛通过截断可选数量实现
    QCOMPARE(static_cast<int>(core::idlePoolEligibleCount(0)), 9);
    QCOMPARE(static_cast<int>(core::idlePoolEligibleCount(core::kWinkAffinityThreshold - 1)), 9);
    QCOMPARE(static_cast<int>(core::idlePoolEligibleCount(core::kWinkAffinityThreshold)), 10);
    QCOMPARE(QString::fromLatin1(core::kIdlePoolPoses[core::kIdlePoolPoseCount - 1]),
             QStringLiteral("wink"));
}

void StateMachineTest::sleepLoopAfterLongIdle()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase); // m_hour 默认 12 → 日间

    const std::int64_t start = kBase + core::kSleepIdleMs; // 待机满 20min → 睡眠起点
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(start)).pose),
             QStringLiteral("sleep"));

    // 睡满 10min → 进入 5s 伸懒腰窗口
    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::tick(start + core::kSleepHoldMs + 1000)).pose),
             QStringLiteral("daily-stretch"));

    // 伸懒腰结束 → 回到睡眠，循环继续
    QCOMPARE(QString::fromStdString(sm.handle(
                 Event::tick(start + core::kSleepHoldMs + core::kSleepStretchHoldMs + 1000)).pose),
             QStringLiteral("sleep"));
}

void StateMachineTest::vitalsFullShowsTailSwing()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    sm.setVitals(100, 100);
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + 100)).pose),
             QStringLiteral("tail-swing"));

    // 任一不满值 → 立即回落常驻
    sm.setVitals(100, 99);
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + 200)).pose),
             QStringLiteral("idle-cute"));
}

// 2026-10-04 修复：时段态优先于满值常驻（方案 A）。
// 回归点：原「满值」块位于时段态之前，满值时深夜永不显示睡衣（18:00 起也不显示 night）。
void StateMachineTest::vitalsFullYieldsToTimeSlots()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    sm.setVitals(100, 100); // 心情 & 饱腹同时满值

    // 日间：满值常驻生效（tail-swing）
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(12, kBase + 200)).pose),
             QStringLiteral("tail-swing"));
    // 傍晚：时段态优先 → night
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(18, kBase + 400)).pose),
             QStringLiteral("night"));
    // 深夜：时段态优先 → 睡衣（回归点）
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(23, kBase + 600)).pose),
             QStringLiteral("daily-pajama"));
    // 回到日间：满值常驻恢复（本档未被删除，只是让位给时段态）
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(10, kBase + 800)).pose),
             QStringLiteral("tail-swing"));
}

// 2026-10-04：深夜独立阶段 —— 不参与任何随机立绘池，只保留点击反馈。
void StateMachineTest::lateNightIsIndependentStage()
{
    ScriptedRandom rng({0.001}); // 该序列在日间会命中逗弄（teasing）
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    sm.setVitals(100, 100); // 满值常驻在深夜同样不得接管
    sm.handle(Event::clock(23, kBase));

    // 1) 随机池关闭：连续 tick 不出现 teasing，恒为睡衣
    for (int i = 1; i <= 30; ++i) {
        QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + i * 1000)).pose),
                 QStringLiteral("daily-pajama"));
    }
    // 2) 睡眠循环不接管深夜（待机远超 20min 仍是睡衣，不退化成 sleep）
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + kSleepIdleMs + 5000)).pose),
             QStringLiteral("daily-pajama"));
    // 3) 满值常驻不接管深夜（时段态优先）
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + kSleepIdleMs + 6000)).pose),
             QStringLiteral("daily-pajama"));

    // 4) 深夜最高优先级：工作态（编程 running）也不接管立绘，且不播报 work.* 语句
    const PoseResult coding = sm.handle(Event::workStateChanged(
        static_cast<int>(WorkState::Coding), kBase + kSleepIdleMs + 6500));
    QCOMPARE(QString::fromStdString(coding.pose), QStringLiteral("daily-pajama"));
    QVERIFY(coding.lineKey.empty());
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(kBase + kSleepIdleMs + 6600)).pose),
             QStringLiteral("daily-pajama"));
    // 退出工作态（避免影响下一步的时段断言）
    sm.handle(Event::workStateChanged(static_cast<int>(WorkState::Unknown),
                                      kBase + kSleepIdleMs + 6700));

    // 5) 离开深夜后随机 / 常驻池立即恢复：傍晚 + 长时间待机 → 睡眠循环接管
    QCOMPARE(QString::fromStdString(sm.handle(Event::clock(18, kBase + kSleepIdleMs + 7000)).pose),
             QStringLiteral("sleep"));
}

// 2026-10-04：深夜点击累计达 10 次 → meme-smile-pain（虚弱）保持 20s + 虚弱台词。
void StateMachineTest::lateNightClicksTriggerWeakPose()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);
    sm.handle(Event::clock(23, kBase));

    // 前 9 次点击：**不切换立绘**（2026-10-04 重构）—— 常驻保持 daily-pajama，
    // 但仍回应台词（click.* 场景）并累计点击。
    const std::int64_t t0 = kBase + 1000;
    for (int i = 0; i < kLateNightWeakClickCount - 1; ++i) {
        const PoseResult r = sm.handle(Event::click(Zone::Head, t0 + i * 10));
        QCOMPARE(QString::fromStdString(r.pose), QStringLiteral("daily-pajama"));
        QCOMPARE(QString::fromStdString(r.lineKey), QStringLiteral("click.head"));
    }

    // 第 10 次：切虚弱立绘 + 虚弱台词，保持 20s
    const std::int64_t weakAt = t0 + (kLateNightWeakClickCount - 1) * 10;
    const PoseResult weak = sm.handle(Event::click(Zone::Head, weakAt));
    QCOMPARE(QString::fromStdString(weak.pose), QString::fromStdString(kLateNightWeakPose));
    QCOMPARE(weak.ttlMs, static_cast<int>(kLateNightWeakHoldMs));
    QCOMPARE(QString::fromStdString(weak.lineKey), QString::fromStdString(kLateNightWeakScene));
    QVERIFY(sm.lateNightWeak(weakAt));

    // 20s 内保持虚弱（一次性姿态未到期）
    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::tick(weakAt + kLateNightWeakHoldMs - 1000)).pose),
             QString::fromStdString(kLateNightWeakPose));

    // 虚弱期间点击 → **完全无响应**（角色不理会鼠标）：立绘不变、也不产生新台词
    const std::uint32_t lineSerialBefore = sm.current().lineSerial;
    const PoseResult ignored = sm.handle(Event::click(Zone::Tail, weakAt + 1000));
    QCOMPARE(QString::fromStdString(ignored.pose), QString::fromStdString(kLateNightWeakPose));
    QCOMPARE(ignored.lineSerial, lineSerialBefore); // 既不新增台词，也不重播虚弱台词
    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::tick(weakAt + 1000 + kCuriousWindowMs)).pose),
             QString::fromStdString(kLateNightWeakPose));

    // 虚弱窗口到期（20s）→ 直接回到常驻睡衣（重构后无唤醒态）
    const std::int64_t weakEnded = weakAt + kLateNightWeakHoldMs + 1000;
    QVERIFY(!sm.lateNightWeak(weakEnded));
    QCOMPARE(QString::fromStdString(sm.handle(Event::tick(weakEnded)).pose),
             QStringLiteral("daily-pajama"));

    // 计数已清零：再累计 9 次仍是睡衣（不换立绘），第 10 次才再次触发虚弱
    const std::int64_t base2 = weakEnded + 1000;
    for (int i = 0; i < kLateNightWeakClickCount - 1; ++i) {
        QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::Body, base2 + i * 100)).pose),
                 QStringLiteral("daily-pajama"));
    }
    QCOMPARE(QString::fromStdString(sm.handle(Event::click(Zone::Body, base2 + 1000)).pose),
             QString::fromStdString(kLateNightWeakPose));
}

void StateMachineTest::workErrorShowsFailure()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::simple(EventType::WorkError, kBase + 100)).pose),
             QStringLiteral("failure"));
}

void StateMachineTest::questDoneShowsDailyDone()
{
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    QCOMPARE(QString::fromStdString(
                 sm.handle(Event::simple(EventType::QuestDone, kBase + 100)).pose),
             QStringLiteral("daily-done"));
}

// 纯逻辑测试：不需要 GUI/显示器，用 QCoreApplication 即可（CI 无桌面也能跑）
QTEST_GUILESS_MAIN(StateMachineTest)
#include "test_state_machine.moc"
