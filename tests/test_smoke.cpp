#include <QtTest>
#include <QApplication>

#include "common/PetVisuals.h"
#include "core/DesktopEdge.h"
#include "core/LineTable.h"
#include "core/PetTypes.h"
#include "core/RobotKitten.h"
#include "minigame/kitten/KittenView.h"
#include "minigame/minesweeper/MinesweeperView.h"
#include "view/PetWindow.h"
#include "view/PoseView.h"
#include "view/SpeechBubble.h"
#include "viewmodel/PetController.h"
#include "viewmodel/PosePresenter.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMimeData>
#include <QMoveEvent>
#include <QPair>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QUrl>
#include <QVector>

#include <string>

// P1 冒烟测试：offscreen 下创建 PetWindow、加载默认立绘、切一次 pose。
// P2 增补：表现批次序号去重 / 特效强制间隔 / 台词流式打断（见 docs/ROADMAP-P2.md）。
// P6+ 增补：小游戏菜单入口的「小游戏…」子菜单结构（文案 / 挂载方式 / 插件项）。
class SmokeTest : public QObject {
    Q_OBJECT
private slots:
    void loadsDefaultPose();
    void createsPetWindow();
    void petWindowStartsCenteredOnPrimaryScreen();
    void dropAcceptsLocalFilesFromAnyDriveRootOrFirstLevelDir();
    void miniGameMenuIsHoverSubmenu();
    void kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty();
    void kittenSceneChangeRebuildsGrid();
    void minesweeperViewRestartKeepsBoardSized();
    void fxSerialPlaysOnceAndRespectsGap();
    void lineSerialDedupesAndStreamInterrupts();
    void signInInteractionReportsWallClock();
    // 桌面四边框贴边（docs/PRESENTATION.md §3.1）
    void desktopEdgeDetectionPicksNearestBorder();
    void poseViewAttachesPeekPoseToDesktopEdge();
    void poseViewKeepsEdgePoseWhileDragging();
    void petWindowReportsDesktopEdgeWhenMovedToBorder();
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

// 启动居中回归（实测 Bug：分辨率调整 / 监视器变更后，恢复的旧坐标可能落在屏幕外，
// 立绘「看不见」）。要求：每次启动都按**当前**主屏可用区域把窗口放到正中央。
void SmokeTest::petWindowStartsCenteredOnPrimaryScreen()
{
    whalepet::PetWindow window;
    window.showPet();
    QApplication::processEvents();

    QScreen *screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "无可用屏幕");

    const QRect area = screen->availableGeometry();
    const QPoint expected(area.center().x() - window.width() / 2,
                          area.center().y() - window.height() / 2);
    // 允许 1px 的取整误差
    QVERIFY2((window.pos() - expected).manhattanLength() <= 2,
             qPrintable(QStringLiteral("启动未居中：pos=(%1,%2) expected=(%3,%4) size=%5x%6")
                            .arg(window.pos().x())
                            .arg(window.pos().y())
                            .arg(expected.x())
                            .arg(expected.y())
                            .arg(window.width())
                            .arg(window.height())));
}

