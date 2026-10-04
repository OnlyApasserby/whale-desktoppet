#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QtTest>

#include "common/PetVisuals.h"
#include "core/IdleRules.h"
#include "core/PetStateMachine.h"
#include "core/PetTypes.h"
#include "view/AssetsResource.h"
#include "view/PoseView.h"
#include "view/SpeechBubble.h"
#include "viewmodel/PetController.h"

// P6 设置项「生效」测试：
//   - night_quiet   → PetStateMachine::setNightQuiet
//   - pose_size     → PoseView::setDisplaySize
//   - particles_enabled → PoseView::setParticlesEnabled
//   - drag_inertia  → PoseView::setDragInertiaEnabled
//   - bubble_enabled → SpeechBubble::setSuppressed
// 设置的持久化（含 json_ext 扩展键往返）在 test_database 覆盖。
//
// 2026-10-04 追加（台词气泡圆角背景框）：
//   - bubbleDrawsRoundedBackground  → 用真实全局样式表做像素级验证（有底 / 圆角 / 框外透明）
//   - bubbleAvoidsCoveringAnchor    → 背景框始终贴在立绘之外（不遮挡立绘）

class SettingsTest : public QObject {
    Q_OBJECT
private slots:
    void nightQuietGatesProactiveSpeech();
    void poseViewDisplaySizeAndToggles();
    void bubbleSuppressedHidesLine();
    void bubbleDrawsRoundedBackground();
    void bubbleAvoidsCoveringAnchor();
    void lateNightWeakIgnoresMouseInput();
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

// 圆角矩形背景框（2026-10-04）
//
// 气泡是「无边框 + WA_TranslucentBackground」的 QWidget 子类顶层窗口：Qt **不会**自动把
// 样式表的 background-color / border / border-radius 画上去，必须在 paintEvent 里转发
// `QStyle::PE_Widget`（实测：不转发时中心像素 alpha = 0，即气泡只有字没有底）。
// 本用例用**真实全局样式表**（default.qss + project.qss）做像素级验证：
//   - 中心     ：完全不透明 → 黑底已绘制
//   - (0,0)    ：完全透明   → 圆角之外保持透明（既不糊成方角，也不遮挡桌面 / 立绘）
//   - 圆角内(2,2)：半透明   → border-radius 的抗锯齿边缘
void SettingsTest::bubbleDrawsRoundedBackground()
{
    // 与 src/app/main.cpp 同源：default.qss 原样 + project.qss（项目控件规则）
    Q_INIT_RESOURCE(qt_ui);
    QString sheet;
    const char *files[] = {":/qt-ui/default.qss", ":/qt-ui/project.qss"};
    for (const char *path : files) {
        QFile file(QString::fromLatin1(path));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), path);
        sheet += QString::fromUtf8(file.readAll());
    }
    QVERIFY2(sheet.contains(QStringLiteral("#SpeechBubble")), "project.qss 缺少 #SpeechBubble 规则");
    qApp->setStyleSheet(sheet); // 全局样式表：唯一入口（不在控件上局部覆盖）

    whalepet::PoseView view;
    view.show(); // 气泡需要可见锚点
    whalepet::SpeechBubble bubble;
    bubble.attachTo(&view);
    bubble.showLine(QStringLiteral("台词背景框像素验证")); // 非流式：立即按整句排版
    QApplication::processEvents();

    const QImage img = bubble.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY2(!img.isNull(), "气泡 grab 失败");
    QVERIFY2(img.width() > 20 && img.height() > 20, "气泡尺寸异常");

    const int centerAlpha = qAlpha(img.pixel(img.width() / 2, img.height() / 2));
    const int outsideAlpha = qAlpha(img.pixel(0, 0));
    const int cornerAlpha = qAlpha(img.pixel(2, 2));

    QCOMPARE(centerAlpha, 255); // 有底：背景框已绘制
    QCOMPARE(outsideAlpha, 0);  // 圆角之外透明
    QVERIFY2(cornerAlpha < 255,
             qPrintable(QStringLiteral("圆角未生效：(2,2) alpha=%1（应为抗锯齿边缘）").arg(cornerAlpha)));

    qApp->setStyleSheet(QString()); // 还原，避免影响后续用例
}

