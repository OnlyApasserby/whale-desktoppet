#include <QtTest>

#include <QCoreApplication>

#include "core/PetStateMachine.h"
#include "core/PoseCatalog.h"
#include "core/WorkState.h"
#include "core/WorkStateRules.h"

#include <string>

// P7 工作状态判定 + 状态机工作态通道（docs/PLUGIN-ARCHITECTURE.md §3.1/§6、docs/ROADMAP-P7-Fin.md P7.0）。
//
// 覆盖点：
//   * 应用类别归一化与「无数据 ≠ 未知应用」；
//   * 各工作状态的可操作判据（含 Coding 与 Vibe Coding 的区分）；
//   * 置信度阈值与最短驻留滞回（抖动抑制）；
//   * 工作态 → 立绘 / 台词场景映射必须落在既有 92 张立绘与 work.* 语料内；
//   * 状态机：工作态优先级、「专注态主动静默」与 work.* 豁免、「不打断」一次性姿态；
//   * **零回归**：WorkState::Unknown 时行为与 P6 完全一致。

using whalepet::core::AppCategory;
using whalepet::core::EnvSample;
using whalepet::core::Event;
using whalepet::core::PoseResult;
using whalepet::core::WorkState;
using whalepet::core::WorkStateRules;
using whalepet::core::WorkStateSample;

namespace {

EnvSample makeSample(const char *appId, const char *title, std::int64_t nowMs)
{
    EnvSample sample;
    sample.appId = (appId == nullptr) ? std::string() : std::string(appId);
    sample.windowTitle = (title == nullptr) ? std::string() : std::string(title);
    sample.category = whalepet::core::classifyApp(sample.appId, sample.windowTitle);
    sample.nowMs = nowMs;
    return sample;
}

QString stateId(WorkState state)
{
    return QString::fromLatin1(whalepet::core::workStateId(state));
}

WorkState stateOf(const WorkStateSample &sample)
{
    return sample.state;
}

} // namespace

class WorkStateTest : public QObject {
    Q_OBJECT
private slots:
    void classifiesAppCategory();
    void unknownWhenNoData();
    void detectsAfkAndIdle();
    void detectsMeetingAndGame();
    void distinguishesReadingFromBrowsing();
    void detectsDebuggingOverVibeCoding();
    void detectsVibeCodingOnBurstAndJumps();
    void detectsCodingOnSustainedDwell();
    void realDesktopProfilesMapToExpectedStates();
    void pausedSessionWinsOverMissingData();
    void ambiguousEditorDoesNotChangeState();
    void hysteresisKeepsPreviousWithinMinDwell();
    void unknownTakesEffectImmediately();
    void focusStatesAreQuiet();
    void poseAndSceneMappingsStayInExistingAssets();
    void machineDrivesPoseAndSilencesProactive();
    void machineDoesNotInterruptOneShot();
    void unknownWorkStateKeepsLegacyBehavior();
};

void WorkStateTest::classifiesAppCategory()
{
    // 进程名优先：去路径、去 .exe、忽略大小写
    QCOMPARE(whalepet::core::classifyApp("C:\\Program Files\\Microsoft VS Code\\Code.exe", ""),
             AppCategory::Editor);
    QCOMPARE(whalepet::core::classifyApp("notepad++.exe", ""), AppCategory::Editor);
    QCOMPARE(whalepet::core::classifyApp("WindowsTerminal.exe", ""), AppCategory::Terminal);
    QCOMPARE(whalepet::core::classifyApp("chrome.exe", ""), AppCategory::Browser);
    QCOMPARE(whalepet::core::classifyApp("ms-teams.exe", ""), AppCategory::Meeting);
    QCOMPARE(whalepet::core::classifyApp("steam.exe", ""), AppCategory::Game);
    QCOMPARE(whalepet::core::classifyApp("winword.exe", ""), AppCategory::Office);

    // 进程名识别不了时退回标题关键词
    QCOMPARE(whalepet::core::classifyApp("something.exe", "main.cpp - Visual Studio Code"),
             AppCategory::Editor);
    QCOMPARE(whalepet::core::classifyApp("something.exe", "Windows Terminal"),
             AppCategory::Terminal);

    // 认得出但与工作状态判定无关 → Other
    QCOMPARE(whalepet::core::classifyApp("explorer.exe", "此电脑"), AppCategory::Other);

    // 「无数据」≠「未知应用」
    QCOMPARE(whalepet::core::classifyApp("", ""), AppCategory::Unknown);

    // 反查
    QCOMPARE(whalepet::core::appCategoryFromId("editor"), AppCategory::Editor);
    QCOMPARE(whalepet::core::appCategoryFromId("no-such"), AppCategory::Unknown);

    // 工作状态 id 往返
    for (int i = 0; i <= static_cast<int>(WorkState::Afk); ++i) {
        const auto state = static_cast<WorkState>(i);
        QCOMPARE(static_cast<int>(whalepet::core::workStateFromId(whalepet::core::workStateId(state))),
                 static_cast<int>(state));
    }
    QCOMPARE(static_cast<int>(whalepet::core::workStateFromId("no-such")),
             static_cast<int>(WorkState::Unknown));
}

