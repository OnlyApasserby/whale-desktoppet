#include <QtTest>

#include <QCoreApplication>

#include "core/WorkState.h"
#include "core/WorkStateRules.h"
#include "platform/DesktopObserver.h"
#include "platform/EmptyDesktopObserver.h"

#include <string>

// P7 感知层骨架（docs/PLUGIN-ARCHITECTURE.md §3/§6.1、docs/ROADMAP-P7.md P7.0）。
//
// 覆盖点：
//   * 空实现恒「无数据」且 available()==false（启用感知后不会被误认为「用户一直空闲」）；
//   * 组合观察者把子采样器拼成一份 EnvSample，并正确计算只有它能算的
//     category / appSwitches / dwellMs / 滚动窗口累计；
//   * 子采样器失败时**不得伪造数据**；
//   * 接口可注入替身（本文件全部用假采样器驱动，不依赖任何真实采集）。

using whalepet::core::EnvSample;

namespace {

class FakeForeground : public whalepet::platform::IForegroundSampler {
public:
    bool available() const override { return m_available; }
    bool sampleForeground(EnvSample &out) override
    {
        if (!m_available || m_fail) {
            return false;
        }
        out.appId = m_appId;
        out.windowTitle = m_title;
        return true;
    }

    bool m_available = true;
    bool m_fail = false;
    std::string m_appId = "Code.exe";
    std::string m_title = "main.cpp - Visual Studio Code";
};

class FakeActivity : public whalepet::platform::IActivitySampler {
public:
    bool available() const override { return m_available; }
    bool sampleActivity(EnvSample &out) override
    {
        if (!m_available) {
            return false;
        }
        out.hasInput = m_hasInput;
        out.idleMs = m_idleMs;
        out.inputEvents = m_eventsDelta; // 约定：自上次采样以来的增量
        return true;
    }

    bool m_available = true;
    bool m_hasInput = true;
    std::int64_t m_idleMs = 0;
    int m_eventsDelta = 5;
};

class FakeSystemStatus : public whalepet::platform::ISystemStatusSampler {
public:
    bool available() const override { return true; }
    bool sampleSystemStatus(EnvSample &out) override
    {
        out.systemPaused = m_paused;
        return true;
    }

    bool m_paused = false;
};

} // namespace

class PlatformSkeletonTest : public QObject {
    Q_OBJECT
private slots:
    void emptyObserverReportsNoData();
    void compositeAggregatesSamplers();
    void compositeCountsAppSwitchesAndDwell();
    void compositeResetsWindowAfterWindowMs();
    void compositeWithoutSamplersIsEmpty();
    void compositePropagatesSystemPaused();
    void failedSamplerDoesNotFakeData();
};

void PlatformSkeletonTest::emptyObserverReportsNoData()
{
    whalepet::platform::EmptyDesktopObserver observer;
    QVERIFY2(!observer.available(), "空实现必须自报不可用（宿主据此可如实上报 env.available=false）");

    const EnvSample sample = observer.sample(12345);
    QVERIFY(sample.isEmpty());
    QCOMPARE(sample.nowMs, 12345);
    QCOMPARE(static_cast<int>(sample.category), static_cast<int>(whalepet::core::AppCategory::Unknown));
    QVERIFY(sample.appId.empty());
    QVERIFY(sample.windowTitle.empty());
    QCOMPARE(sample.inputEvents, 0);
    QVERIFY(!sample.systemPaused);
}

void PlatformSkeletonTest::compositeAggregatesSamplers()
{
    FakeForeground foreground;
    FakeActivity activity;
    activity.m_eventsDelta = 7;

    whalepet::platform::CompositeDesktopObserver observer;
    observer.setForegroundSampler(&foreground);
    observer.setActivitySampler(&activity);
    QVERIFY(observer.available());

    const EnvSample sample = observer.sample(1000);
    QCOMPARE(QString::fromStdString(sample.appId), QStringLiteral("Code.exe"));
    QCOMPARE(QString::fromStdString(sample.windowTitle),
             QStringLiteral("main.cpp - Visual Studio Code"));
    // category 由组合层经 classifyApp 归一化（core 判定规则因此保持纯函数）
    QCOMPARE(static_cast<int>(sample.category), static_cast<int>(whalepet::core::AppCategory::Editor));
    QCOMPARE(sample.inputEvents, 7);
    QVERIFY(sample.hasInput);
    QCOMPARE(sample.nowMs, 1000);
}

