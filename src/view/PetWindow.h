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
#include "minigame/MiniGameRegistry.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

class QAction;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMenu;
class QPushButton;
class QSystemTrayIcon;

namespace whalepet {

namespace model {
class Database;
struct SettingsData;
} // namespace model

namespace viewmodel {
class AchievementService;
class GrowthService;
class MiniGameService;
class QuestService;
class SigninService;
class StomachService;
} // namespace viewmodel

class ContentPanel;
class GlobalHotkey;
class HotwordDialog;
class MiniGameView;
class PoseLibrary;
class PoseView;
class PetController;
class SettingsDialog;
class SpeechBubble;
class StatusPanel;

class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow() override;

    // 启动立绘预载与定时驱动、按当前屏幕居中并显示窗口与托盘
    void showPet();

    PoseView *poseView() const { return m_pose; }
    SpeechBubble *speechBubble() const { return m_bubble; }
    PetController *controller() const { return m_controller; }
    viewmodel::GrowthService *growthService() const { return m_growth; }
    viewmodel::AchievementService *achievementService() const { return m_achievement; }
    viewmodel::QuestService *questService() const { return m_quest; }
    viewmodel::SigninService *signinService() const { return m_signin; }
    viewmodel::StomachService *stomachService() const { return m_stomach; }
    viewmodel::MiniGameService *miniGameService() const { return m_miniGameService; }
    model::Database *database() const { return m_db; }

    // 回到「初始位置」：当前主屏可用区域的几何中心（每次启动都会调用）
    void resetToDefaultPosition();

    // 状态面板（P3）：显示并刷新
    void showStatusPanel();

    // 内容面板（P4）：日常 / 成就墙 / 成长日记
    void showContentPanel();

    // 设置面板（P6）：陪伴表现 / 日常·成就·日记 / 小游戏 / 数据与重置
    void showSettingsDialog();

    // 小游戏（插件化）：按插件 id 显示窗口（右键 / 托盘菜单与设置面板共用入口）
    void showMiniGame(const QString &pluginId);

signals:
    void feedRequested();      // 投喂
    void pokeRequested();      // 戳一下
    void praiseRequested();    // 夸夸

protected:
    // 用于把右键菜单/托盘菜单抬到置顶立绘之上（见 setupContextMenu）
    bool eventFilter(QObject *watched, QEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    // 拖拽投喂：判定区域 = 本窗口的整个矩形（桌宠所在区域）。
    // 仅接受含本地文件/文件夹（text/uri-list）的拖放；落点触发与「投喂」完全相同的表现与数值。
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupWindowFlags();
    // 装配一个弹出菜单（右键 / 托盘 / 「小游戏…」子菜单共用）：置顶 + 显示时 raise
    void configurePopupMenu(QMenu *menu);
    void setupContextMenu();
    void setupTray();
    void setupController();
    void setupGrowth();
    void setupContent();
    void setupStomach(); // 拖拽投喂：stomach 目录 + 每 5 分钟清空到回收站
    void setupChat();    // P5：加载 keyword_aware 并挂接剪贴板触发源
    void setupHotword(); // P6：注册全局热键 + 载入自定义热词（失败仅降级为菜单入口）
    void setupMiniGames();   // 小游戏插件：注册内置插件（必须在构建菜单之前调用）
    void setupSettings();    // P6：设置面板（懒创建在 showSettingsDialog）
    void setupRecallEntry(); // P6：左下角唤回入口（桌宠隐藏时显示）

    QPoint defaultPosition() const;   // 当前主屏可用区域的几何中心
    void watchScreenChanges();        // 运行期分辨率 / 显示器变化时保持桌宠可见

    // P6 设置生效与「找不到看板娘」防护
    void applySettings(const model::SettingsData &data); // 把库中设置应用到界面
    void setPetVisible(bool visible);                    // 桌宠显隐（联动托盘与唤回入口）
    void repositionRecallEntry();                        // 左下角唤回入口定位
    void clampToVisibleArea();                           // 位置越界夹回可见区域
    void openDataDirectory();                            // 打开 data/ 目录

    // 小游戏插件统一结算：奖励（每日上限）+ 成就 + 文案回填
    void settleMiniGame(const core::MiniGameResult &result);

    void syncStatusPanel();
    void syncContentPanel();
    bool keywordAware() const;          // 关键词感知当前是否开启
    void setKeywordAware(bool on);      // 应用 + 持久化 keyword_aware
    void showHotwordDialog();           // 打开/抬起「热词录入」面板（全局热键与菜单共用）
    void reloadHotwords();              // hotwords 表 → ChatService + 面板列表
    void syncAchievementProgress();
    void handleSignIn();       // 一次签到同时驱动 GrowthService 与 SigninService
    void checkComeback();      // 离开 2 小时后回来 → 「欢迎回来」成就
    QString storageInfo() const;

    static constexpr int kDragThreshold = 4;   // 移动超过 4px 才进入拖拽
    static constexpr int kVelocityResetMs = 120; // 停顿超过该时长则松手速度视为 0

    PoseLibrary *m_library = nullptr;
    PoseView *m_pose = nullptr;
    SpeechBubble *m_bubble = nullptr;
    PetController *m_controller = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_keywordAction = nullptr; // 「关键词感知」勾选项（P5）
    QAction *m_hotwordAction = nullptr; // 「热词录入」入口（P6）
    QMenu *m_miniGameMenu = nullptr;         // 右键菜单「小游戏…」子菜单（内容由插件动态生成）
    QMenu *m_trayMiniGameMenu = nullptr;     // 托盘菜单「小游戏…」子菜单
    QList<QAction *> m_miniGameEntryActions; // 各小游戏入口项（受 minigame_enabled 统一门控）
    GlobalHotkey *m_hotkey = nullptr;   // 系统级热键（P6）
    HotwordDialog *m_hotwordDialog = nullptr; // 懒创建，随主窗口析构
    QSystemTrayIcon *m_tray = nullptr;
    StatusPanel *m_statusPanel = nullptr;
    ContentPanel *m_contentPanel = nullptr;
    SettingsDialog *m_settingsDialog = nullptr; // 懒创建，随主窗口析构
    QHash<QString, MiniGameView *> m_miniGameViews; // 懒创建的小游戏窗口（先于 m_db 释放）
    MiniGameRegistry m_miniGames;                   // 已注册的小游戏插件
    QPushButton *m_recallButton = nullptr;      // P6：左下角唤回入口
    bool m_petEnabled = true;                   // 设置项 pet_enabled 的运行时镜像

    model::Database *m_db = nullptr;
    viewmodel::GrowthService *m_growth = nullptr;
    viewmodel::AchievementService *m_achievement = nullptr;
    viewmodel::QuestService *m_quest = nullptr;
    viewmodel::SigninService *m_signin = nullptr;
    viewmodel::StomachService *m_stomach = nullptr; // 「胃袋」：拖入落盘 + 定时清空
    viewmodel::MiniGameService *m_miniGameService = nullptr; // 扫雷结算：奖励上限 + 个人最快

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

    bool m_screenWatchInstalled = false; // watchScreenChanges 只装配一次
};

} // namespace whalepet