void WorkStateTest::unknownWhenNoData()
{
    WorkStateRules rules;
    EnvSample empty;
    empty.nowMs = 1000;

    const WorkStateSample candidate = rules.candidate(empty);
    QCOMPARE(static_cast<int>(candidate.state), static_cast<int>(WorkState::Unknown));
    QCOMPARE(candidate.confidence, 0.0);

    // 即便上一次是 Coding，无数据也必须立即回到 Unknown（不等驻留、不看置信度）
    WorkStateSample prev;
    prev.state = WorkState::Coding;
    prev.confidence = 0.78;
    prev.sinceMs = 0;
    const WorkStateSample next = rules.evaluate(empty, prev);
    QCOMPARE(static_cast<int>(next.state), static_cast<int>(WorkState::Unknown));
}

void WorkStateTest::detectsAfkAndIdle()
{
    WorkStateRules rules;

    EnvSample afk = makeSample("chrome.exe", "某网页", 1000);
    afk.hasInput = false;
    afk.idleMs = whalepet::core::kWorkAfkMs;
    QCOMPARE(static_cast<int>(rules.candidate(afk).state), static_cast<int>(WorkState::Afk));

    // 会话锁定 / 屏保 / 全屏独占：人在不在电脑前是确定的
    EnvSample locked = makeSample("chrome.exe", "某网页", 1000);
    locked.hasInput = true;
    locked.idleMs = 0;
    locked.systemPaused = true;
    QCOMPARE(static_cast<int>(rules.candidate(locked).state), static_cast<int>(WorkState::Afk));

    EnvSample idle = makeSample("chrome.exe", "某网页", 1000);
    idle.hasInput = false;
    idle.idleMs = whalepet::core::kWorkIdleMs + 1000;
    QCOMPARE(static_cast<int>(rules.candidate(idle).state), static_cast<int>(WorkState::Idle));
}

void WorkStateTest::detectsMeetingAndGame()
{
    WorkStateRules rules;
    QCOMPARE(static_cast<int>(rules.candidate(makeSample("zoom.exe", "Zoom 会议", 1)).state),
             static_cast<int>(WorkState::Meeting));
    QCOMPARE(static_cast<int>(rules.candidate(makeSample("steam.exe", "Steam", 1)).state),
             static_cast<int>(WorkState::Game));
}

void WorkStateTest::distinguishesReadingFromBrowsing()
{
    WorkStateRules rules;

    EnvSample reading = makeSample("chrome.exe", "文档", 1);
    reading.hasInput = true;
    reading.idleMs = 5000;
    reading.inputEvents = whalepet::core::kWorkReadingMaxEvents; // 稀疏
    QCOMPARE(static_cast<int>(rules.candidate(reading).state), static_cast<int>(WorkState::Reading));

    EnvSample browsing = reading;
    browsing.inputEvents = whalepet::core::kWorkReadingMaxEvents + 5; // 输入适中
    QCOMPARE(static_cast<int>(rules.candidate(browsing).state),
             static_cast<int>(WorkState::Browsing));
}

void WorkStateTest::detectsDebuggingOverVibeCoding()
{
    WorkStateRules rules;

    // 输入节奏与 Vibe 一模一样，但标题带调试语义 → 必须判 Debugging（顺序不能反）
    EnvSample debug = makeSample("Code.exe", "launch.json - 断点调试 - Visual Studio Code", 5000);
    debug.hasInput = true;
    debug.inputEvents = whalepet::core::kVibeBurstMinEvents + 10;
    debug.appSwitches = whalepet::core::kVibeMinSwitches + 2;
    debug.dwellMs = whalepet::core::kVibeMaxDwellMs - 1000;

    QCOMPARE(static_cast<int>(rules.candidate(debug).state),
             static_cast<int>(WorkState::Debugging));
}

