#pragma once

// 小游戏插件「扫雷」的视图层实现（由原 view/MinesweeperDialog 迁移而来）。
//
// 功能（重构后保持不变）：
//   - 内置 3 个预设（初级 9×9·10 / 中级 16×16·40 / 高级 30×16·99）+ 自定义（宽 / 高 / 雷数）；
//   - 界面清晰展示「当前难度与参数」，并支持在预设与自定义之间切换；
//   - 进行游戏时保留参考项目原有的鲸鱼娘「立绘变化 + 台词播报」：
//       开局 → game-think / game.start；连翻 → game-happy / game.chain；
//       踩雷 → game-cheat / game.boom；通关 → game-win / game.win；失败 → game-lose / game.lose。
//
// 边界：本类只做「界面 + 交互」，棋盘规则全部委托 core::Minesweeper（零 Qt，可单测）；
// 结算经 MiniGameView::gameFinished 上报**通用的** core::MiniGameResult，宿主统一处理
// 养成奖励与成就（玩法细节到此为止，不外泄给宿主）。
// 外观仅依赖全局样式表（resources/qt-ui/default.qss + project.qss），不自行设计样式。

#include "core/Minesweeper.h"
#include "minigame/MiniGameCompanionSource.h"
#include "minigame/MiniGamePlugin.h"

#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;

namespace whalepet {

// 棋盘控件：网格按钮，受全局样式表控制外观；左键翻格、右键插旗
class MineBoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit MineBoardWidget(QWidget *parent = nullptr);

    void setGame(core::Minesweeper *game); // 不接管所有权
    void rebuild();                        // 依据当前棋盘重建格子
    void refresh();                        // 依据棋盘状态刷新格子外观

signals:
    void revealRequested(int index);
    void flagRequested(int index);

private:
    void applyCell(int index);

    core::Minesweeper *m_game = nullptr;
    QGridLayout *m_grid = nullptr;
    QList<QToolButton *> m_buttons;
    int m_cellSize = 26;
};

// 扫雷窗口（宿主经 IMiniGamePlugin::createView 创建）
class MinesweeperView : public MiniGameView, public IMiniGameCompanionSource {
    Q_OBJECT
public:
    explicit MinesweeperView(const MiniGameContext &ctx, QWidget *parent = nullptr);

    // 以持久化配置载入并开新局（打开前调用）
    void reload() override;

    // 展示本局结算信息（奖励 / 纪录），由宿主结算后回填
    void setRewardText(const QString &text) override;

    // 陪玩自描述（EX4）：把扫雷状态折算为中立快照，陪玩侧零改动即可接入
    bool companionSnapshot(core::GameSnapshot *out) const override;

private:
    QWidget *buildConfigBar();
    QWidget *buildStatusBar();

    void startNewGame(const core::MineConfig &cfg);
    void applyPreset(int index);
    void applyCustomConfig();
    void updateCustomVisibility();
    void updateMineRange();
    void updateDifficultyLabel();
    void updateStatus();
    void persistConfig(int preset, const core::MineConfig &cfg);

    void onRevealRequested(int index);
    void onFlagRequested(int index);
    void finishGame();
    void announce(const QString &pose, const QString &sceneKey, int ttlMs);

    PetController *m_controller = nullptr;
    model::Database *m_db = nullptr;

    core::Minesweeper m_game;
    core::SystemRandom m_rng; // 布雷随机源

    QComboBox *m_presetBox = nullptr;
    QWidget *m_customRow = nullptr;
    QSpinBox *m_widthBox = nullptr;
    QSpinBox *m_heightBox = nullptr;
    QSpinBox *m_minesBox = nullptr;
    QPushButton *m_applyButton = nullptr;
    QLabel *m_difficultyLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_rewardLabel = nullptr;
    QPushButton *m_newGameButton = nullptr;
    MineBoardWidget *m_board = nullptr;

    QTimer *m_timer = nullptr; // 用时
    qint64 m_elapsedMs = 0;
    bool m_timerRunning = false;
    bool m_finished = false;
    bool m_chainAnnounced = false;
    bool m_loading = false; // 载入控件期间抑制「切换即开局」
};

} // namespace whalepet
