#pragma once

// 小游戏插件「鲸鱼娘找小猫」的视图层实现。
//
// 功能（对照需求）：
//   - 地图探索：字符网格地图（地图文本为外部资源），左上角起点的鲸鱼娘可四方向移动；
//   - 场景切换：走到「海流」（出口格）即进入下一场景，难度决定需要穿越几个场景；
//   - 多种可交互物体：小猫 + 有趣物品（扇贝 / 海龟 / 海星 / 珍珠）+ 无关杂物
//     （海草 / 水母 / 漂流瓶 / 破靴子），逐个触发差异化立绘与专属台词；
//   - 差异化反馈：撞礁石 → 撞墙提示；有趣物品 → 好奇立绘；杂物 → 嫌弃立绘；
//     找到小猫 → 专属对话 + 通关庆祝。
//
// 边界：本类只做「界面 + 交互」，规则与地图解析全部委托 core::RfkWorld / rfkParseRoom
// （零 Qt，可单测）；结算经 MiniGameView::gameFinished 上报**通用的** core::MiniGameResult，
// 宿主统一处理养成奖励与成就。
// 外观仅依赖全局样式表（resources/qt-ui/default.qss + project.qss），不自行设计样式。

#include "core/RobotKitten.h"
#include "minigame/MiniGamePlugin.h"

#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QTimer;
class QToolButton;

namespace whalepet {

// 地图控件：按当前场景生成字符网格；点击与角色相邻的格子即朝该方向移动
class KittenMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit KittenMapWidget(QWidget *parent = nullptr);

    void setWorld(core::RfkWorld *world); // 不接管所有权
    void rebuild();                       // 依据当前场景重建网格
    void refresh();                       // 依据世界状态刷新格子外观

signals:
    void moveRequested(int dx, int dy);

private:
    void onCellClicked(int index);
    void applyCell(int index);

    core::RfkWorld *m_world = nullptr;
    QGridLayout *m_grid = nullptr;
    QList<QToolButton *> m_cells;
    int m_cellSize = 30;
};

// 找小猫窗口（宿主经 IMiniGamePlugin::createView 创建）
class KittenView : public MiniGameView {
    Q_OBJECT
public:
    explicit KittenView(const MiniGameContext &ctx, QWidget *parent = nullptr);

    // 以持久化难度载入并开新局（打开前调用）
    void reload() override;

    // 展示本局结算信息（奖励 / 纪录），由宿主结算后回填
    void setRewardText(const QString &text) override;

protected:
    void keyPressEvent(QKeyEvent *event) override; // 方向键 / WASD
    void showEvent(QShowEvent *event) override;    // 打开即把焦点交给窗口本体
    // 拦截难度下拉框对方向键的消费（上下 = 切换选项 = 换地图），转发给移动逻辑
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 方向键 / WASD → 移动；返回 false 表示该键不是移动键
    bool handleMoveKey(QKeyEvent *event);

    QWidget *buildConfigBar();
    QWidget *buildStatusBar();
    QWidget *buildDirectionPad();

    void startNewGame(core::RfkDifficulty difficulty);
    bool loadWorld(core::RfkDifficulty difficulty, QString *error);
    void applyDifficulty(int index);
    void updateDifficultyLabel();
    void updateStatus();
    void persistDifficulty(int index);

    void onMoveRequested(int dx, int dy);
    void endCurrentGame(); // 「结束本局」：放弃本局并按当前进度结算
    void finishGame();     // 结束本局并上报结算（找到小猫 / 主动放弃）
    void announce(const QString &pose, const QString &sceneKey, int ttlMs);

    PetController *m_controller = nullptr;
    model::Database *m_db = nullptr;

    core::RfkWorld m_world;

    QComboBox *m_difficultyBox = nullptr;
    QLabel *m_difficultyLabel = nullptr;
    KittenMapWidget *m_map = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_restartButton = nullptr;
    QPushButton *m_endButton = nullptr;
    QLabel *m_rewardLabel = nullptr;

    QTimer *m_timer = nullptr; // 用时
    qint64 m_elapsedMs = 0;
    bool m_timerRunning = false;
    bool m_finished = false;
    bool m_loading = false; // 载入控件期间抑制「切换难度即开新局」
};

} // namespace whalepet