// 拖拽投喂可用性的**路径无关性**回归（见 docs/traps-extend0.md）：
// 「拖拽显示禁止投放」曾只出现在安装版，且与投放文件所在盘符 / 目录层级无关。
// 这里守住「投放无关来源位置」：任意盘符根目录、一级子目录（乃至更深的本地路径）
// 的文件都必须被 dragEnter / dragMove 接受；非本地 URL 仍按既有语义忽略。
void SmokeTest::dropAcceptsLocalFilesFromAnyDriveRootOrFirstLevelDir()
{
    whalepet::PetWindow window;
    window.showPet();

    QMimeData localFiles;
    localFiles.setUrls({QUrl::fromLocalFile(QStringLiteral("D:/投喂.txt")),
                        QUrl::fromLocalFile(QStringLiteral("D:/一级子目录/投喂2.txt")),
                        QUrl::fromLocalFile(QStringLiteral("E:/notes.md")),
                        QUrl::fromLocalFile(QStringLiteral("F:/sub/deep/file.bin"))});

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &localFiles, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QVERIFY2(enter.isAccepted(), "本地文件拖入必须被接受，否则会出现「禁止投放」光标");

    QDragMoveEvent move(QPoint(10, 10), Qt::CopyAction, &localFiles, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &move);
    QVERIFY2(move.isAccepted(), "拖拽移动过程中必须持续接受");

    // 非本地 URL（http/https、浏览器拖出的文本）不响应：保持既有语义
    QMimeData remote;
    remote.setUrls({QUrl(QStringLiteral("https://example.com/a.png"))});
    QDragEnterEvent remoteEnter(QPoint(10, 10), Qt::CopyAction, &remote, Qt::LeftButton,
                                Qt::NoModifier);
    QApplication::sendEvent(&window, &remoteEnter);
    QVERIFY2(!remoteEnter.isAccepted(), "非本地 URL 不应被接受");
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

// 小游戏「鲸鱼娘找小猫」界面回归（实测 Bug 守卫）：
//   1) 方向键必须交给移动逻辑，不能被难度下拉框吃掉——否则上下键会变成「切换地图」；
//   2) 角色离开起点后，起点格不应继续显示鲸鱼娘标记；
//   3) 切换难度重载地图后，地图必须仍然完整（不能变成空白、必须重启才恢复）。
void SmokeTest::kittenViewArrowKeysMoveInsteadOfSwitchingDifficulty()
{
    whalepet::MiniGameContext ctx; // controller / db 均为空：无表现也能玩
    whalepet::KittenView view(ctx);
    view.show();
    QApplication::processEvents();

    auto *box = view.findChild<QComboBox *>();
    QVERIFY2(box != nullptr, "难度下拉框缺失");
    auto *map = view.findChild<QWidget *>(QStringLiteral("KittenMap"));
    QVERIFY2(map != nullptr, "地图控件缺失");

    auto cellCount = [&view]() {
        QWidget *m = view.findChild<QWidget *>(QStringLiteral("KittenMap"));
        return (m == nullptr) ? -1 : static_cast<int>(m->findChildren<QToolButton *>().size());
    };
    // 角色位置由 cellState=="player" 表达（方块上不渲染文字）
    auto playerCells = [&view]() {
        QWidget *m = view.findChild<QWidget *>(QStringLiteral("KittenMap"));
        if (m == nullptr) {
            return -1;
        }
        int count = 0;
        const QList<QToolButton *> cells = m->findChildren<QToolButton *>();
        for (QToolButton *cell : cells) {
            if (cell->property("cellState").toString() == QStringLiteral("player")) {
                ++count;
            }
        }
        return count;
    };
    // 方块上不得有任何文字：既不能有物体名，也不能留下占位符 / 空文本以外的残留
    auto labelledCells = [&view]() {
        QWidget *m = view.findChild<QWidget *>(QStringLiteral("KittenMap"));
        if (m == nullptr) {
            return -1;
        }
        int count = 0;
        const QList<QToolButton *> cells = m->findChildren<QToolButton *>();
        for (QToolButton *cell : cells) {
            if (!cell->text().isEmpty()) {
                ++count;
            }
        }
        return count;
    };

    const int cellsBefore = cellCount();
    QVERIFY2(cellsBefore > 0, "开局应有地图格");
    QCOMPARE(playerCells(), 1);
    QCOMPARE(labelledCells(), 0);

    // 真实按键先到达焦点控件（打开窗口时就是难度下拉框），故按焦点控件投递
    QWidget *target = (view.focusWidget() != nullptr) ? view.focusWidget()
                                                      : static_cast<QWidget *>(&view);

    // 模拟「用户上次停在最后一档难度」：实测 Bug 里的现象 1/2 正是这个组合下出现的
    box->setCurrentIndex(whalepet::core::kRfkDifficultyCount - 1);
    QApplication::processEvents();
    const int atLastIndex = box->currentIndex();

    QTest::keyClick(target, Qt::Key_Up); // 上键应向上移动，不能变成「切换地图」
    QApplication::processEvents();
    const int afterUp = box->currentIndex();
    const int cellsAfterUp = cellCount();

    QTest::keyClick(target, Qt::Key_Down); // 下键同理（实测曾表现为「无响应」）
    QApplication::processEvents();
    const int afterDown = box->currentIndex();
    const int cellsAfterDown = cellCount();

    // 向右移动一步（起点右侧是可走格）：角色标记必须唯一跟随，且仍不出现任何文字
    QTest::keyClick(&view, Qt::Key_Right);
    QApplication::processEvents();
    const int playerAfterMove = playerCells();
    const int labelledAfterMove = labelledCells();
    const int cellsAfterMove = cellCount();

    // 切换难度：新地图必须完整重载（实测曾出现 0×0 空白、必须重启才恢复）
    box->setCurrentIndex(0);
    QApplication::processEvents();
    const int cellsAfterSwitch = cellCount();
    const int playerAfterSwitch = playerCells();
    const int labelledAfterSwitch = labelledCells();
    const QSize sizeAfterSwitch = map->size();

    // 方向键不得改变难度（否则等于「退出当前地图 / 换地图」），也不得重载地图
    QCOMPARE(afterUp, atLastIndex);
    QCOMPARE(cellsAfterUp, cellsBefore);
    QCOMPARE(afterDown, atLastIndex);
    QCOMPARE(cellsAfterDown, cellsBefore);

    // 角色离开起点后，玩家标记仍唯一（起点不再被标记），且不残留任何文字
    QCOMPARE(playerAfterMove, 1);
    QCOMPARE(labelledAfterMove, 0);
    QCOMPARE(cellsAfterMove, cellsBefore);

    // 切换难度后地图必须完整重载（实测 Bug：切完变成 0×0 空白、必须重启才恢复）
    QVERIFY2(cellsAfterSwitch > 0, "切换难度后地图为空");
    QVERIFY2(sizeAfterSwitch.width() > 0 && sizeAfterSwitch.height() > 0, "切换难度后地图尺寸为 0");
    QCOMPARE(playerAfterSwitch, 1);   // 新地图上仍只有一个角色
    QCOMPARE(labelledAfterSwitch, 0); // 重载后同样不得出现任何文字
}

// 场景切换必须按【新场景】重建网格（实测 Bug：切换地图后出现「隐形墙」——
// 显示是空地却走不过去）。
// 各场景宽高不同（深海遗迹为 11×8 → 13×8 → 13×9），若切换后只 refresh 旧网格，
// 新场景的格子会被按旧网格的行列错位显示：视觉与判定对不上，表现为墙壁凭空出现。
// 本用例用 core 解析同一份随包地图作为「判定真源」，逐格比对显示状态。
void SmokeTest::kittenSceneChangeRebuildsGrid()
{
    whalepet::MiniGameContext ctx;
    whalepet::KittenView view(ctx);
    view.show();
    QApplication::processEvents();

    auto *box = view.findChild<QComboBox *>();
    QVERIFY2(box != nullptr, "难度下拉框缺失");

    auto readAsset = [](const QString &name, bool *ok) {
        QFile file(QStringLiteral(WHALEPET_MAPS_DIR) + QStringLiteral("/") + name);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (ok != nullptr) {
                *ok = false;
            }
            return std::string();
        }
        if (ok != nullptr) {
            *ok = true;
        }
        return file.readAll().toStdString();
    };
    auto gridCells = [&view]() {
        QWidget *m = view.findChild<QWidget *>(QStringLiteral("KittenMap"));
        return (m == nullptr) ? QList<QToolButton *>() : m->findChildren<QToolButton *>();
    };
    // BFS 求「起点 → 出口」的最短方向序列，避免在测试里硬编码地图走法
    auto pathToExit = [](const whalepet::core::RfkRoom &room) {
        QVector<QPair<int, int>> steps;
        const int count = room.cellCount();
        QVector<int> prev(count, -2); // -2 = 未访问
        QVector<int> dirOf(count, -1);
        QVector<int> queue;
        prev[room.startIndex] = -1;
        queue.push_back(room.startIndex);
        const int dxs[4] = {0, 0, -1, 1};
        const int dys[4] = {-1, 1, 0, 0};
        while (!queue.isEmpty()) {
            const int cur = queue.takeFirst();
            if (cur == room.exitIndex) {
                break;
            }
            const int x = cur % room.width;
            const int y = cur / room.width;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dxs[k];
                const int ny = y + dys[k];
                if (nx < 0 || ny < 0 || nx >= room.width || ny >= room.height) {
                    continue;
                }
                const int ni = ny * room.width + nx;
                if (prev[ni] != -2
                    || room.cells[static_cast<std::size_t>(ni)].kind
                           == whalepet::core::RfkKind::Blocker) {
                    continue;
                }
                prev[ni] = cur;
                dirOf[ni] = k;
                queue.push_back(ni);
            }
        }
        int cur = room.exitIndex;
        while (cur >= 0 && prev[cur] >= 0) {
            const int k = dirOf[cur];
            steps.prepend(qMakePair(dxs[k], dys[k]));
            cur = prev[cur];
        }
        return steps;
    };

    bool ok = false;
    const whalepet::core::RfkObjectTable table =
        whalepet::core::RfkObjectTable::parse(readAsset(QStringLiteral("kitten_objects.txt"), &ok));
    QVERIFY2(ok, "物体表打不开（检查 WHALEPET_MAPS_DIR）");

    // 选「深海遗迹」：3 个场景，且场景尺寸互不相同（11×8 → 13×8 → 13×9）
    box->setCurrentIndex(whalepet::core::kRfkDifficultyCount - 1);
    QApplication::processEvents();
    QCOMPARE(box->currentIndex(), whalepet::core::kRfkDifficultyCount - 1);

    std::string error;
    whalepet::core::RfkRoom room1;
    QVERIFY2(whalepet::core::rfkParseRoom(readAsset(QStringLiteral("kitten_expert_1.txt"), &ok),
                                          table, room1, &error),
             error.c_str());
    QCOMPARE(gridCells().size(), room1.cellCount()); // 开局网格与场景 1 一致

    // 沿最短路径走到出口 → 触发场景切换
    const QVector<QPair<int, int>> steps = pathToExit(room1);
    QVERIFY2(!steps.isEmpty(), "场景 1 的出口不可达");
    for (const QPair<int, int> &step : steps) {
        // 注意类型必须是 Qt::Key：若写成 int，QTest::keyClick 会选中 char 重载并截断键值
        const Qt::Key key = (step.first == 0) ? (step.second < 0 ? Qt::Key_Up : Qt::Key_Down)
                                              : (step.first < 0 ? Qt::Key_Left : Qt::Key_Right);
        QTest::keyClick(&view, key);
        QApplication::processEvents();
    }

    // 场景 2 的尺寸与场景 1 不同：网格必须重建（否则行列错位 → 隐形墙）
    whalepet::core::RfkRoom room2;
    QVERIFY2(whalepet::core::rfkParseRoom(readAsset(QStringLiteral("kitten_expert_2.txt"), &ok),
                                          table, room2, &error),
             error.c_str());
    QVERIFY2(room2.cellCount() != room1.cellCount(),
             "本例依赖「场景切换后尺寸改变」，地图改动需同步调整用例");

    const QList<QToolButton *> grid = gridCells();
    QCOMPARE(grid.size(), room2.cellCount());

    // 逐格比对：显示状态必须与 core 的判定数据一致
    for (int i = 0; i < room2.cellCount(); ++i) {
        const bool wallByCore =
            room2.cells[static_cast<std::size_t>(i)].kind == whalepet::core::RfkKind::Blocker;
        const bool wallByView =
            grid.at(i)->property("cellState").toString() == QStringLiteral("wall");
        if (wallByView != wallByCore) {
            QFAIL(qPrintable(QStringLiteral("第 %1 格显示与判定不一致（显示 wall=%2 / 判定 wall=%3）"
                                            " —— 网格未按新场景重建")
                                 .arg(i)
                                 .arg(wallByView)
                                 .arg(wallByCore)));
        }
    }

    // 重建后的方块同样不得携带任何文字（含物体名 / 角色名 / 占位符）
    for (QToolButton *cell : grid) {
        QVERIFY2(cell->text().isEmpty(), "场景切换重建后仍有方块残留文字");
    }
}