void WorkStateTest::detectsVibeCodingOnBurstAndJumps()
{
    WorkStateRules rules;

    EnvSample vibe = makeSample("Code.exe", "main.cpp - Visual Studio Code", 5000);
    vibe.hasInput = true;
    vibe.inputEvents = whalepet::core::kVibeBurstMinEvents + 5; // 爆发
    vibe.appSwitches = whalepet::core::kVibeMinSwitches;        // 频繁切换
    vibe.dwellMs = whalepet::core::kVibeMaxDwellMs - 5000;      // 停留短

    const WorkStateSample candidate = rules.candidate(vibe);
    QCOMPARE(static_cast<int>(candidate.state), static_cast<int>(WorkState::VibeCoding));
    QVERIFY(candidate.confidence >= whalepet::core::kWorkStateMinConfidence);
}

void WorkStateTest::detectsCodingOnSustainedDwell()
{
    WorkStateRules rules;

    EnvSample coding = makeSample("Code.exe", "main.cpp - Visual Studio Code", 100000);
    coding.hasInput = true;
    coding.inputEvents = 30;
    coding.appSwitches = whalepet::core::kWorkCodingMaxSwitches;
    coding.dwellMs = whalepet::core::kWorkCodingMinDwellMs + 1000; // 长时间连续编辑

    const WorkStateSample candidate = rules.candidate(coding);
    QCOMPARE(static_cast<int>(candidate.state), static_cast<int>(WorkState::Coding));
    QVERIFY(candidate.confidence >= whalepet::core::kWorkStateMinConfidence);

    // 有输入才谈得上「在写」：停留再久但没有输入 → 不得判 Coding
    EnvSample noInput = coding;
    noInput.inputEvents = 0;
    QVERIFY(rules.candidate(noInput).state != WorkState::Coding);
}

void WorkStateTest::realDesktopProfilesMapToExpectedStates()
{
    // P7.1：把真实 Win32 采集（低层钩子计数）观察到的**典型输入画像**固化为回归用例。
    // 数值取自钩子计数的量级（打字约 5~10 次/秒，鼠标只计按键与滚轮），
    // 用于防止后续调参把「真实使用」判错。
    WorkStateRules rules;

    // 画像 1：专注编码 —— 编辑器内连续输入，几乎不切换
    EnvSample focus = makeSample("Code.exe", "main.cpp - Visual Studio Code", 3600000);
    focus.hasInput = true;
    focus.idleMs = 400;
    focus.inputEvents = 80;   // 10s 窗口内 80 次（≈8 次/秒）
    focus.appSwitches = 1;
    focus.dwellMs = 240000;   // 同一应用已停留 4 分钟
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(focus))), static_cast<int>(WorkState::Coding));

    // 画像 2：刚切回编辑器就开始高速输入（停留远不足 60s）也必须承认「在写」
    EnvSample fresh = focus;
    fresh.dwellMs = 12000;
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(fresh))), static_cast<int>(WorkState::Coding));

    // 画像 3：与 Agent 快速迭代 —— 编辑器/终端之间来回切，输入密集且停留短
    EnvSample vibe = makeSample("WindowsTerminal.exe", "pwsh", 3600000);
    vibe.hasInput = true;
    vibe.idleMs = 200;
    vibe.inputEvents = 180; // ≈18 次/秒
    vibe.appSwitches = 6;
    vibe.dwellMs = 15000;
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(vibe))),
             static_cast<int>(WorkState::VibeCoding));

    // 画像 4：看文档 —— 浏览器内输入稀疏（10s 窗口 ≤3 次）
    EnvSample reading = makeSample("chrome.exe", "某 API 文档", 1000);
    reading.hasInput = true;
    reading.idleMs = 6000;
    reading.inputEvents = 2;
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(reading))), static_cast<int>(WorkState::Reading));

    // 画像 5：离开 —— 长时间无输入
    EnvSample away = makeSample("chrome.exe", "某 API 文档", 1000);
    away.hasInput = false;
    away.idleMs = whalepet::core::kWorkAfkMs + 20000;
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(away))), static_cast<int>(WorkState::Afk));
}

void WorkStateTest::pausedSessionWinsOverMissingData()
{
    WorkStateRules rules;

    // 锁屏的真实特征：前台窗口读不到（无 appId / 标题、无输入），
    // 数据形状与「没开感知」完全一样，唯一区别是 systemPaused。
    EnvSample locked;
    locked.nowMs = 60000;
    locked.systemPaused = true;
    QVERIFY(locked.isEmpty());
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(locked))), static_cast<int>(WorkState::Afk));

    // 反向守卫（零回归红线）：没有系统状态时，空样本仍必须是 Unknown
    EnvSample empty;
    empty.nowMs = 60000;
    QCOMPARE(static_cast<int>(stateOf(rules.candidate(empty))), static_cast<int>(WorkState::Unknown));
}

