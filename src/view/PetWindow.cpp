#include "view/PetWindow.h"

#include "core/PetTypes.h"
#include "model/Database.h"
#include "model/SettingsRepo.h"
#include "view/PoseLibrary.h"
#include "view/PoseView.h"
#include "view/SpeechBubble.h"
#include "view/StatusPanel.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/PetController.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDebug>
#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace whalepet {

namespace {
const char *kDefaultPose = "idle-cute";
// P1 遗留的 QSettings 键：**仅**用于一次性导入（见 importLegacyPositionIfNeeded）。
// P3 起窗口位置存入 settings 表（DATA-MODEL §3.3），不再读写 QSettings。
const char *kLegacyPosKey = "window/position";
} // namespace

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    m_settingsKey = QStringLiteral("WhalePet");

    setupWindowFlags();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 立绘库：首批同步（保证首帧）+ 其余惰性预载
    m_library = new PoseLibrary(this);

    m_pose = new PoseView(this);
    m_pose->setLibrary(m_library);
    m_pose->setPoseImmediate(QString::fromUtf8(kDefaultPose));
    layout->addWidget(m_pose);

    // 台词气泡：独立工具窗口，跟随立绘
    m_bubble = new SpeechBubble(this);
    m_bubble->attachTo(this);

    setupContextMenu();
    setupTray();
    setupController();
    setupGrowth();

    // 固定尺寸：统一 256x256 画布 → 切换姿态不再 resize
    setFixedSize(kPetWindowSize, kPetWindowSize);
    resetToDefaultPosition();
}

PetWindow::~PetWindow()
{
    if (m_growth != nullptr) {
        m_growth->stopTicking();
        m_growth->flush();
    }
    delete m_statusPanel;
    m_statusPanel = nullptr;
    delete m_db;   // Database 析构会 close() 并释放连接
    m_db = nullptr;
}

void PetWindow::setupWindowFlags()
{
    // 透明 + 无边框 + 置顶 + 工具窗口（不进入任务栏、不抢焦点）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setWindowTitle(QStringLiteral("WhalePet"));
}

void PetWindow::setupController()
{
    m_controller = new PetController(m_pose, m_bubble, this);
}

void PetWindow::setupGrowth()
{
    m_db = new model::Database;
    if (!m_db->open()) {
        // 已由 Database 内部降级到内存库；此处再补一条，说明养成数据不会跨会话保留
        qWarning() << "[PetWindow] 数据库不可用，养成数据仅在内存中存活";
    }

    m_growth = new viewmodel::GrowthService(m_db, this);
    m_growth->load();
    m_controller->setGrowthService(m_growth);

    connect(m_growth, &viewmodel::GrowthService::stateChanged, this,
            &PetWindow::syncStatusPanel);

    // 退出前强制落盘（状态变更已即时落盘，这里是最后一道保险）
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_growth != nullptr) {
            m_growth->flush();
        }
    });
}

QString PetWindow::storageInfo() const
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return QStringLiteral("存储：不可用（未连接数据库）");
    }
    QString modeText;
    switch (m_db->mode()) {
    case model::StorageMode::InstallDir:
        modeText = QStringLiteral("安装目录");
        break;
    case model::StorageMode::UserDir:
        modeText = QStringLiteral("用户目录（已降级）");
        break;
    case model::StorageMode::Memory:
        modeText = QStringLiteral("内存模式（不落盘）");
        break;
    }
    return QStringLiteral("存储：%1\n%2").arg(modeText, m_db->location());
}

void PetWindow::syncStatusPanel()
{
    if (m_statusPanel == nullptr || m_growth == nullptr) {
        return;
    }
    m_statusPanel->updateFrom(m_growth->state(), m_growth->bondUnlocks(), storageInfo());
}

void PetWindow::showStatusPanel()
{
    if (m_statusPanel == nullptr) {
        m_statusPanel = new StatusPanel(nullptr);
        connect(m_statusPanel, &StatusPanel::signInRequested, this, [this] {
            if (m_growth == nullptr) {
                return;
            }
            if (!m_growth->signIn()) {
                // 今天已签到：不弹窗打断，只记录
                qInfo() << "[PetWindow] 今日已签到";
            }
            syncStatusPanel();
        });
    }

    syncStatusPanel();
    m_statusPanel->show();
    m_statusPanel->raise();
    m_statusPanel->activateWindow();
}