// 小游戏「扫雷」界面回归（实测 Bug 守卫）：窗口显示后重开一局（或切难度重建棋盘）时，
// 棋盘尺寸必须仍然有效，不得被 `setFixedSize(0,0)` 锁死成 0×0 —— 否则棋盘空白，
// 且因 min/max 被同时写死而**必须重启程序才恢复**。
// 见 docs/mapinit.md（同源问题：docs/traps-P6.md TRAP-P6-005 根因 B）。
void SmokeTest::minesweeperViewRestartKeepsBoardSized()
{
    whalepet::MiniGameContext ctx; // controller / db 均为空：无表现、无持久化也能玩
    whalepet::MinesweeperView view(ctx);
    view.show();
    QApplication::processEvents();

    auto *board = view.findChild<QWidget *>(QStringLiteral("MineBoard"));
    QVERIFY2(board != nullptr, "棋盘控件缺失");

    auto cellCount = [&view]() {
        QWidget *b = view.findChild<QWidget *>(QStringLiteral("MineBoard"));
        return (b == nullptr) ? -1 : static_cast<int>(b->findChildren<QToolButton *>().size());
    };

    const int cellsBefore = cellCount();
    QVERIFY2(cellsBefore > 0, "开局应有棋盘格");
    QVERIFY2(board->minimumSize().width() > 0 && board->minimumSize().height() > 0,
             "开局棋盘尺寸为 0（setFixedSize 已被锁死）");

    // 「重新开始」是「窗口显示后重建棋盘」的最短路径——正是 0×0 的触发点
    QPushButton *restart = nullptr;
    const QList<QPushButton *> buttons = view.findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->text() == QStringLiteral("重新开始")) {
            restart = b;
            break;
        }
    }
    QVERIFY2(restart != nullptr, "未找到「重新开始」按钮");
    restart->click();
    QApplication::processEvents();

    QCOMPARE(cellCount(), cellsBefore);
    // setFixedSize 会同时写 min/max；运行中重建若算出 (0,0)，这两者会被永久锁死为 0
    QVERIFY2(board->minimumSize().width() > 0 && board->minimumSize().height() > 0,
             "重开后棋盘 min 尺寸为 0（setFixedSize(0,0) 锁死）");
    QVERIFY2(board->maximumSize().width() > 0 && board->maximumSize().height() > 0,
             "重开后棋盘 max 尺寸为 0（setFixedSize(0,0) 锁死）");
    QVERIFY2(board->size().width() > 0 && board->size().height() > 0,
             "重开后棋盘实际尺寸为 0");
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

