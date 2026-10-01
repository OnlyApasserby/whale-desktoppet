#include <QtTest>
#include <QApplication>

#include "common/PetVisuals.h"
#include "core/LineTable.h"
#include "core/PetTypes.h"
#include "view/PetWindow.h"
#include "view/PoseView.h"
#include "view/SpeechBubble.h"
#include "viewmodel/PetController.h"
#include "viewmodel/PosePresenter.h"

#include <QMenu>
#include <QVector>

// P1 冒烟测试：offscreen 下创建 PetWindow、加载默认立绘、切一次 pose。
// P2 增补：表现批次序号去重 / 特效强制间隔 / 台词流式打断（见 docs/ROADMAP-P2.md）。
// P6+ 增补：小游戏菜单入口的「小游戏…」子菜单结构（文案 / 挂载方式 / 插件项）。
class SmokeTest : public QObject {
    Q_OBJECT
private slots:
    void loadsDefaultPose();
    void createsPetWindow();
    void miniGameMenuIsHoverSubmenu();
    void fxSerialPlaysOnceAndRespectsGap();
    void lineSerialDedupesAndStreamInterrupts();
    void signInInteractionReportsWallClock();
};

// 签到交互广播（P4 内容层「今日签到」每日任务的唯一计量来源）：
//   - reportSignIn() 必须广播一次 Interaction::Signin；
//   - 时间戳必须是**系统墙钟**（Unix 毫秒），不得回退为 elapsed 计时
//     （回归守卫：docs/traps-P4.md TRAP-P4-004）。
void SmokeTest::signInInteractionReportsWallClock()
{
    whalepet::PoseView view;
    whalepet::PetController controller(&view, nullptr);

    QVector<int> types;
    QVector<qint64> stamps;
    QObject::connect(&controller, &whalepet::PetController::interactionOccurred, &view,
                     [&types, &stamps](whalepet::core::Interaction type, qint64 nowMs) {
                         types.append(static_cast<int>(type));
                         stamps.append(nowMs);
                     });

    controller.reportSignIn();

    QCOMPARE(types.size(), 1);
    QCOMPARE(types.first(), static_cast<int>(whalepet::core::Interaction::Signin));
    // 2020-01-01 的 Unix 毫秒下限：进程启动起算的 elapsed 值必然远小于它
    QVERIFY(stamps.first() > 1577836800000LL);
}

void SmokeTest::loadsDefaultPose()
{
    whalepet::PoseView view;
    QVERIFY2(view.setPose(QStringLiteral("idle-cute")),
             "默认立绘 idle-cute 加载失败（检查 webp 插件/资源）");
    QVERIFY(view.hasPose());
    QVERIFY(!view.pixmap().isNull());
}

void SmokeTest::createsPetWindow()
{
    whalepet::PetWindow window;
    QVERIFY(window.poseView() != nullptr);
    QVERIFY(window.poseView()->hasPose());
    window.showPet();
    QVERIFY(window.isVisible());
}

// 小游戏入口：单个「小游戏…」子菜单（悬停展开 / 离开收起 + 键盘导航由 QMenu 原生提供），
// 列表内容由已注册插件动态生成。此处只验证可自动化的结构部分（文案 / 挂载方式 / 插件项）；
// 悬停与键盘交互为人工目视项（见 docs/MINIGAME-INTERFACE.md §8）。
void SmokeTest::miniGameMenuIsHoverSubmenu()
{
    whalepet::PetWindow window;

    QMenu *sub = window.findChild<QMenu *>(QStringLiteral("MiniGameMenu"));
    QVERIFY2(sub != nullptr, "右键菜单缺少「小游戏…」子菜单");
    QCOMPARE(sub->menuAction()->text(), QStringLiteral("小游戏…"));
    // 以 QMenu 子菜单形式挂载：悬停展开、离开收起、上下键 + 左右键导航由此保证
    QCOMPARE(sub->menuAction()->menu(), sub);
    QVERIFY(!sub->isEmpty());

    // 列表项来自插件元数据（menuLabel），且为可直接触发的普通项（非分隔符）
    QCOMPARE(sub->actions().first()->text(), QStringLiteral("扫雷"));
    QVERIFY(sub->actions().first()->isEnabled());
    QVERIFY(!sub->actions().first()->isSeparator());
}

