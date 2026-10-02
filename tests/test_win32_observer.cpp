#include <QtTest>

#include <QCoreApplication>

#include "core/WorkState.h"
#include "core/WorkStateRules.h"
#include "platform/Win32DesktopObserver.h"
#include "platform/Win32TextUtil.h"

#include <cstdint>
#include <string>

// P7.1 真实 Win32 感知（docs/ROADMAP-P7.md P7.1、docs/CONTEXT-API.md §5）。
//
// 覆盖点：
//   * 文本工具：宽字符（UTF-16）→ UTF-8 中文标题不乱码；路径取进程名（识别 `\` 与 `/`）；
//   * 前台采样器：注入替身读数时的字段填充 / 失败（有效位为 false）时**不伪造**；
//   * 输入采样器：GetLastInputInfo 差分降级路径（每次至多计 1，诚实计数）、
//     空闲时长计算、注入替身时**绝不安装系统钩子**（单测进程安全）；
//   * 系统状态采样器：锁屏 / 屏保 → systemPaused；
//   * 端到端：锁屏导致前台窗口读不到时，WorkStateRules 必须判 `afk` 而不是 `unknown`
//     （P7.1 修复的判定顺序，见 docs/traps-P7.md TRAP-P7-006）。
//
// 全部用例只驱动**注入替身读数**：不安装任何系统钩子、不依赖真实前台窗口，
// 故可在无桌面/CI 环境稳定运行（不做「看起来通过」的伪断言）。

using whalepet::core::AppCategory;
using whalepet::core::EnvSample;
using whalepet::core::WorkState;
using whalepet::platform::Win32ActivityRaw;
using whalepet::platform::Win32DesktopObserver;
using whalepet::platform::Win32ForegroundRaw;
using whalepet::platform::Win32SystemRaw;

namespace {

// 一次「既定时段」的伪造桌面：全部字段可整体替换，模拟真实读数
struct FakeDesktop {
    bool fgValid = true;
    std::wstring windowTitle = L"main.cpp - Visual Studio Code";
    std::wstring processPath = L"C:\\Program Files\\Microsoft VS Code\\Code.exe";

    bool activityValid = true;
    std::uint32_t lastInputTickMs = 1000;
    std::uint32_t nowTickMs = 1000;

    bool systemValid = true;
    bool sessionLocked = false;
    bool screensaverRunning = false;
};

whalepet::platform::Win32ForegroundReader foregroundReader(const FakeDesktop &desktop)
{
    return [&desktop] {
        Win32ForegroundRaw raw;
        raw.valid = desktop.fgValid;
        raw.windowTitle = desktop.windowTitle;
        raw.processPath = desktop.processPath;
        return raw;
    };
}

whalepet::platform::Win32ActivityReader activityReader(const FakeDesktop &desktop)
{
    return [&desktop] {
        Win32ActivityRaw raw;
        raw.valid = desktop.activityValid;
        raw.lastInputTickMs = desktop.lastInputTickMs;
        raw.nowTickMs = desktop.nowTickMs;
        return raw;
    };
}

whalepet::platform::Win32SystemReader systemReader(const FakeDesktop &desktop)
{
    return [&desktop] {
        Win32SystemRaw raw;
        raw.valid = desktop.systemValid;
        raw.sessionLocked = desktop.sessionLocked;
        raw.screensaverRunning = desktop.screensaverRunning;
        return raw;
    };
}

} // namespace

class Win32ObserverTest : public QObject {
    Q_OBJECT
private slots:
    void utf8ConversionKeepsChineseText();
    void fileNameFromPathHandlesSeparators();
    void foregroundSamplerFillsNameAndTitle();
    void foregroundSamplerFailureDoesNotFakeData();
    void activitySamplerComputesIdleAndDelta();
    void activitySamplerInjectedReaderNeverInstallsHooks();
    void defaultConfigurationPrefersLowLevelHooks();
    void activitySamplerInvalidReadingReportsFailure();
    void systemStatusSamplerMapsLockAndScreensaver();
    void observerAggregatesInjectedReadings();
    void observerLifecycleClearsAggregationMemory();
    void lockedSessionWithoutForegroundIsAfk();
};

void Win32ObserverTest::utf8ConversionKeepsChineseText()
{
    const std::wstring chinese = L"未命名 - 记事本";
    const std::string utf8 = whalepet::platform::win32::toUtf8(chinese);
    // 必须与原宽字符串等价（乱码会在这里暴露）
    QCOMPARE(QString::fromStdString(utf8), QString::fromWCharArray(chinese.data(), int(chinese.size())));
    QVERIFY(!utf8.empty());

    QVERIFY(whalepet::platform::win32::toUtf8(std::wstring()).empty());
}