void PetWindow::setupContextMenu()
{
    m_menu = new QMenu(this);
    // 立绘窗口本身是 WindowStaysOnTopHint 的置顶窗口；菜单默认只是 Qt::Popup，
    // 会被压在立绘下面（目视验收：右键菜单被立绘遮挡）。
    // 因此菜单必须同样置顶；同时用事件过滤器在显示时再 raise 一次——
    // Windows 上 Popup 的真实窗口在弹出阶段才创建，仅设置 flag 不足以保证层级。
    m_menu->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    m_menu->installEventFilter(this);

    QAction *feed = m_menu->addAction(QStringLiteral("投喂"));
    connect(feed, &QAction::triggered, this, [this] {
        emit feedRequested();
        m_controller->handleMenuAction(core::EventType::Feed);
    });

    QAction *poke = m_menu->addAction(QStringLiteral("戳一下"));
    connect(poke, &QAction::triggered, this, [this] {
        emit pokeRequested();
        m_controller->handleMenuAction(core::EventType::Tease);
    });

    QAction *praise = m_menu->addAction(QStringLiteral("夸夸"));
    connect(praise, &QAction::triggered, this, [this] {
        emit praiseRequested();
        m_controller->handleMenuAction(core::EventType::Praise);
    });

    m_menu->addSeparator();

    QAction *reset = m_menu->addAction(QStringLiteral("回原位"));
    connect(reset, &QAction::triggered, this, [this] {
        resetToDefaultPosition();
        savePosition();
    });

    QAction *status = m_menu->addAction(QStringLiteral("状态"));
    connect(status, &QAction::triggered, this, &PetWindow::showStatusPanel);

    QAction *settings = m_menu->addAction(QStringLiteral("设置"));
    connect(settings, &QAction::triggered, this, [this] { emit settingsRequested(); });

    m_menu->addSeparator();

    QAction *quit = m_menu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);
}

void PetWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "[PetWindow] 系统托盘不可用，跳过托盘初始化";
        return;
    }

    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("WhalePet"));

    QPixmap icon = m_pose->pixmap();
    if (!icon.isNull()) {
        m_tray->setIcon(QIcon(icon));
    }

    auto *trayMenu = new QMenu(this);
    // 与右键菜单同理：托盘菜单也要置顶，否则同样可能被置顶立绘压住
    trayMenu->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    trayMenu->installEventFilter(this);
    QAction *toggle = trayMenu->addAction(QStringLiteral("显示 / 隐藏"));
    connect(toggle, &QAction::triggered, this, [this] { setVisible(!isVisible()); });

    QAction *status = trayMenu->addAction(QStringLiteral("状态"));
    connect(status, &QAction::triggered, this, &PetWindow::showStatusPanel);

    QAction *quit = trayMenu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(trayMenu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    setVisible(!isVisible());
                }
            });

    m_tray->show();
}

void PetWindow::showPet()
{
    m_library->startPreload();
    importLegacyPositionIfNeeded();
    restorePosition();
    show();
    m_controller->start();

    // 养成结算：每 60s 一次（饱食衰减 + 陪伴时长累计），见 ROADMAP-P3 §3
    if (m_growth != nullptr) {
        m_growth->startTicking();
    }
}

QPoint PetWindow::defaultPosition() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QPoint(100, 100);
    }
    const QRect area = screen->availableGeometry();
    const int x = area.right() - width() - 40;
    const int y = area.bottom() - height() - 40;
    return QPoint(qMax(area.left(), x), qMax(area.top(), y));
}

void PetWindow::resetToDefaultPosition()
{
    move(defaultPosition());
    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
}

