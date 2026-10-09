#pragma once

// 小游戏插件「国际象棋」的视图层实现。
//
// 功能：
//   - 8×8 棋盘，**点击或拖动**走子：点击棋子高亮全部合法落点，点击落点或把棋子拖到落点即走子；
//     落在非法格一律不移动（点击时取消选中、拖动时棋子回到原格），只允许合法着法
//     （含王车易位 / 吃过路兵 / 升变）；落子确定后经 UCI 与外部引擎通信；
//   - 对手由**外部 UCI 引擎**（Stockfish 等）经 QProcess 驱动，界面不阻塞；
//   - 引擎路径可手动指定，未配置时回退默认目录（安装目录 engine/ 或开发目录 dummy/stockfish）；
//   - 难度（引擎棋力 + 思考时间）与执子（白 / 黑）可在窗口内切换并落库；
//   - 开局 / 吃子 / 将军 / 胜 / 负 / 和 会切换鲸鱼娘立绘并播报台词。
//
// 边界：本类只做「界面 + 交互 + 引擎编排」，规则全部委托 core::ChessGame（零 Qt，可单测）；
// 结算经 MiniGameView::gameFinished 上报**通用的** core::MiniGameResult，宿主统一发放奖励与成就。
// 外观仅依赖全局样式表（resources/qt-ui/default.qss + project.qss），不自行设计样式。

#include "core/Chess.h"
#include "minigame/MiniGameCompanionSource.h"
#include "minigame/MiniGamePlugin.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QTimer;
class QToolButton;

namespace whalepet {

class UciEngine;

// 棋盘控件：8×8 格子，按「行 = rank8 在上、列 = file」布局；
// 棋子用 Unicode 棋符渲染，格子状态经动态属性 cellState / moveHint 由 project.qss 表达。
//
// 交互状态机（清晰 / 可预测）：
//   Idle ──press(己方棋子)──▶ Pressed ──拖动超过阈值──▶ Dragging ──release──▶ Idle
//   * press 落在己方棋子：选中它并高亮全部合法落点；
//   * press 落在合法落点（已有选中）：按下即视为「点击走子」，release 时提交；
//   * Dragging：跟随光标显示棋子浮影（原格暂时清空），release 落在合法落点则提交，
//     否则取消（棋子回到原格，不触发移动）；
//   * 引擎回合 / 已结束时 interactive=false：忽略一切输入并清空选中。
// 所有鼠标输入在**本控件**处理（格子按钮对鼠标透明），避免子控件抢事件导致拖动断续。
class ChessBoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChessBoardWidget(QWidget *parent = nullptr);

    void setGame(core::ChessGame *game); // 不接管所有权

    // 是否接受玩家操作（引擎回合 / 对局已结束 → false：忽略输入并清空选中）
    void setInteractive(bool interactive);

    void rebuild();  // 首次 / 尺寸变化时建立 64 个格子
    void refresh();  // 依据棋盘状态刷新格子外观与棋子
    void setLastMove(int from, int to);
    void clearSelection();

    // 某格相对本控件的矩形（供定位 / 测试）
    QRect cellGeometry(int index) const;

signals:
    // 已确认落在合法目标格的着法（是否升变 / 升变为何由宿主决定）
    void moveRequested(int from, int to);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum class DragState { Idle, Pressed, Dragging };

    int cellAt(const QPoint &localPos) const;
    int cellAtGlobal(const QPoint &globalPos) const;
    bool isMovablePiece(int square) const;
    bool isTargetOf(int from, int to) const;
    QList<int> targetsOf(int from) const;
    void applyCell(int index);
    void select(int square);
    void clearSelectionInternal();

    void beginPress(int square, const QPoint &globalPos);
    void updateDrag(const QPoint &globalPos);
    void endPress(int dropSquare, const QPoint &globalPos);
    void showGhost(int square, const QPoint &globalPos);
    void moveGhost(const QPoint &globalPos);
    void hideGhost();

    core::ChessGame *m_game = nullptr;
    QGridLayout *m_grid = nullptr;
    QList<QToolButton *> m_cells; // 下标 = 格子索引（0 = a1）
    QLabel *m_ghost = nullptr;    // 拖动时跟随光标的棋子浮影（懒创建）
    int m_cellSize = 52;

    bool m_interactive = false;
    int m_selected = -1;
    QList<int> m_targets;
    int m_lastFrom = -1;
    int m_lastTo = -1;

    DragState m_dragState = DragState::Idle;
    int m_dragFrom = -1;    // 正在拖动的棋子起点（Idle / 空白按下时为 -1）
    int m_pressIndex = -1;  // 按下时命中的格子（-1 = 棋盘外）
    QPoint m_pressGlobal;   // 按下时的全局坐标（拖动阈值判定用）
    bool m_pressOnSelected = false; // 按下时该格已被选中（用于「再次点击取消选中」）
};

// 国际象棋窗口（宿主经 IMiniGamePlugin::createView 创建）。
class ChessView : public MiniGameView, public IMiniGameCompanionSource {
    Q_OBJECT
public:
    explicit ChessView(const MiniGameContext &ctx, QWidget *parent = nullptr);
    ~ChessView() override;

    // 以持久化配置载入并开新局（宿主每次打开窗口前调用）
    void reload() override;

    // 展示本局结算信息（奖励 / 纪录），由宿主结算后回填
    void setRewardText(const QString &text) override;

    // 陪玩自描述（EX4）：把象棋状态折算为中立快照（危险 = 被将军）
    bool companionSnapshot(core::GameSnapshot *out) const override;

private:
    QWidget *buildConfigBar();
    QWidget *buildStatusBar();

    void ensureEngine();     // 按当前路径启动 / 复用引擎
    void applyEngineLevel(); // 下发 Skill Level

    void startNewGame();
    void resignGame();
    void finishGame(); // 依当前状态收尾并上报结算

    void onEngineReady();
    void onEngineBestMove(const QString &uci);
    void onEngineFailed(const QString &reason);
    // 棋盘已完成「点击 / 拖动」交互、确认落在合法目标格 → 生成着法并提交
    void onMoveRequested(int from, int to);

    void applyMove(const core::ChessMove &move, bool byHuman);
    void requestEngineMove();

    void updateBoard(); // 刷新棋盘并同步「是否可交互」
    void updateStatus();
    void setEngineStatus(const QString &text);

    void persistSettings();
    void announce(const QString &pose, const QString &sceneKey, int ttlMs);
    char askPromotion();

    PetController *m_controller = nullptr;
    model::Database *m_db = nullptr;

    UciEngine *m_engine = nullptr;
    core::ChessGame m_game;

    QLineEdit *m_enginePathEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QComboBox *m_levelBox = nullptr;
    QComboBox *m_sideBox = nullptr;
    QLabel *m_engineLabel = nullptr;
    ChessBoardWidget *m_board = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_rewardLabel = nullptr;
    QPushButton *m_newGameButton = nullptr;
    QPushButton *m_resignButton = nullptr;

    QTimer *m_timer = nullptr; // 用时
    qint64 m_elapsedMs = 0;
    int m_moveCount = 0;
    bool m_timerRunning = false;
    bool m_finished = false;
    bool m_engineThinking = false;
    bool m_loading = false; // 载入控件期间抑制信号引起的动作

    int m_levelIndex = 0;
    bool m_humanIsWhite = true;

    core::ChessMove m_lastMove;
};

} // namespace whalepet
