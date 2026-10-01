#include "minigame/minesweeper/MinesweeperView.h"

#include "model/Database.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"
#include "viewmodel/PetController.h"

#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace whalepet {

namespace {

// 棋盘格按钮：把左右键翻译成回调（不用信号，避免额外的 moc 依赖）。
class MineCellButton : public QToolButton {
public:
    MineCellButton(int index, std::function<void(int)> onReveal, std::function<void(int)> onFlag,
                   QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_index(index)
        , m_onReveal(std::move(onReveal))
        , m_onFlag(std::move(onFlag))
    {
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(false);
        setProperty("cellState", QStringLiteral("hidden"));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_onReveal) {
                m_onReveal(m_index);
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            if (m_onFlag) {
                m_onFlag(m_index);
            }
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }

private:
    int m_index = 0;
    std::function<void(int)> m_onReveal;
    std::function<void(int)> m_onFlag;
};

} // namespace

// ---------------------------------------------------------------------------
// MineBoardWidget
// ---------------------------------------------------------------------------

MineBoardWidget::MineBoardWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("MineBoard")); // 供全局样式表定位棋盘格
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(1);
}

void MineBoardWidget::setGame(core::Minesweeper *game)
{
    m_game = game;
}

void MineBoardWidget::rebuild()
{
    // 清理旧格（先从布局摘除再延迟释放，避免事件处理中析构自身）
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    m_buttons.clear();

    if (m_game == nullptr) {
        return;
    }

    const int w = m_game->width();
    const int h = m_game->height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int index = y * w + x;
            auto *btn = new MineCellButton(
                index, [this](int i) { emit revealRequested(i); },
                [this](int i) { emit flagRequested(i); }, this);
            btn->setFixedSize(m_cellSize, m_cellSize);
            m_grid->addWidget(btn, y, x);
            m_buttons.append(btn);
        }
    }

    refresh();
    m_grid->activate();
    setFixedSize(m_grid->sizeHint());
}

void MineBoardWidget::refresh()
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        applyCell(i);
    }
}

void MineBoardWidget::applyCell(int index)
{
    QToolButton *btn = m_buttons.value(index);
    if (btn == nullptr || m_game == nullptr) {
        return;
    }
    const core::MineCell &c = m_game->cell(index);

    QString state = QStringLiteral("hidden");
    QString text;
    if (c.revealed) {
        if (c.mine) {
            state = QStringLiteral("mine");
            text = QStringLiteral("✱");
        } else {
            state = QStringLiteral("revealed");
            if (c.adjacent > 0) {
                text = QString::number(c.adjacent);
            }
        }
    } else if (c.flagged) {
        state = QStringLiteral("flag");
        text = QStringLiteral("⚑");
    }

    btn->setText(text);
    if (btn->property("cellState").toString() != state) {
        btn->setProperty("cellState", state);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
    // 已翻开的格子不再接收鼠标事件：避免 hover / pressed 影响观感
    btn->setAttribute(Qt::WA_TransparentForMouseEvents, c.revealed);
}

// ---------------------------------------------------------------------------
// MinesweeperView
// ---------------------------------------------------------------------------

MinesweeperView::MinesweeperView(const MiniGameContext &ctx, QWidget *parent)
    : MiniGameView(parent)
    , m_controller(ctx.controller)
    , m_db(ctx.db)
{
    setWindowTitle(QStringLiteral("鲸鱼娘 · 扫雷"));

    m_board = new MineBoardWidget(this);
    m_board->setGame(&m_game);
    connect(m_board, &MineBoardWidget::revealRequested, this,
            &MinesweeperView::onRevealRequested);
    connect(m_board, &MineBoardWidget::flagRequested, this, &MinesweeperView::onFlagRequested);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_elapsedMs += 1000;
        updateStatus();
    });

    m_rewardLabel = new QLabel(this);
    m_rewardLabel->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->addWidget(buildConfigBar());
    root->addWidget(m_board, 0, Qt::AlignHCenter);
    root->addWidget(buildStatusBar());
    root->addWidget(m_rewardLabel);

    reload();
}