void PetWindow::importLegacyPositionIfNeeded()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    const bool hadRow = repo.load(data);
    if (hadRow && data.hasPosition) {
        return; // 已是新存储，后续不再读取 QSettings
    }

    QSettings legacy(m_settingsKey, m_settingsKey);
    const QVariant pos = legacy.value(QString::fromUtf8(kLegacyPosKey));
    if (!pos.isValid()) {
        return;
    }

    data.hasPosition = true;
    data.posX = pos.toPoint().x();
    data.posY = pos.toPoint().y();
    if (repo.save(data)) {
        qInfo() << "[PetWindow] 已从 P1 的 QSettings 一次性导入窗口位置:" << pos.toPoint();
    }
    // 刻意不清除旧键：保留一份可回滚的旧值，但程序不再读取
}

void PetWindow::restorePosition()
{
    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    if (repo.load(data) && data.hasPosition) {
        const QPoint p(data.posX, data.posY);
        // 简单校验：落在任一屏幕可用区域内才采用，否则回默认位置
        bool onScreen = false;
        const auto screens = QGuiApplication::screens();
        for (QScreen *screen : screens) {
            if (screen->availableGeometry().contains(p)) {
                onScreen = true;
                break;
            }
        }
        if (onScreen) {
            move(p);
            return;
        }
        qWarning() << "[PetWindow] 保存的位置不在任何屏幕内，回退到默认位置:" << p;
    }

    resetToDefaultPosition();
}

void PetWindow::savePosition()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    repo.load(data);   // 保留其它设置项；无记录时以默认值起底
    data.hasPosition = true;
    data.posX = pos().x();
    data.posY = pos().y();
    repo.save(data);
}

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_dragging = false;
        m_pressGlobalPos = event->globalPosition().toPoint();
        m_pressViewPos = event->position().toPoint();
        m_dragOffset = m_pressGlobalPos - frameGeometry().topLeft();

        m_dragClock.start();
        m_lastMovePos = m_pressGlobalPos;
        m_lastMoveMs = 0;
        m_velocity = QPointF(0.0, 0.0);

        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && (event->buttons() & Qt::LeftButton)) {
        const QPoint globalPos = event->globalPosition().toPoint();

        if (!m_dragging) {
            const int distance = (globalPos - m_pressGlobalPos).manhattanLength();
            if (distance > kDragThreshold) {
                m_dragging = true;
                m_pose->beginDrag();
                m_controller->handleDragBegin();
            }
        }

        if (m_dragging) {
            move(globalPos - m_dragOffset);
            m_pose->dragMove(globalPos - m_pressGlobalPos);
            m_bubble->reposition();

            // 速度估计（px/ms，指数平滑），松手时用于惯性滑行
            const qint64 now = m_dragClock.elapsed();
            const qint64 dt = now - m_lastMoveMs;
            if (dt > 0) {
                const QPointF v((globalPos - m_lastMovePos).x() / static_cast<qreal>(dt),
                                (globalPos - m_lastMovePos).y() / static_cast<qreal>(dt));
                m_velocity = m_velocity * 0.6 + v * 0.4;
                m_lastMovePos = globalPos;
                m_lastMoveMs = now;
            }
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasDragging = m_dragging;
    m_pressed = false;
    m_dragging = false;

    if (wasDragging) {
        // 停顿超过阈值则视为「已停下」，不做惯性
        QPointF velocity = m_velocity;
        if (m_dragClock.elapsed() - m_lastMoveMs > kVelocityResetMs) {
            velocity = QPointF(0.0, 0.0);
        }
        m_pose->endDrag(velocity * 1000.0); // 转换到 px/s
        m_controller->handleDragEnd();
        savePosition();
    } else {
        // 单击：分区命中 → 即时反馈 + 语义事件
        const core::Zone zone = m_pose->zoneAt(m_pressViewPos);
        m_pose->clickFeedback();
        m_controller->handleClick(zone);
    }

    event->accept();
}

bool PetWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 菜单弹出后再抬一次层级：保证菜单完整可见、可点击（不被立绘遮挡）
    if (event->type() == QEvent::Show) {
        if (auto *menu = qobject_cast<QMenu *>(watched)) {
            menu->raise();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_menu) {
        m_menu->exec(event->globalPos());
        event->accept();
    }
}

void PetWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
}

void PetWindow::closeEvent(QCloseEvent *event)
{
    // 托盘常驻：关窗只隐藏，退出走菜单/托盘
    m_controller->stop();
    m_bubble->hideLine();
    event->ignore();
    hide();
}

} // namespace whalepet