void Win32ObserverTest::fileNameFromPathHandlesSeparators()
{
    using whalepet::platform::win32::fileNameFromPath;

    QCOMPARE(QString::fromStdString(fileNameFromPath("C:\\Program Files\\Code.exe")),
             QStringLiteral("Code.exe"));
    QCOMPARE(QString::fromStdString(fileNameFromPath("C:/tools/powershell.exe")),
             QStringLiteral("powershell.exe"));
    // 已是纯文件名
    QCOMPARE(QString::fromStdString(fileNameFromPath("notepad++.exe")), QStringLiteral("notepad++.exe"));
    // 结尾分隔符 / 空串：没有文件名就不猜（不得返回目录名）
    QVERIFY(fileNameFromPath("C:\\dir\\").empty());
    QVERIFY(fileNameFromPath("").empty());
}

void Win32ObserverTest::foregroundSamplerFillsNameAndTitle()
{
    FakeDesktop desktop;
    whalepet::platform::Win32ForegroundSampler sampler(foregroundReader(desktop));
    QVERIFY(sampler.available());

    EnvSample out;
    out.nowMs = 5000;
    QVERIFY(sampler.sampleForeground(out));
    // 只暴露进程名（去路径），标题保持 UTF-8
    QCOMPARE(QString::fromStdString(out.appId), QStringLiteral("Code.exe"));
    QCOMPARE(QString::fromStdString(out.windowTitle),
             QStringLiteral("main.cpp - Visual Studio Code"));
    // 采样器只填自己负责的字段
    QCOMPARE(out.inputEvents, 0);
    QCOMPARE(out.nowMs, 5000);
}

void Win32ObserverTest::foregroundSamplerFailureDoesNotFakeData()
{
    FakeDesktop desktop;
    desktop.fgValid = false; // 无前台窗口（锁屏）
    whalepet::platform::Win32ForegroundSampler sampler(foregroundReader(desktop));

    EnvSample out;
    QVERIFY2(!sampler.sampleForeground(out), "读不到前台窗口必须返回 false，而不是编一个应用名");
    QVERIFY(out.appId.empty());
    QVERIFY(out.windowTitle.empty());
}

void Win32ObserverTest::activitySamplerComputesIdleAndDelta()
{
    FakeDesktop desktop;
    whalepet::platform::Win32ActivitySampler sampler(activityReader(desktop));

    // 差分降级模式（注入替身 → 不装钩子）：每次至多计 1，绝不放大
    QVERIFY(!sampler.usingLowLevelHooks());

    EnvSample first;
    desktop.lastInputTickMs = 1000;
    desktop.nowTickMs = 1000;
    QVERIFY(sampler.sampleActivity(first));
    QCOMPARE(first.idleMs, 0);
    QVERIFY(first.hasInput);                 // 距最近输入 0ms → 正在活跃
    QCOMPARE(first.inputEvents, 0);           // 首个样本没有增量基准

    // 2s 无输入：空闲过期 → 不再活跃，且不产生事件
    EnvSample idle;
    desktop.nowTickMs = 3000;
    QVERIFY(sampler.sampleActivity(idle));
    QCOMPARE(idle.idleMs, 2000);
    QVERIFY(!idle.hasInput);
    QCOMPARE(idle.inputEvents, 0);

    // 期间发生过输入（lastInput 前进）→ 差分计 1
    EnvSample typed;
    desktop.lastInputTickMs = 2500;
    desktop.nowTickMs = 3000;
    QVERIFY(sampler.sampleActivity(typed));
    QCOMPARE(typed.inputEvents, 1);
    QCOMPARE(typed.idleMs, 500);
    QVERIFY(typed.hasInput);

    // 时钟回绕/异常：不得产出负空闲
    EnvSample wrap;
    desktop.lastInputTickMs = 4000;
    desktop.nowTickMs = 1000;
    QVERIFY(sampler.sampleActivity(wrap));
    QCOMPARE(wrap.idleMs, 0);
}

void Win32ObserverTest::activitySamplerInjectedReaderNeverInstallsHooks()
{
    FakeDesktop desktop;
    whalepet::platform::Win32ActivitySampler sampler(activityReader(desktop));

    // 注入替身后即便被要求「开始观察」，也不得安装系统级钩子（单测进程安全）
    sampler.setObserving(true);
    QVERIFY(!sampler.usingLowLevelHooks());
    QCOMPARE(QString::fromLatin1(sampler.sourceName()),
             QStringLiteral("GetLastInputInfo differential"));

    sampler.setObserving(false);
    QVERIFY(!sampler.usingLowLevelHooks());
}

void Win32ObserverTest::defaultConfigurationPrefersLowLevelHooks()
{
    // 默认（真实读数）装配必须**倾向**低层钩子：否则差分降级会成为常态，
    // inputEvents 退化为「窗口内有输入的秒数」，Vibe Coding 判据（≥20）永远不可达。
    // 本用例只查「配置意图」，不真的安装钩子（单测进程不受影响）。
    Win32DesktopObserver observer;
    QVERIFY2(observer.hooksPreferred(),
             "默认装配必须倾向低层钩子（真实读数路径不得退化为差分降级）");
    QVERIFY(!observer.usingLowLevelHooks()); // 未 setObserving(true)，不得已装钩子

    // 注入替身读数（单测路径）则表示「不使用钩子」
    FakeDesktop desktop;
    Win32DesktopObserver injected(foregroundReader(desktop), activityReader(desktop),
                                  systemReader(desktop));
    QVERIFY(!injected.hooksPreferred());
}