// 桌面四边框判定（docs/PRESENTATION.md §3.1）：纯几何逻辑，判定桌宠窗口贴合了哪条边框。
// 场景取：桌面可用区域 1000x800、窗口 240x240、阈值 16px。
void SmokeTest::desktopEdgeDetectionPicksNearestBorder()
{
    using whalepet::core::DesktopEdge;
    const auto detect = [](int x, int y) {
        return whalepet::core::detectDesktopEdge(x, y, 240, 240, 0, 0, 1000, 800, 16);
    };

    // 左上角：上 / 左距离同为 0 → 按 上 → 下 → 左 → 右 的既定顺序取「上」
    QCOMPARE(detect(0, 0), DesktopEdge::Top);

    // 单条边框贴合（另一轴远离边框）
    QCOMPARE(detect(380, 0), DesktopEdge::Top);      // 上：y - 0 = 0
    QCOMPARE(detect(380, 560), DesktopEdge::Bottom); // 下：800 - (560 + 240) = 0
    QCOMPARE(detect(0, 280), DesktopEdge::Left);     // 左：x - 0 = 0
    QCOMPARE(detect(760, 280), DesktopEdge::Right);  // 右：1000 - (760 + 240) = 0

    // 阈值边界：14px 仍算贴合，18px 不算（阈值 16px）
    QCOMPARE(detect(380, 14), DesktopEdge::Top);
    QCOMPARE(detect(380, 18), DesktopEdge::None);

    // 远离所有边框 → 不贴边
    QCOMPARE(detect(380, 280), DesktopEdge::None);
}