QWidget *MinesweeperView::buildConfigBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("难度"), bar));
    m_presetBox = new QComboBox(bar);
    for (const core::MinePresetDef &p : core::kMinePresets) {
        m_presetBox->addItem(QStringLiteral("%1（%2×%3 · %4 雷）")
                                 .arg(QString::fromUtf8(p.name))
                                 .arg(p.width)
                                 .arg(p.height)
                                 .arg(p.mines));
    }
    m_presetBox->addItem(QStringLiteral("自定义…"));
    row->addWidget(m_presetBox);
    row->addStretch();
    outer->addLayout(row);

    // 自定义参数行：宽 / 高 / 雷数 + 开始
    m_customRow = new QWidget(bar);
    auto *custom = new QHBoxLayout(m_customRow);
    custom->setContentsMargins(0, 0, 0, 0);
    custom->addWidget(new QLabel(QStringLiteral("宽"), m_customRow));
    m_widthBox = new QSpinBox(m_customRow);
    m_widthBox->setRange(core::kMineMinWidth, core::kMineMaxWidth);
    custom->addWidget(m_widthBox);
    custom->addWidget(new QLabel(QStringLiteral("高"), m_customRow));
    m_heightBox = new QSpinBox(m_customRow);
    m_heightBox->setRange(core::kMineMinHeight, core::kMineMaxHeight);
    custom->addWidget(m_heightBox);
    custom->addWidget(new QLabel(QStringLiteral("雷数"), m_customRow));
    m_minesBox = new QSpinBox(m_customRow);
    m_minesBox->setRange(core::kMineMinMines, core::kMineMaxWidth * core::kMineMaxHeight - 1);
    custom->addWidget(m_minesBox);
    m_applyButton = new QPushButton(QStringLiteral("开始自定义"), m_customRow);
    custom->addWidget(m_applyButton);
    custom->addStretch();
    outer->addWidget(m_customRow);

    m_difficultyLabel = new QLabel(bar);
    outer->addWidget(m_difficultyLabel);

    connect(m_presetBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { applyPreset(index); });
    connect(m_widthBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { updateMineRange(); });
    connect(m_heightBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { updateMineRange(); });
    connect(m_applyButton, &QPushButton::clicked, this, &MinesweeperView::applyCustomConfig);

    return bar;
}

QWidget *MinesweeperView::buildStatusBar()
{
    auto *bar = new QWidget(this);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel(bar);
    row->addWidget(m_statusLabel);
    row->addStretch();

    m_newGameButton = new QPushButton(QStringLiteral("重新开始"), bar);
    connect(m_newGameButton, &QPushButton::clicked, this,
            [this] { startNewGame(m_game.config()); });
    row->addWidget(m_newGameButton);

    return bar;
}

void MinesweeperView::reload()
{
    int preset = static_cast<int>(core::MinePreset::Beginner);
    int cw = 9;
    int ch = 9;
    int cm = 10;
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        if (repo.load(data)) {
            preset = data.minigamePreset;
            cw = data.minigameCustomWidth;
            ch = data.minigameCustomHeight;
            cm = data.minigameCustomMines;
        }
    }
    if (preset < 0 || preset > core::kMinePresetCount) {
        preset = static_cast<int>(core::MinePreset::Beginner);
    }

    m_loading = true;
    m_presetBox->setCurrentIndex(preset); // m_loading 期间不触发开局
    m_widthBox->setValue(qBound(core::kMineMinWidth, cw, core::kMineMaxWidth));
    m_heightBox->setValue(qBound(core::kMineMinHeight, ch, core::kMineMaxHeight));
    updateMineRange();
    m_minesBox->setValue(qBound(core::kMineMinMines, cm, m_minesBox->maximum()));
    m_loading = false;

    updateCustomVisibility();

    core::MineConfig cfg;
    if (preset == static_cast<int>(core::MinePreset::Custom)) {
        cfg = core::MineConfig{m_widthBox->value(), m_heightBox->value(), m_minesBox->value()};
        if (!core::mineConfigValid(cfg)) {
            cfg = core::mineConfigOfPreset(core::MinePreset::Beginner);
        }
    } else {
        cfg = core::mineConfigOfPreset(static_cast<core::MinePreset>(preset));
    }
    startNewGame(cfg);
}

void MinesweeperView::applyPreset(int index)
{
    if (m_loading) {
        return;
    }
    updateCustomVisibility();
    if (index < 0 || index >= core::kMinePresetCount) {
        return; // 「自定义…」：等待「开始自定义」按钮
    }
    const core::MineConfig cfg = core::mineConfigOfPreset(core::kMinePresets[index].preset);
    persistConfig(static_cast<int>(core::kMinePresets[index].preset), cfg);
    startNewGame(cfg);
}

void MinesweeperView::applyCustomConfig()
{
    core::MineConfig cfg;
    cfg.width = m_widthBox->value();
    cfg.height = m_heightBox->value();
    cfg.mines = m_minesBox->value();
    if (!core::mineConfigValid(cfg)) {
        QMessageBox::warning(
            this, QStringLiteral("参数不合法"),
            QStringLiteral("尺寸需在 %1×%2 ~ %3×%4 之间，雷数需在 %5 ~ %6 之间（至少留 1 个空格）。")
                .arg(core::kMineMinWidth)
                .arg(core::kMineMinHeight)
                .arg(core::kMineMaxWidth)
                .arg(core::kMineMaxHeight)
                .arg(core::kMineMinMines)
                .arg(core::mineMaxMines(cfg)));
        return;
    }
    persistConfig(static_cast<int>(core::MinePreset::Custom), cfg);
    startNewGame(cfg);
}

void MinesweeperView::updateCustomVisibility()
{
    const bool custom = (m_presetBox->currentIndex() >= core::kMinePresetCount);
    m_customRow->setVisible(custom);
    m_applyButton->setVisible(custom);
}