// 背景框不遮挡立绘（2026-10-04）
//
// 气泡是**独立顶层窗口**，无法用「降低 z 序」避让（降 z 会被立绘窗口 / 桌面盖住），
// 因此统一用「偏移方向」：气泡整体贴在立绘窗口之外（上方优先，上方不足翻到下方），
// 与立绘矩形**永不相交**。本用例覆盖「常规位置（上方）」与「贴屏幕顶端（翻下方）」两种。
void SettingsTest::bubbleAvoidsCoveringAnchor()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "无可用屏幕");
    const QRect area = screen->availableGeometry();
    if (area.width() < 400 || area.height() < 400) {
        QSKIP("屏幕可用区域过小，无法覆盖上下两种摆放");
    }

    whalepet::PoseView anchor; // 240x240，与桌宠窗口同尺寸
    whalepet::SpeechBubble bubble;
    bubble.attachTo(&anchor);

    // 1) 常规位置：气泡挂在立绘上方，二者不相交
    anchor.move(area.left() + 40, area.top() + area.height() / 2);
    anchor.show();
    QApplication::processEvents();
    bubble.showLine(QStringLiteral("这是一条比较长的台词，用来触发换行并把气泡撑高一些"));

    const QRect anchorRect(anchor.mapToGlobal(QPoint(0, 0)), anchor.size());
    QRect bubbleRect(bubble.pos(), bubble.size());
    QVERIFY2(!bubbleRect.intersects(anchorRect),
             qPrintable(QStringLiteral("气泡压住立绘: bubble=%1,%2 %3x%4 anchor=%5,%6 %7x%8")
                            .arg(bubbleRect.x())
                            .arg(bubbleRect.y())
                            .arg(bubbleRect.width())
                            .arg(bubbleRect.height())
                            .arg(anchorRect.x())
                            .arg(anchorRect.y())
                            .arg(anchorRect.width())
                            .arg(anchorRect.height())));
    QVERIFY2(bubbleRect.bottom() < anchorRect.top(), "气泡应贴在立绘上方");
    QCOMPARE(anchorRect.top() - bubbleRect.bottom(), whalepet::kBubbleGapPx + 1); // bottom 为闭端点

    // 2) 立绘贴到屏幕顶端 → 上方放不下，自动翻到下方，仍不重叠
    anchor.move(area.left() + 40, area.top());
    QApplication::processEvents();
    bubble.reposition();

    const QRect movedAnchorRect(anchor.mapToGlobal(QPoint(0, 0)), anchor.size());
    bubbleRect = QRect(bubble.pos(), bubble.size());
    QVERIFY2(!bubbleRect.intersects(movedAnchorRect), "翻到下方后仍压住立绘");
    QVERIFY2(bubbleRect.top() > movedAnchorRect.bottom(), "气泡应改挂在立绘下方");
}

// 深夜虚弱：角色不再响应鼠标事件（2026-10-04）
//
// 触发条件：深夜独立阶段点击累计 ≥ kLateNightWeakClickCount → 虚弱窗口 20s。
// 窗口内 **一切鼠标交互被拒绝**：handleClick / handleDragBegin / handleDragEnd /
// handleMenuAction 全部返回 false，且不换立绘、不播台词、不广播 interactionOccurred（= 不涨养成）。
// PetWindow 依据同一返回值决定「不移动窗口、不播点击反馈动画、不接受文件投喂」。
void SettingsTest::lateNightWeakIgnoresMouseInput()
{
    using namespace whalepet::core;

    whalepetInitAssetsResource(); // 台词/立绘资源（缺失只降级，不影响本用例断言）

    whalepet::PoseView view;
    view.show();
    whalepet::SpeechBubble bubble;
    bubble.attachTo(&view);
    whalepet::PetController controller(&view, &bubble);

    // 直接推进状态机到深夜（控制器时间基准是系统墙钟；本用例不需要启动定时器）
    controller.stateMachine().handle(Event::clock(23, QDateTime::currentMSecsSinceEpoch()));
    QCOMPARE(static_cast<int>(controller.stateMachine().daySlot()),
             static_cast<int>(DaySlot::LateNight));

    int interactions = 0;
    QObject::connect(&controller, &whalepet::PetController::interactionOccurred,
                     [&interactions](whalepet::core::Interaction, qint64) { ++interactions; });

    // 累计到阈值 → 进入虚弱（前 9 次仍是正常点击反馈）
    for (int i = 0; i < kLateNightWeakClickCount; ++i) {
        controller.handleClick(Zone::Head);
    }
    QVERIFY(controller.lateNightWeak());
    QCOMPARE(QString::fromStdString(controller.stateMachine().current().pose),
             QString::fromStdString(kLateNightWeakPose));

    // 虚弱期间：鼠标交互一律被拒绝，且不产生任何副作用
    const int interactionsBefore = interactions;
    const int lineSerialBefore = controller.stateMachine().current().lineSerial;
    QVERIFY(!controller.handleClick(Zone::Belly));
    QVERIFY(!controller.handleClick(Zone::Tail));
    QVERIFY(!controller.handleDragBegin());
    QVERIFY(!controller.handleDragEnd());
    QVERIFY(!controller.handleMenuAction(EventType::Feed));
    QCOMPARE(interactions, interactionsBefore); // 不广播交互（不涨养成 / 不计任务）
    QCOMPARE(controller.stateMachine().current().lineSerial, lineSerialBefore); // 不产生新台词
    QCOMPARE(QString::fromStdString(controller.stateMachine().current().pose),
             QString::fromStdString(kLateNightWeakPose)); // 立绘不变
    QVERIFY(!controller.stateMachine().dragging());
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
