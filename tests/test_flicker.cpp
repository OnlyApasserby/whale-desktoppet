// EX6 随机闪现（docs/PRESENTATION.md §3.2、docs/SETTINGS.md §2）：
//   * core/RandomFlickerRules 纯逻辑：安全矩形内缩 / 落点恒在安全矩形内 / 最小位移 /
//     全部太近时退化为对角镜像 / 区间非法与空随机源的降级；
//   * viewmodel/FlickerService：随机间隔落在区间内且可复现（固定序列 RNG）/
//     启停幂等 / 门槛不满足时**不发射**（不打扰）/ flickerNow 立即发射。
// 纯逻辑 + QObject 服务，不构造任何 Widget（QTEST_GUILESS_MAIN），无显示环境可跑。

#include <QtTest>

#include "core/IRandom.h"
#include "core/RandomFlickerRules.h"
#include "viewmodel/FlickerService.h"

#include <QSignalSpy>

namespace {

// 常见桌面场景：1920x1040 可用区域、窗口 240x240、安全边距 41（贴边带 21 + 透明留白 20）
constexpr int kAreaW = 1920;
constexpr int kAreaH = 1040;
constexpr int kWinW = 240;
constexpr int kWinH = 240;
constexpr int kMargin = 41;

whalepet::core::FlickerRect sceneSafeRect()
{
    return whalepet::core::flickerSafeRect(0, 0, kAreaW, kAreaH, kMargin);
}

} // namespace

class TestFlicker : public QObject {
    Q_OBJECT
private slots:
    void safeRectInsetsAllSides();
    void safeRectRejectsTooSmallArea();
    void pickedTargetStaysInsideSafeRect();
    void pickedTargetHonoursMinimumDistance();
    void pickedTargetFallsBackToMirrorWhenAllAttemptsTooClose();
    void pickedTargetRejectsNullRandomAndTinyScreen();
    void delayStaysWithinRangeAndDegradesSafely();
    void serviceStartStopIsIdempotent();
    void serviceEmitsOnlyWhenGateAllows();
    void serviceFlickerNowEmitsImmediately();
};

void TestFlicker::safeRectInsetsAllSides()
{
    const whalepet::core::FlickerRect safe = sceneSafeRect();
    QCOMPARE(safe.x, kMargin);
    QCOMPARE(safe.y, kMargin);
    QCOMPARE(safe.w, kAreaW - 2 * kMargin);
    QCOMPARE(safe.h, kAreaH - 2 * kMargin);
    QVERIFY(!safe.isEmpty());

    // 边距为 0 / 负数：等价于不内缩
    const whalepet::core::FlickerRect zero = whalepet::core::flickerSafeRect(0, 0, 100, 100, 0);
    QCOMPARE(zero.x, 0);
    QCOMPARE(zero.w, 100);
    const whalepet::core::FlickerRect negative =
        whalepet::core::flickerSafeRect(0, 0, 100, 100, -50);
    QCOMPARE(negative.w, 100);
}

void TestFlicker::safeRectRejectsTooSmallArea()
{
    // 屏幕 100x100、边距 41 → 内缩后 18x18，容不下 240x240 的窗口
    const whalepet::core::FlickerRect safe = whalepet::core::flickerSafeRect(0, 0, 100, 100, kMargin);
    QCOMPARE(safe.w, 18);
    QVERIFY(!whalepet::core::flickerPointIsSafe(safe.x, safe.y, kWinW, kWinH, safe));

    // 恰好放得下（宽 = 窗口宽）：唯一合法位置即安全矩形左上角，必须判为安全
    const whalepet::core::FlickerRect exact =
        whalepet::core::flickerSafeRect(0, 0, kWinW + 2 * kMargin, kWinH + 2 * kMargin, kMargin);
    QCOMPARE(exact.w, kWinW);
    QVERIFY(whalepet::core::flickerPointIsSafe(exact.x, exact.y, kWinW, kWinH, exact));
}

