#pragma once

// PetWindow：桌宠主窗口（docs/ARCHITECTURE.md §3 View 层）。
//
// 透明 / 无边框 / 置顶 / 不抢焦点（Qt::Tool），承载立绘与台词气泡。
// 窗口尺寸**固定**为 kPetWindowSize：92 张立绘已统一 256x256，
// 切换姿态不再 resize 窗口（动效全部在 PoseView 内容层完成）。
//
// 职责边界：本类只把鼠标手势翻译成 PoseView 的即时反馈 + PetController 的语义事件，
// 不做任何状态判定（状态判定在 core/PetStateMachine）。

#include "common/PetVisuals.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

class QMenu;
class QSystemTrayIcon;

namespace whalepet {

namespace model {
class Database;
} // namespace model

namespace viewmodel {
class GrowthService;
} // namespace viewmodel

class PoseLibrary;
class PoseView;
class PetController;
class SpeechBubble;
class StatusPanel;

class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow() override;

    // 启动立绘预载与定时驱动、恢复上次位置并显示窗口与托盘
    void showPet();

    PoseView *poseView() const { return m_pose; }
    SpeechBubble *speechBubble() const { return m_bubble; }
    PetController *controller() const { return m_controller; }
    viewmodel::GrowthService *growthService() const { return m_growth; }
    model::Database *database() const { return m_db; }

    // 回到默认位置（主屏右下角上方）
    void resetToDefaultPosition();

    // 状态面板（P3）：显示并刷新
    void showStatusPanel();

signals:
    void feedRequested();      // 投喂
    void pokeRequested();      // 戳一下
    void praiseRequested();    // 夸夸
    void settingsRequested();  // 设置

protected:
    // 用于把右键菜单/托盘菜单抬到置顶立绘之上（见 setupContextMenu）
    bool eventFilter(QObject *watched, QEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void setupWindowFlags();
    void setupContextMenu();
    void setupTray();
    void setupController();
    void setupGrowth();

    void restorePosition();
    void savePosition();
    QPoint defaultPosition() const;

    // P1 遗留的 QSettings 位置一次性导入（导入成功后不再读 QSettings）
    void importLegacyPositionIfNeeded();

    void syncStatusPanel();
    QString storageInfo() const;

    static constexpr int kDragThreshold = 4;   // 移动超过 4px 才进入拖拽
    static constexpr int kVelocityResetMs = 120; // 停顿超过该时长则松手速度视为 0

    PoseLibrary *m_library = nullptr;
    PoseView *m_pose = nullptr;
    SpeechBubble *m_bubble = nullptr;
    PetController *m_controller = nullptr;
    QMenu *m_menu = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    StatusPanel *m_statusPanel = nullptr;

    model::Database *m_db = nullptr;
    viewmodel::GrowthService *m_growth = nullptr;

    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobalPos;
    QPoint m_pressViewPos;
    QPoint m_dragOffset;

    // 松手惯性用的速度估计（px/ms，指数平滑）
    QElapsedTimer m_dragClock;
    QPoint m_lastMovePos;
    qint64 m_lastMoveMs = 0;
    QPointF m_velocity;

    QString m_settingsKey;
};

} // namespace whalepet
