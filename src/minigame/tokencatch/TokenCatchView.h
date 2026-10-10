#pragma once

// 小游戏插件「接 Token」的视图层实现。
//
// 玩法（「接元宝」类下落接取玩法的换皮，语义见 core/TokenCatch.h）：
//   - 底部接取区（两格，左格绘制「鲸」，即鲸鱼娘）左右移动，接住落下的 Token（界面字「币」）；
//   - 接到**白饭**（界面字「饭」）→ 本局立即结束，并播放 daily-picnic 立绘
//     （core::kTokenCatchRicePose，即资源 dsh-whale-state-daily-picnic.webp）；
//   - 达成目标 Token 数即通关（用时最短计入个人最快纪录）；时限 60 秒，到点未达成即结束。
//
// 边界：玩法规则全部委托 core::TokenCatch（零 Qt、可单测）；本类只做「界面 + 交互 +
// 立绘/台词表现」；结算经 MiniGameView::gameFinished 上报**通用的** core::MiniGameResult，
// 宿主统一处理养成奖励与成就（玩法细节不外泄给宿主）。
//
// 陪玩（EX4）：本视图**额外**实现 IMiniGameCompanionSource —— 这是本插件作为
// 「陪玩侧通用聚合」实战测试用例的关键：只多写一个 companionSnapshot()，
// 陪玩侧（viewmodel::MiniGameCompanionSource）与宿主一行不改即可接入。
//
// 外观仅依赖全局样式表（resources/qt-ui/default.qss + project.qss），不自行设计样式。
// 网格尺寸按 docs/mapinit.md 由自身参数显式计算，禁止用布局返回值定尺寸。

#include "core/TokenCatch.h"
#include "minigame/MiniGameCompanionSource.h"
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

// 接取棋盘控件：按 kTokenCatchCols × kTokenCatchRows 生成格子按钮，
// 受全局样式表控制外观；左键点击某列 → 接取区移到该列。
class TokenCatchBoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit TokenCatchBoardWidget(QWidget *parent = nullptr);

    void setGame(core::TokenCatch *game); // 不接管所有权
    void rebuild();                       // 依据网格参数重建格子（每次重开 / 切难度都调用）
    void refresh();                       // 依据对局状态刷新格子外观（仅属性变化时才 polish）

signals:
    void columnClicked(int column);

private:
    void applyCell(int index);

    core::TokenCatch *m_game = nullptr;
    QGridLayout *m_grid = nullptr;
    QList<QToolButton *> m_cells;
    int m_cellSize = 26;
};

// 接 Token 窗口（宿主经 IMiniGamePlugin::createView 创建）
class TokenCatchView : public MiniGameView, public IMiniGameCompanionSource {
    Q_OBJECT
public:
    explicit TokenCatchView(const MiniGameContext &ctx, QWidget *parent = nullptr);

    // 按当前难度重开一局（宿主每次打开窗口前调用）
    void reload() override;

    // 展示本局结算信息（奖励 / 纪录），由宿主结算后回填
    void setRewardText(const QString &text) override;

    // 陪玩自描述（EX4）：把「接 Token」状态折算为中立快照，陪玩侧零改动即可接入
    bool companionSnapshot(core::GameSnapshot *out) const override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildConfigBar();
    QWidget *buildStatusBar();

    void startNewGame(core::TokenCatchPreset preset);
    void applyPreset(int index);
    void ensureStarted();
    void onMoveRequested(int delta);
    void onColumnClicked(int column);
    void onTick();
    void updateDifficultyLabel();
    void updateStatus();
    void finishGame();
    bool handleMoveKey(QKeyEvent *event);
    void announce(const QString &pose, const QString &sceneKey, int ttlMs);

    PetController *m_controller = nullptr;

    core::TokenCatch m_game;
    core::SystemRandom m_rng;

    QComboBox *m_presetBox = nullptr;
    QLabel *m_difficultyLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_rewardLabel = nullptr;
    QPushButton *m_restartButton = nullptr;
    TokenCatchBoardWidget *m_board = nullptr;

    QTimer *m_loop = nullptr;  // 一帧 kTokenCatchTickMs 毫秒（首次有效输入才开始）
    bool m_finished = false;
    bool m_loading = false;    // 载入控件期间抑制「切换即开局」
    int m_chainAnnounced = 0;  // 连击播报里程碑（已播到第几个 Token）
};

} // namespace whalepet