void TestFlicker::pickedTargetStaysInsideSafeRect()
{
    const whalepet::core::FlickerRect safe = sceneSafeRect();
    whalepet::core::SystemRandom rng(20261010); // 固定种子：结论可复现

    for (int i = 0; i < 200; ++i) {
        const whalepet::core::FlickerTarget target =
            whalepet::core::pickFlickerTarget(960, 520, kWinW, kWinH, safe, 120, 12, &rng);
        QVERIFY(target.ok);
        // 关键不变量：落点（含退化路径）**恒**完整落在安全矩形内
        QVERIFY2(whalepet::core::flickerPointIsSafe(target.pos.x, target.pos.y, kWinW, kWinH, safe),
                 qPrintable(QStringLiteral("越界落点: %1,%2").arg(target.pos.x).arg(target.pos.y)));
    }
}

void TestFlicker::pickedTargetHonoursMinimumDistance()
{
    const whalepet::core::FlickerRect safe = sceneSafeRect();
    // 固定序列：0.9 → 落在安全矩形靠右下角，与 (1000, 500) 的位移必然超过 120
    whalepet::core::ScriptedRandom rng({ 0.9, 0.9 });
    const whalepet::core::FlickerTarget target =
        whalepet::core::pickFlickerTarget(1000, 500, kWinW, kWinH, safe, 120, 12, &rng);
    QVERIFY(target.ok);
    QVERIFY(whalepet::core::flickerPointIsSafe(target.pos.x, target.pos.y, kWinW, kWinH, safe));

    const int dx = target.pos.x > 1000 ? target.pos.x - 1000 : 1000 - target.pos.x;
    const int dy = target.pos.y > 500 ? target.pos.y - 500 : 500 - target.pos.y;
    QVERIFY(dx >= 120 || dy >= 120);
}

void TestFlicker::pickedTargetFallsBackToMirrorWhenAllAttemptsTooClose()
{
    const whalepet::core::FlickerRect safe = sceneSafeRect();
    const int maxX = safe.x + safe.w - kWinW; // 1639
    const int maxY = safe.y + safe.h - kWinH; // 759

    // 随机序列恒为 0 → 每次都抽到安全矩形左上角，与当前位置完全重合 → 12 次尝试全部太近
    whalepet::core::ScriptedRandom rng({ 0.0, 0.0 });
    const whalepet::core::FlickerTarget target =
        whalepet::core::pickFlickerTarget(safe.x, safe.y, kWinW, kWinH, safe, 120, 12, &rng);

    QVERIFY(target.ok);
    // 退化路径 = 对角镜像：贴到安全矩形右下角（仍完整在内，且不会停在原地）
    QCOMPARE(target.pos.x, maxX);
    QCOMPARE(target.pos.y, maxY);
    QVERIFY(whalepet::core::flickerPointIsSafe(target.pos.x, target.pos.y, kWinW, kWinH, safe));
    QVERIFY(!(target.pos.x == safe.x && target.pos.y == safe.y));

    // 尝试次数为 0 时不得崩溃（按 1 次处理，仍走同一条路径）
    const whalepet::core::FlickerTarget zeroAttempts =
        whalepet::core::pickFlickerTarget(safe.x, safe.y, kWinW, kWinH, safe, 120, 0, &rng);
    QVERIFY(zeroAttempts.ok);
}

void TestFlicker::pickedTargetRejectsNullRandomAndTinyScreen()
{
    const whalepet::core::FlickerRect safe = sceneSafeRect();
    // 空随机源：放弃本次闪现（不猜坐标）
    QVERIFY(!whalepet::core::pickFlickerTarget(960, 520, kWinW, kWinH, safe, 120, 12, nullptr).ok);

    // 屏幕容不下窗口：放弃
    whalepet::core::SystemRandom rng(1);
    const whalepet::core::FlickerRect tiny = whalepet::core::flickerSafeRect(0, 0, 100, 100, kMargin);
    QVERIFY(!whalepet::core::pickFlickerTarget(0, 0, kWinW, kWinH, tiny, 120, 12, &rng).ok);
    // 窗口尺寸非法（0）：放弃
    QVERIFY(!whalepet::core::pickFlickerTarget(0, 0, 0, 0, safe, 120, 12, &rng).ok);
}