void Win32ObserverTest::activitySamplerInvalidReadingReportsFailure()
{
    FakeDesktop desktop;
    desktop.activityValid = false;
    whalepet::platform::Win32ActivitySampler sampler(activityReader(desktop));

    EnvSample out;
    QVERIFY2(!sampler.sampleActivity(out), "GetLastInputInfo 失败必须返回 false（不伪造空闲/输入）");
    QCOMPARE(out.idleMs, 0);
    QCOMPARE(out.inputEvents, 0);
    QVERIFY(!out.hasInput);
}

void Win32ObserverTest::systemStatusSamplerMapsLockAndScreensaver()
{
    FakeDesktop desktop;
    whalepet::platform::Win32SystemStatusSampler sampler(systemReader(desktop));

    EnvSample normal;
    QVERIFY(sampler.sampleSystemStatus(normal));
    QVERIFY(!normal.systemPaused);

    desktop.sessionLocked = true;
    EnvSample locked;
    QVERIFY(sampler.sampleSystemStatus(locked));
    QVERIFY(locked.systemPaused);

    desktop.sessionLocked = false;
    desktop.screensaverRunning = true;
    EnvSample saver;
    QVERIFY(sampler.sampleSystemStatus(saver));
    QVERIFY(saver.systemPaused);

    desktop.systemValid = false;
    EnvSample failed;
    QVERIFY(!sampler.sampleSystemStatus(failed));
}

void Win32ObserverTest::observerAggregatesInjectedReadings()
{
    FakeDesktop desktop;
    desktop.lastInputTickMs = 900;
    desktop.nowTickMs = 1000;

    Win32DesktopObserver observer(foregroundReader(desktop), activityReader(desktop),
                                  systemReader(desktop));
    QVERIFY(observer.available());

    const EnvSample sample = observer.sample(1000);
    QCOMPARE(sample.nowMs, 1000);
    QCOMPARE(QString::fromStdString(sample.appId), QStringLiteral("Code.exe"));
    QCOMPARE(static_cast<int>(sample.category), static_cast<int>(AppCategory::Editor));
    QCOMPARE(sample.idleMs, 100);
    QVERIFY(sample.hasInput);
    QVERIFY(!sample.systemPaused);
    QCOMPARE(sample.dwellMs, 0); // 首次观察不算切换/停留

    // 一个采样周期内切换应用 → 切换数 + 停留归零（验收：切换能在一个周期内反映）
    desktop.processPath = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    desktop.windowTitle = L"某网页";
    const EnvSample switched = observer.sample(2000);
    QCOMPARE(switched.appSwitches, 1);
    QCOMPARE(switched.dwellMs, 0);
    QCOMPARE(static_cast<int>(switched.category), static_cast<int>(AppCategory::Browser));
}

void Win32ObserverTest::observerLifecycleClearsAggregationMemory()
{
    FakeDesktop desktop;
    Win32DesktopObserver observer(foregroundReader(desktop), activityReader(desktop),
                                  systemReader(desktop));

    observer.sample(1000);
    desktop.processPath = L"chrome.exe";
    QCOMPARE(observer.sample(2000).appSwitches, 1);

    // 停止观察：清空跨采样记忆（重新开启后不得把「关闭期间」算成切换与停留）
    observer.setObserving(false);
    const EnvSample restarted = observer.sample(3000);
    QCOMPARE(restarted.appSwitches, 0);
    QCOMPARE(restarted.dwellMs, 0);
}

void Win32ObserverTest::lockedSessionWithoutForegroundIsAfk()
{
    // 锁屏的真实读数特征：前台窗口读不到（appId/title 为空、无输入）+ 会话已锁定。
    // 若判定顺序把「无数据」放在「系统暂停」之前，这里会得到 unknown → 桌宠误以为
    // 「没开感知」，从而完全错过「主人离开了」这一确定信息。
    FakeDesktop desktop;
    desktop.fgValid = false;
    desktop.activityValid = true;
    desktop.lastInputTickMs = 1000;
    desktop.nowTickMs = 60000; // 长时间无输入
    desktop.sessionLocked = true;

    Win32DesktopObserver observer(foregroundReader(desktop), activityReader(desktop),
                                  systemReader(desktop));

    const EnvSample sample = observer.sample(60000);
    QVERIFY(sample.appId.empty());
    QVERIFY(sample.systemPaused);
    QVERIFY2(!sample.hasInput, "锁屏后不应报告输入活跃");

    const whalepet::core::WorkStateRules rules;
    QCOMPARE(static_cast<int>(rules.candidate(sample).state), static_cast<int>(WorkState::Afk));
}

int main(int argc, char *argv[])
{
    // Win32 实现虽不建窗口，但统一走 offscreen，避免任何隐式平台依赖（docs/TESTING.md）
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QCoreApplication app(argc, argv);
    Win32ObserverTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_win32_observer.moc"