void WorkStateTest::ambiguousEditorDoesNotChangeState()
{
    WorkStateRules rules;

    // 刚切到编辑器：停留不够长（不满足 Coding）、也不够跳动（不满足 Vibe）→ 低置信度
    EnvSample ambiguous = makeSample("Code.exe", "main.cpp - Visual Studio Code", 50000);
    ambiguous.hasInput = true;
    ambiguous.inputEvents = 30;
    ambiguous.appSwitches = whalepet::core::kVibeMinSwitches;
    ambiguous.dwellMs = whalepet::core::kWorkCodingMinDwellMs - 10000;

    QCOMPARE(static_cast<int>(rules.candidate(ambiguous).state),
             static_cast<int>(WorkState::Reading));
    QVERIFY(rules.candidate(ambiguous).confidence < whalepet::core::kWorkStateMinConfidence);

    // 置信度不足 → 不改变现状（这本身就是抖动抑制）
    WorkStateSample prev;
    prev.state = WorkState::Unknown;
    const WorkStateSample next = rules.evaluate(ambiguous, prev);
    QCOMPARE(static_cast<int>(next.state), static_cast<int>(WorkState::Unknown));
}

void WorkStateTest::hysteresisKeepsPreviousWithinMinDwell()
{
    WorkStateRules rules;

    WorkStateSample prev;
    prev.state = WorkState::Coding;
    prev.confidence = 0.78;
    prev.sinceMs = 100000;

    // 刚进入 Coding 1s 就切到会议 → 未满最短驻留 → 保持 Coding
    EnvSample meeting = makeSample("zoom.exe", "Zoom 会议", 101000);
    meeting.hasInput = true;
    const WorkStateSample kept = rules.evaluate(meeting, prev);
    QCOMPARE(static_cast<int>(kept.state), static_cast<int>(WorkState::Coding));
    QCOMPARE(kept.sinceMs, prev.sinceMs);

    // 驻留满后允许切换
    EnvSample later = makeSample("zoom.exe", "Zoom 会议",
                                100000 + whalepet::core::kWorkStateMinDwellMs);
    later.hasInput = true;
    QCOMPARE(static_cast<int>(rules.evaluate(later, prev).state),
             static_cast<int>(WorkState::Meeting));
}

void WorkStateTest::unknownTakesEffectImmediately()
{
    WorkStateRules rules;

    WorkStateSample prev;
    prev.state = WorkState::VibeCoding;
    prev.confidence = 0.72;
    prev.sinceMs = 200000;

    EnvSample empty;
    empty.nowMs = 200500; // 远未满最短驻留
    QCOMPARE(static_cast<int>(rules.evaluate(empty, prev).state),
             static_cast<int>(WorkState::Unknown));
}

void WorkStateTest::focusStatesAreQuiet()
{
    QVERIFY(whalepet::core::workStateIsFocus(WorkState::Coding));
    QVERIFY(whalepet::core::workStateIsFocus(WorkState::VibeCoding));
    QVERIFY(whalepet::core::workStateIsFocus(WorkState::Debugging));
    QVERIFY(whalepet::core::workStateIsFocus(WorkState::Meeting));
    QVERIFY(!whalepet::core::workStateIsFocus(WorkState::Idle));
    QVERIFY(!whalepet::core::workStateIsFocus(WorkState::Reading));
    QVERIFY(!whalepet::core::workStateIsFocus(WorkState::Browsing));
    QVERIFY(!whalepet::core::workStateIsFocus(WorkState::Game));
    QVERIFY(!whalepet::core::workStateIsFocus(WorkState::Afk));
    QVERIFY(!whalepet::core::workStateIsFocus(WorkState::Unknown));
}

void WorkStateTest::poseAndSceneMappingsStayInExistingAssets()
{
    // 不得自造美术资源：每个工作态的立绘都必须存在于既有 92 张清单中
    for (int i = 0; i <= static_cast<int>(WorkState::Afk); ++i) {
        const auto state = static_cast<WorkState>(i);
        const char *pose = whalepet::core::workStatePose(state);
        const char *scene = whalepet::core::workStateScene(state);

        if (state == WorkState::Unknown) {
            QVERIFY(pose == nullptr);
            QVERIFY(scene == nullptr);
            continue;
        }
        QVERIFY2(pose != nullptr, qPrintable(stateId(state)));
        QVERIFY2(whalepet::core::poseExists(pose), pose);
        QVERIFY2(scene != nullptr, qPrintable(stateId(state)));
        QVERIFY(QString::fromLatin1(scene).startsWith(QStringLiteral("work.")));
    }
}