void TestFlicker::delayStaysWithinRangeAndDegradesSafely()
{
    // 固定序列决定取值：0.5 → int(0.5 * 11) = 5 → 10 + 5 = 15
    whalepet::core::ScriptedRandom mid({ 0.5 });
    QCOMPARE(whalepet::core::nextFlickerDelayMs(&mid, 10, 20), 15);

    whalepet::core::ScriptedRandom low({ 0.0 });
    QCOMPARE(whalepet::core::nextFlickerDelayMs(&low, 10, 20), 10);

    // 空随机源 → 下限（绝不退化为 0 造成忙循环）
    QCOMPARE(whalepet::core::nextFlickerDelayMs(nullptr, 10, 20), 10);

    // 区间反转 / 负数：夹取为合法区间（min == max）
    whalepet::core::ScriptedRandom any({ 0.9 });
    QCOMPARE(whalepet::core::nextFlickerDelayMs(&any, 30, 5), 30);
    QCOMPARE(whalepet::core::nextFlickerDelayMs(&any, -5, -1), 0);
    // 生产区间：抽样恒落在 [kMinIntervalMs, kMaxIntervalMs] 内
    whalepet::core::SystemRandom rng(7);
    for (int i = 0; i < 100; ++i) {
        const int delay = whalepet::core::nextFlickerDelayMs(
            &rng, whalepet::viewmodel::FlickerService::kMinIntervalMs,
            whalepet::viewmodel::FlickerService::kMaxIntervalMs);
        QVERIFY(delay >= whalepet::viewmodel::FlickerService::kMinIntervalMs);
        QVERIFY(delay <= whalepet::viewmodel::FlickerService::kMaxIntervalMs);
    }
}

void TestFlicker::serviceStartStopIsIdempotent()
{
    whalepet::viewmodel::FlickerService service;
    QVERIFY(!service.running());
    QCOMPARE(service.lastDelayMs(), 0);

    service.start();
    service.start(); // 幂等：重复 start 不重建定时器、不叠加
    QVERIFY(service.running());
    QVERIFY(service.lastDelayMs() > 0);

    service.stop();
    service.stop();
    QVERIFY(!service.running());
    QCOMPARE(service.lastDelayMs(), 0);

    // 非法区间被忽略（保持原值）
    service.setIntervalRange(0, 100);
    QCOMPARE(service.minIntervalMs(), whalepet::viewmodel::FlickerService::kMinIntervalMs);
    service.setIntervalRange(500, 100);
    QCOMPARE(service.maxIntervalMs(), whalepet::viewmodel::FlickerService::kMaxIntervalMs);
}

void TestFlicker::serviceEmitsOnlyWhenGateAllows()
{
    whalepet::viewmodel::FlickerService service;
    whalepet::core::ScriptedRandom rng({ 0.0 }); // 间隔恒为下限
    service.setRandom(&rng);
    service.setIntervalRange(10, 20);

    bool allowed = false;
    service.setCanFlicker([&allowed]() { return allowed; });

    QSignalSpy spy(&service, &whalepet::viewmodel::FlickerService::flickerRequested);
    service.start();
    QVERIFY(service.running());
    QCOMPARE(service.lastDelayMs(), 10);

    // 门槛为假：一轮接一轮地被挡下，但一次都不发射（不打扰）
    QTest::qWait(150);
    QCOMPARE(spy.count(), 0);
    QVERIFY(service.running()); // 被挡下不等于停表

    allowed = true;
    QTest::qWait(300);
    QVERIFY2(spy.count() >= 1, "门槛放行后必须发射 flickerRequested");
    service.stop();
}

void TestFlicker::serviceFlickerNowEmitsImmediately()
{
    whalepet::viewmodel::FlickerService service;
    QSignalSpy spy(&service, &whalepet::viewmodel::FlickerService::flickerRequested);

    // 未启动也允许主动触发（跳过等待与门槛，语义由调用方负责）
    service.flickerNow();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!service.running());
}

QTEST_GUILESS_MAIN(TestFlicker)
#include "test_flicker.moc"