// 特效：同一结果被每 tick 重放时只播一次；500ms 内的新特效被丢弃。
void SmokeTest::fxSerialPlaysOnceAndRespectsGap()
{
    whalepet::PoseView view;
    QVERIFY(view.setPoseImmediate(QStringLiteral("idle-cute")));

    whalepet::PosePresenter presenter(&view, nullptr);

    whalepet::core::PoseResult heart;
    heart.pose = "blush";
    heart.fx = whalepet::core::Fx::Heart;
    heart.ttlMs = 6000;
    heart.fxSerial = 1;

    // 同一个结果重复 present（模拟状态机每 tick 重推缓存态）→ 只迸发一次
    presenter.present(heart);
    presenter.present(heart);
    presenter.present(heart);
    QCOMPARE(view.particleCount(), whalepet::kHeartCount);

    // 500ms 内的新特效 → 丢弃（不排队，也不补播）
    whalepet::core::PoseResult star = heart;
    star.fx = whalepet::core::Fx::Star;
    star.fxSerial = 2;
    presenter.present(star);
    QCOMPARE(view.particleCount(), whalepet::kHeartCount);

    // 冷却期内**再重放**同一序号：仍然什么都不播（丢弃即终局）
    QTest::qWait(whalepet::kFxMinGapMs + 80);
    presenter.present(star);
    QCOMPARE(view.particleCount(), whalepet::kHeartCount);

    // 新事件（新序号）且已过强制间隔 → 正常迸发
    star.fxSerial = 3;
    presenter.present(star);
    QCOMPARE(view.particleCount(), whalepet::kHeartCount + whalepet::kStarCount);

    // 间隔已过但没有新序号 → 仍然不播
    QTest::qWait(whalepet::kFxMinGapMs + 80);
    presenter.present(star);
    QCOMPARE(view.particleCount(), whalepet::kHeartCount + whalepet::kStarCount);
}

// 台词：同一序号只播一次；新序号打断并从头流式输出（多次操作只留最后一次）。
void SmokeTest::lineSerialDedupesAndStreamInterrupts()
{
    whalepet::PoseView view;
    view.show(); // 气泡需要可见的锚点，否则会自行隐藏

    whalepet::SpeechBubble bubble;
    bubble.attachTo(&view);

    whalepet::core::LineTable lines;
    lines.addLine("click.head", QStringLiteral("你好世界").toStdString());
    lines.addLine("click.belly", QStringLiteral("肚子很软").toStdString());

    whalepet::PosePresenter presenter(&view, &bubble);
    presenter.setLineTable(&lines); // rng 为 nullptr → 固定取第一条候选

    whalepet::core::PoseResult click;
    click.pose = "curious";
    click.lineKey = "click.head";
    click.lineSerial = 1;

    presenter.present(click);
    QVERIFY(bubble.bubbleVisible());
    QCOMPARE(bubble.displayedText(), QStringLiteral("你")); // 第 1 个字立即出现

    // 同一序号重放 → 不重播、不换句
    presenter.present(click);
    QCOMPARE(bubble.displayedText(), QStringLiteral("你"));

    // 新序号 → 打断当前流式并从第 1 个字重新开始
    whalepet::core::PoseResult belly = click;
    belly.lineKey = "click.belly";
    belly.lineSerial = 2;
    presenter.present(belly);
    QCOMPARE(bubble.displayedText(), QStringLiteral("肚"));

    // 流式跑完 → 全文可见，且此时才开始计时隐藏
    QTest::qWait(400);
    QCOMPARE(bubble.displayedText(), QStringLiteral("肚子很软"));
    QVERIFY(bubble.streamFinished());
    QVERIFY(bubble.bubbleVisible());

    // 非流式兜底路径（一次显示全文）仍然可用
    bubble.showLine(QStringLiteral("直接显示"), 1000);
    QCOMPARE(bubble.displayedText(), QStringLiteral("直接显示"));
    QVERIFY(bubble.streamFinished());
    QVERIFY(bubble.bubbleVisible());
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    if (QGuiApplication::screens().isEmpty()) {
        qWarning() << "No screen available; skipping smoke test.";
        return 77; // CTest SKIP_RETURN_CODE
    }

    SmokeTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_smoke.moc"