void PlatformSkeletonTest::compositeCountsAppSwitchesAndDwell()
{
    FakeForeground foreground;
    FakeActivity activity;
    activity.m_eventsDelta = 3;

    whalepet::platform::CompositeDesktopObserver observer;
    observer.setForegroundSampler(&foreground);
    observer.setActivitySampler(&activity);

    const EnvSample first = observer.sample(1000);
    QCOMPARE(first.appSwitches, 0); // 首次观察不算切换
    QCOMPARE(first.dwellMs, 0);

    const EnvSample same = observer.sample(3000);
    QCOMPARE(same.appSwitches, 0);
    QCOMPARE(same.dwellMs, 2000);
    QCOMPARE(same.inputEvents, 6); // 窗口内累计（3 + 3）

    foreground.m_appId = "chrome.exe";
    foreground.m_title = "某网页";
    const EnvSample switched = observer.sample(4000);
    QCOMPARE(switched.appSwitches, 1);
    QCOMPARE(switched.dwellMs, 0);
    QCOMPARE(static_cast<int>(switched.category), static_cast<int>(whalepet::core::AppCategory::Browser));

    foreground.m_appId = "Code.exe";
    const EnvSample back = observer.sample(5000);
    QCOMPARE(back.appSwitches, 2);
    QCOMPARE(back.dwellMs, 0);
}

void PlatformSkeletonTest::compositeResetsWindowAfterWindowMs()
{
    FakeForeground foreground;
    FakeActivity activity;
    activity.m_eventsDelta = 5;

    whalepet::platform::CompositeDesktopObserver observer;
    observer.setForegroundSampler(&foreground);
    observer.setActivitySampler(&activity);

    QCOMPARE(observer.sample(1000).inputEvents, 5);
    QCOMPARE(observer.sample(2000).inputEvents, 10);

    // 超过窗口长度 → 窗口重新开始累计（appSwitches 同样清零）
    const EnvSample fresh = observer.sample(1000 + whalepet::core::kWorkWindowMs);
    QCOMPARE(fresh.inputEvents, 5);
    QCOMPARE(fresh.appSwitches, 0);
}

void PlatformSkeletonTest::compositeWithoutSamplersIsEmpty()
{
    whalepet::platform::CompositeDesktopObserver observer;
    QVERIFY(!observer.available());

    const EnvSample sample = observer.sample(500);
    QVERIFY2(sample.isEmpty(), "没有任何子采样器时必须自报「无数据」，不得伪造");
    QCOMPARE(sample.nowMs, 500);
}

void PlatformSkeletonTest::compositePropagatesSystemPaused()
{
    FakeForeground foreground;
    FakeActivity activity;
    FakeSystemStatus status;
    status.m_paused = true;

    whalepet::platform::CompositeDesktopObserver observer;
    observer.setForegroundSampler(&foreground);
    observer.setActivitySampler(&activity);
    observer.setSystemStatusSampler(&status);
    QVERIFY(observer.available());

    const EnvSample sample = observer.sample(1000);
    QVERIFY(sample.systemPaused);
    // 会话锁定 → core 判定为 Afk（人在不在电脑前是确定的）
    const whalepet::core::WorkStateRules rules;
    QCOMPARE(static_cast<int>(rules.candidate(sample).state),
             static_cast<int>(whalepet::core::WorkState::Afk));
}

void PlatformSkeletonTest::failedSamplerDoesNotFakeData()
{
    FakeForeground foreground;
    foreground.m_fail = true; // 自报可用但采集失败
    FakeActivity activity;

    whalepet::platform::CompositeDesktopObserver observer;
    observer.setForegroundSampler(&foreground);
    observer.setActivitySampler(&activity);

    const EnvSample sample = observer.sample(1000);
    QVERIFY2(sample.appId.empty(), "采集失败时不得保留/伪造前台应用");
    QVERIFY2(sample.windowTitle.empty(), "采集失败时不得保留/伪造窗口标题");
    QCOMPARE(static_cast<int>(sample.category),
             static_cast<int>(whalepet::core::AppCategory::Unknown));
    // 活跃度采样器仍正常工作 → 数据不因另一个维度失败而整体失效
    QVERIFY(sample.hasInput);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    PlatformSkeletonTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_platform_skeleton.moc"
