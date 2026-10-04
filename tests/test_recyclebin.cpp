#include <QtTest>

#include "viewmodel/RecycleBinService.h"

using namespace whalepet::viewmodel;

// 回收站清理提醒（2026-10-04 立绘激活 18）：
// 只验证服务自身的确定性行为（查询不崩、启停切换、start 立即检查一次）。
// 不断言回收站「必须非空」（依赖真实系统状态），非 Windows / 查询不可用时按 skip 处理。
class TestRecycleBin : public QObject {
    Q_OBJECT

private slots:
    void queryDoesNotCrash();
    void startStopTogglesTimer();
    void startEmitsInitialCheck();
};

void TestRecycleBin::queryDoesNotCrash()
{
    RecycleBinService svc;
    const RecycleBinInfo info = svc.query();
    // 字段恒为非负（即使不可用也保持默认 0）
    QVERIFY(info.itemCount >= 0);
    QVERIFY(info.sizeBytes >= 0);

#if defined(Q_OS_WIN)
    if (!info.available) {
        QSKIP("SHQueryRecycleBin 在当前环境不可用，跳过可用性断言");
    }
    QVERIFY(info.available);
#else
    // 非 Windows：无对应 API，必须诚实返回「未知」
    QVERIFY(!info.available);
#endif
}

void TestRecycleBin::startStopTogglesTimer()
{
    RecycleBinService svc;
    QVERIFY(!svc.running());
    svc.start();
    QVERIFY(svc.running());
    svc.stop();
    QVERIFY(!svc.running());
}

void TestRecycleBin::startEmitsInitialCheck()
{
    RecycleBinService svc;
    QSignalSpy spy(&svc, &RecycleBinService::checked);
    svc.start();
    QCOMPARE(spy.count(), 1); // start() 立即检查一次（随后进入随机周期）
    svc.stop();
}

QTEST_GUILESS_MAIN(TestRecycleBin)
#include "test_recyclebin.moc"