// 贴边表现：贴边时立绘换成探头立绘并贴齐对应边框，命中区整体视为「头」
// （与参考项目 peek 分区一致）；取消贴边后恢复常规分区判定。
void SmokeTest::poseViewAttachesPeekPoseToDesktopEdge()
{
    using whalepet::core::DesktopEdge;
    whalepet::PoseView view;
    const int midY = view.height() / 2;

    QCOMPARE(view.edgeAttachment(), DesktopEdge::None);

    // 贴合左边框：探头立绘贴齐窗口左边 → 左侧命中「头」，右侧不命中
    view.setEdgeAttachment(DesktopEdge::Left);
    QCOMPARE(view.edgeAttachment(), DesktopEdge::Left);
    QCOMPARE(view.zoneAt(QPoint(4, midY)), whalepet::core::Zone::Head);
    QCOMPARE(view.zoneAt(QPoint(view.width() - 4, midY)), whalepet::core::Zone::None);

    // 换到右边框：命中区随之贴齐右侧
    view.setEdgeAttachment(DesktopEdge::Right);
    QCOMPARE(view.zoneAt(QPoint(view.width() - 4, midY)), whalepet::core::Zone::Head);
    QCOMPARE(view.zoneAt(QPoint(4, midY)), whalepet::core::Zone::None);

    // 取消贴边：恢复常规分区（此点落在内容矩形之外 → 无命中）
    view.setEdgeAttachment(DesktopEdge::None);
    QCOMPARE(view.edgeAttachment(), DesktopEdge::None);
    QCOMPARE(view.zoneAt(QPoint(4, midY)), whalepet::core::Zone::None);
}