void MinesweeperView::updateMineRange()
{
    core::MineConfig cfg;
    cfg.width = m_widthBox->value();
    cfg.height = m_heightBox->value();
    cfg.mines = m_minesBox->value();
    m_minesBox->setMaximum(core::mineMaxMines(cfg));
}

void MinesweeperView::startNewGame(const core::MineConfig &cfg)
{
    m_game.setRandom(&m_rng);
    m_game.newGame(cfg);

    m_elapsedMs = 0;
    m_timerRunning = false;
    m_finished = false;
    m_chainAnnounced = false;
    m_timer->stop();

    m_board->rebuild();
    m_newGameButton->setText(QStringLiteral("重新开始"));
    setRewardText(QString());
    updateDifficultyLabel();
    updateStatus();
    adjustSize();

    announce(QStringLiteral("game-think"), QStringLiteral("game.start"), 5000);
}

void MinesweeperView::setRewardText(const QString &text)
{
    if (m_rewardLabel != nullptr) {
        m_rewardLabel->setText(text);
    }
}

void MinesweeperView::updateDifficultyLabel()
{
    m_difficultyLabel->setText(QStringLiteral("当前难度：%1")
                                   .arg(QString::fromStdString(core::mineConfigLabel(m_game.config()))));
}

void MinesweeperView::updateStatus()
{
    QString state = QStringLiteral("进行中");
    switch (m_game.status()) {
    case core::MineStatus::Ready:
    case core::MineStatus::Playing:
        state = QStringLiteral("进行中");
        break;
    case core::MineStatus::Won:
        state = QStringLiteral("通关");
        break;
    case core::MineStatus::Lost:
        state = QStringLiteral("踩雷");
        break;
    }

    m_statusLabel->setText(QStringLiteral("剩余雷数 %1 / %2 · 已用 %3 秒 · %4")
                               .arg(m_game.remainingMines())
                               .arg(m_game.mineCount())
                               .arg(m_elapsedMs / 1000)
                               .arg(state));
}

void MinesweeperView::persistConfig(int preset, const core::MineConfig &cfg)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    repo.load(data);
    data.minigamePreset = preset;
    if (preset == static_cast<int>(core::MinePreset::Custom)) {
        data.minigameCustomWidth = cfg.width;
        data.minigameCustomHeight = cfg.height;
        data.minigameCustomMines = cfg.mines;
    }
    if (!repo.save(data)) {
        qWarning() << "[MinesweeperView] 小游戏配置持久化失败";
    }
}

void MinesweeperView::onRevealRequested(int index)
{
    if (m_finished) {
        return;
    }
    const core::MineMove move = m_game.reveal(index);
    if (!move.changed) {
        return;
    }

    if (!m_timerRunning && m_game.minesPlaced()) {
        m_timerRunning = true;
        m_timer->start();
    }

    m_board->refresh();
    updateStatus();

    if (move.exploded) {
        announce(QStringLiteral("game-cheat"), QStringLiteral("game.boom"), 2200);
        finishGame();
        return;
    }
    if (move.won) {
        announce(QStringLiteral("game-win"), QStringLiteral("game.win"), 4000);
        finishGame();
        return;
    }
    // 连翻里程碑：每局只播一次，避免刷屏
    if (!m_chainAnnounced && move.chainPeak >= 5) {
        m_chainAnnounced = true;
        announce(QStringLiteral("game-happy"), QStringLiteral("game.chain"), 2000);
    }
}

void MinesweeperView::onFlagRequested(int index)
{
    if (m_finished) {
        return;
    }
    if (!m_game.toggleFlag(index).changed) {
        return;
    }
    m_board->refresh();
    updateStatus();
}

void MinesweeperView::finishGame()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_timer->stop();
    m_timerRunning = false;
    m_board->refresh();

    const core::MineSummary s = m_game.summary();
    const int presetIndex = static_cast<int>(core::minePresetOfConfig(m_game.config()));
    // 折算为通用结算契约；难度文案用实际棋盘参数（自定义时也能显示完整尺寸）
    core::MiniGameResult result = core::mineGameResult(s, presetIndex, m_elapsedMs);
    result.difficultyLabel = core::mineConfigLabel(m_game.config());
    emit gameFinished(result);

    m_newGameButton->setText(QStringLiteral("再来一局"));
    updateStatus();

    // 失败：踩雷姿态播完后，再补一次「结算」表现（参考项目 endGame 的 game-lose）。
    // 用 m_finished 作守卫：期间若已开新局（startNewGame 置 false）则不再补播。
    if (!s.won) {
        QTimer::singleShot(1600, this, [this] {
            if (m_finished) {
                announce(QStringLiteral("game-lose"), QStringLiteral("game.lose"), 3500);
            }
        });
    }
}

void MinesweeperView::announce(const QString &pose, const QString &sceneKey, int ttlMs)
{
    if (m_controller != nullptr) {
        m_controller->presentGame(pose, sceneKey, ttlMs);
    }
}

} // namespace whalepet
