#include <QApplication>
#include <QGuiApplication>
#include <QtTest>

#include "common/PetVisuals.h"
#include "core/PetStateMachine.h"
#include "core/PetTypes.h"
#include "view/PoseView.h"
#include "view/SpeechBubble.h"

// P6 设置项「生效」测试：
//   - night_quiet   → PetStateMachine::setNightQuiet
//   - pose_size     → PoseView::setDisplaySize
//   - particles_enabled → PoseView::setParticlesEnabled
//   - drag_inertia  → PoseView::setDragInertiaEnabled
//   - bubble_enabled → SpeechBubble::setSuppressed
// 设置的持久化（含 json_ext 扩展键往返）在 test_database 覆盖。

class SettingsTest : public QObject {
    Q_OBJECT
private slots:
    void nightQuietGatesProactiveSpeech();
    void poseViewDisplaySizeAndToggles();
    void bubbleSuppressedHidesLine();
};

void SettingsTest::nightQuietGatesProactiveSpeech()
{
    using namespace whalepet::core;
    PetStateMachine sm(nullptr);

    sm.handle(Event::clock(23, 1000)); // 进入深夜时段

    // 默认 night_quiet = true → 深夜主动发言被静默
    const PoseResult quiet =
        sm.speak("", "greet.evening", 0, Event::simple(EventType::Tick, 2000), true);
    QVERIFY(quiet.lineKey.empty());

    // 关闭 night_quiet → 同一时段可主动发言
    sm.setNightQuiet(false);
    const PoseResult loud =
        sm.speak("", "greet.evening", 0, Event::simple(EventType::Tick, 3000), true);
    QCOMPARE(QString::fromStdString(loud.lineKey), QStringLiteral("greet.evening"));

    // 用户主动交互（proactive=false）在深夜始终豁免，与开关无关
    sm.setNightQuiet(true);
    const PoseResult userHit =
        sm.speak("", "meme.omg", 0, Event::simple(EventType::Tick, 4000), false);
    QCOMPARE(QString::fromStdString(userHit.lineKey), QStringLiteral("meme.omg"));
}

void SettingsTest::poseViewDisplaySizeAndToggles()
{
    whalepet::PoseView view;
    QVERIFY(view.setPoseImmediate(QStringLiteral("idle-cute")));
    QCOMPARE(view.displaySize(), whalepet::kPetDisplaySize);

    // 尺寸变更：画布随之变化，且当前立绘按新尺寸重采样（仍有图）
    view.setDisplaySize(120);
    QCOMPARE(view.displaySize(), 120);
    QCOMPARE(view.width(), 120 + whalepet::kPetMargin * 2);
    QVERIFY(view.hasPose());
    QVERIFY(!view.pixmap().isNull());

    // 越界 → 夹取到 [kMinDisplaySize, kMaxDisplaySize]
    view.setDisplaySize(99999);
    QCOMPARE(view.displaySize(), whalepet::PoseView::kMaxDisplaySize);
    view.setDisplaySize(1);
    QCOMPARE(view.displaySize(), whalepet::PoseView::kMinDisplaySize);

    // 粒子开关：开启时正常迸发；关闭时清空存量且不再生成
    view.setParticlesEnabled(true);
    view.playFx(whalepet::core::Fx::Star);
    QCOMPARE(view.particleCount(), whalepet::kStarCount);

    view.setParticlesEnabled(false);
    QCOMPARE(view.particleCount(), 0); // 关闭时清空存量
    view.playFx(whalepet::core::Fx::Heart);
    QCOMPARE(view.particleCount(), 0); // 关闭后不再生成

    // 拖拽惯性开关：状态可查询（endDrag 在关闭时不做滑行，且不崩溃）
    view.setDragInertiaEnabled(false);
    QVERIFY(!view.dragInertiaEnabled());
    view.endDrag(QPointF(5000.0, 0.0));
    view.setDragInertiaEnabled(true);
    QVERIFY(view.dragInertiaEnabled());
}

void SettingsTest::bubbleSuppressedHidesLine()
{
    whalepet::PoseView view;
    view.show(); // 气泡需要一个可见锚点，否则会自行隐藏

    whalepet::SpeechBubble bubble;
    bubble.attachTo(&view);

    // bubble_enabled = false → 不显示任何台词
    bubble.setSuppressed(true);
    QVERIFY(bubble.suppressed());
    bubble.startStream(QStringLiteral("你好世界"));
    QVERIFY(!bubble.bubbleVisible());
    QVERIFY(bubble.displayedText().isEmpty());

    // 重新开启 → 台词恢复（流式从第 1 个字开始）
    bubble.setSuppressed(false);
    bubble.startStream(QStringLiteral("你好世界"));
    QVERIFY(bubble.bubbleVisible());
    QCOMPARE(bubble.displayedText(), QStringLiteral("你"));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    if (QGuiApplication::screens().isEmpty()) {
        qWarning() << "No screen available; skipping settings test.";
        return 77; // CTest SKIP_RETURN_CODE
    }

    SettingsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_settings.moc"