void WorkStateTest::machineDrivesPoseAndSilencesProactive()
{
    whalepet::core::PetStateMachine machine(nullptr);
    machine.reset(0);

    // 状态显著变化 → 切换立绘 + 播报一句（work.* 场景）
    const PoseResult arrived = machine.handle(
        Event::workStateChanged(static_cast<int>(WorkState::Coding), 1000));
    QCOMPARE(QString::fromStdString(arrived.pose), QStringLiteral("work-ram"));
    QCOMPARE(QString::fromStdString(arrived.lineKey), QStringLiteral("work.coding"));
    QCOMPARE(static_cast<int>(machine.workState()), static_cast<int>(WorkState::Coding));

    // 专注态：主动台词静默
    const PoseResult quiet =
        machine.speak("", "greet.slot1", 0, Event::tick(20000), true);
    QVERIFY2(quiet.lineKey.empty(), "专注编码期间不得主动说话");

    // 唯一豁免：工作状态自身的播报（否则「状态显著变化时出现」会被自己静默掉）
    const PoseResult exempt =
        machine.speak("", "work.debugging", 0, Event::tick(40000), true);
    QCOMPARE(QString::fromStdString(exempt.lineKey), QStringLiteral("work.debugging"));

    // 工作态优先级高于挂机态（10 分钟无输入仍显示工作态，而非 afk）
    const PoseResult later = machine.handle(Event::tick(600000));
    QCOMPARE(QString::fromStdString(later.pose), QStringLiteral("work-ram"));
}

void WorkStateTest::machineDoesNotInterruptOneShot()
{
    whalepet::core::PetStateMachine machine(nullptr);
    machine.reset(0);

    const PoseResult click = machine.handle(Event::click(whalepet::core::Zone::Head, 100));
    QCOMPARE(QString::fromStdString(click.pose), QStringLiteral("react-head"));
    QVERIFY(click.lineSerial != 0);

    // 一次性姿态保持期内工作态到达：不得覆盖立绘、也不得产生**新的**表现批次
    // （返回值是同一份缓存结果，lineSerial 不变 → Presenter 不会重播台词/插话）
    const PoseResult during = machine.handle(
        Event::workStateChanged(static_cast<int>(WorkState::Coding), 1000));
    QCOMPARE(QString::fromStdString(during.pose), QStringLiteral("react-head"));
    QCOMPARE(during.lineSerial, click.lineSerial);
    QCOMPARE(during.fxSerial, click.fxSerial);
    // 但工作态本身已被记下
    QCOMPARE(static_cast<int>(machine.workState()), static_cast<int>(WorkState::Coding));

    // 一次性姿态到期后自然接管
    const PoseResult after = machine.handle(Event::tick(7000));
    QCOMPARE(QString::fromStdString(after.pose), QStringLiteral("work-ram"));
}

void WorkStateTest::unknownWorkStateKeepsLegacyBehavior()
{
    // 零回归守卫：默认 Unknown（无感知数据）时，姿态判定与 P6 完全一致
    whalepet::core::PetStateMachine machine(nullptr);
    machine.reset(0);
    QCOMPARE(static_cast<int>(machine.workState()), static_cast<int>(WorkState::Unknown));

    const PoseResult idle = machine.handle(Event::tick(200));
    QCOMPARE(QString::fromStdString(idle.pose), QStringLiteral("idle-cute"));

    // 挂机态仍然生效（此时仍是白天）
    const PoseResult afk = machine.handle(Event::tick(600000));
    QCOMPARE(QString::fromStdString(afk.pose), QStringLiteral("afk"));

    // 时段态仍然生效
    machine.handle(Event::clock(23, 600100));
    const PoseResult night = machine.handle(Event::tick(600200));
    QCOMPARE(QString::fromStdString(night.pose), QStringLiteral("sleep"));

    // 工作态优先于时段态；显式 Unknown 事件不播报并退回时段态
    machine.handle(Event::workStateChanged(static_cast<int>(WorkState::Meeting), 800000));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(800100)).pose),
             QStringLiteral("work-meeting"));

    const PoseResult cleared = machine.handle(
        Event::workStateChanged(static_cast<int>(WorkState::Unknown), 800200));
    QVERIFY(cleared.lineKey.empty());
    QCOMPARE(static_cast<int>(machine.workState()), static_cast<int>(WorkState::Unknown));
    QCOMPARE(QString::fromStdString(machine.handle(Event::tick(800300)).pose),
             QStringLiteral("sleep"));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QCoreApplication app(argc, argv);
    WorkStateTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_work_state.moc"