// 贴边表现优先：拖动过程中窗口已贴合边框时，**不**用拖动立绘（pick-up）替换探头立绘；
// 拖离边框后恢复正常接受姿态切换。
void SmokeTest::poseViewKeepsEdgePoseWhileDragging()
{
    using whalepet::core::DesktopEdge;
    whalepet::PoseView view;
    QVERIFY(view.setPoseImmediate(QStringLiteral("idle-cute")));

    view.setEdgeAttachment(DesktopEdge::Top);
    view.beginDrag();
    QVERIFY(view.setPose(QStringLiteral("pick-up")));
    // 贴边 + 拖动中：拖动立绘被丢弃，目标姿态保持原值（探头立绘不被替换）
    QCOMPARE(view.poseName(), QStringLiteral("idle-cute"));

    // 仍在拖动，但已离开边框 → 正常接受拖动立绘
    view.setEdgeAttachment(DesktopEdge::None);
    QVERIFY(view.setPose(QStringLiteral("pick-up")));
    QCOMPARE(view.poseName(), QStringLiteral("pick-up"));

    view.endDrag(QPointF(0.0, 0.0));
}

// 端到端：把桌宠窗口移到屏幕边框 → 判定为对应方向并切换贴边表现；
// 离开边框 → 回到不贴边。移动事件在此显式投递，避免依赖平台是否回发 QMoveEvent。
void SmokeTest::petWindowReportsDesktopEdgeWhenMovedToBorder()
{
    using whalepet::core::DesktopEdge;
    whalepet::PetWindow window;
    window.showPet();
    QApplication::processEvents();

    QScreen *screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "无可用屏幕");
    const QRect area = screen->availableGeometry();

    const auto moveTo = [&window](const QPoint &p) {
        window.move(p);
        QMoveEvent moved(p, p);
        QApplication::sendEvent(&window, &moved);
    };

    // 左边框（另一轴留出 200px，确保只有左边贴合）
    moveTo(QPoint(area.left(), area.top() + 200));
    QCOMPARE(window.desktopEdge(), DesktopEdge::Left);

    // 上边框
    moveTo(QPoint(area.left() + 200, area.top()));
    QCOMPARE(window.desktopEdge(), DesktopEdge::Top);

    // 回到屏幕中间 → 不贴边
    moveTo(area.center() - QPoint(window.width() / 2, window.height() / 2));
    QCOMPARE(window.desktopEdge(), DesktopEdge::None);
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
