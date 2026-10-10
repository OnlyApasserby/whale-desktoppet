#include "minigame/tokencatch/TokenCatchView.h"

#include "viewmodel/PetController.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace whalepet {

namespace {

// 棋盘格按钮：把左键翻译成「点击列」回调（不用信号，避免额外的 moc 依赖）。
class TokenCatchCellButton : public QToolButton {
public:
    TokenCatchCellButton(int index, std::function<void(int)> onColumnClick,
                         QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_index(index)
        , m_onColumnClick(std::move(onColumnClick))
    {
        setFocusPolicy(Qt::NoFocus); // 方向键必须落到窗口本体（否则被格子吞掉）
        setAutoRaise(false);
        setProperty("cellState", QStringLiteral("empty"));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_onColumnClick) {
                m_onColumnClick(m_index);
            }
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }

private:
    int m_index = 0;
    std::function<void(int)> m_onColumnClick;
};

} // namespace

// ---------------------------------------------------------------------------
// TokenCatchBoardWidget
// ---------------------------------------------------------------------------

TokenCatchBoardWidget::TokenCatchBoardWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TokenCatchBoard")); // 供全局样式表定位格子
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(1);
}

void TokenCatchBoardWidget::setGame(core::TokenCatch *game)
{
    m_game = game;
}

void TokenCatchBoardWidget::rebuild()
{
    // 清理旧格（先从布局摘除再延迟释放，避免事件处理中析构自身）
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    m_cells.clear();

    if (m_game == nullptr) {
        return;
    }

    for (int row = 0; row < core::kTokenCatchRows; ++row) {
        for (int column = 0; column < core::kTokenCatchCols; ++column) {
            const int index = row * core::kTokenCatchCols + column;
            auto *btn = new TokenCatchCellButton(
                index, [this](int i) { emit columnClicked(i % core::kTokenCatchCols); }, this);
            btn->setFixedSize(m_cellSize, m_cellSize);
            m_grid->addWidget(btn, row, column);
            m_cells.append(btn);
        }
    }

    refresh();

    // 尺寸必须**显式计算**，不能依赖 m_grid->sizeHint()：
    // 运行中（窗口已显示）重建时该返回值会退化为 (0,0)，而 setFixedSize 是粘性的
    // —— 写死成 0×0 之后 min/max 永久为 0，棋盘再也显示不出来（必须重启程序才恢复）。
    // 见 docs/mapinit.md（同源问题：docs/pitfalls/ TRAP-P6-005 根因 B）。
    const int spacing = m_grid->spacing();
    const int boardW = core::kTokenCatchCols * m_cellSize
                       + std::max(0, core::kTokenCatchCols - 1) * spacing;
    const int boardH = core::kTokenCatchRows * m_cellSize
                       + std::max(0, core::kTokenCatchRows - 1) * spacing;
    setFixedSize(boardW, boardH);

    m_grid->activate(); // 让子控件几何按新尺寸立即生效
}

void TokenCatchBoardWidget::refresh()
{
    for (int i = 0; i < m_cells.size(); ++i) {
        applyCell(i);
    }
}

void TokenCatchBoardWidget::applyCell(int index)
{
    QToolButton *btn = m_cells.value(index);
    if (btn == nullptr || m_game == nullptr) {
        return;
    }
    const int row = index / core::kTokenCatchCols;
    const int column = index % core::kTokenCatchCols;

    QString state = QStringLiteral("empty");
    QString text;
    if (row == core::kTokenCatchRows - 1 && m_game->catcherCovers(column)) {
        state = QStringLiteral("catcher");
        // 接取区占两格：只在左格画「鲸」，右格同色延伸（避免看起来像两只鲸鱼）
        if (column == m_game->catcherColumn()) {
            text = QStringLiteral("鲸");
        }
    } else {
        core::FallingKind kind = core::FallingKind::Token;
        if (m_game->itemAt(column, row, &kind)) {
            if (kind == core::FallingKind::Rice) {
                state = QStringLiteral("rice");
                text = QStringLiteral("饭");
            } else {
                state = QStringLiteral("token");
                text = QStringLiteral("币");
            }
        }
    }

    if (btn->text() != text) {
        btn->setText(text);
    }
    if (btn->property("cellState").toString() != state) {
        btn->setProperty("cellState", state);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

// ---------------------------------------------------------------------------
// TokenCatchView
// ---------------------------------------------------------------------------

TokenCatchView::TokenCatchView(const MiniGameContext &ctx, QWidget *parent)
    : MiniGameView(parent)
    , m_controller(ctx.controller)
{
    // ctx.db 不参与：本插件不落库配置（难度每次在窗口内选择，见插件 configSummary 说明）
    setWindowTitle(QStringLiteral("鲸鱼娘 · 接Token"));
    setFocusPolicy(Qt::StrongFocus); // 方向键 / 空格需要落到窗口本体

    m_board = new TokenCatchBoardWidget(this);
    m_board->setGame(&m_game);
    connect(m_board, &TokenCatchBoardWidget::columnClicked, this, &TokenCatchView::onColumnClicked);

    m_loop = new QTimer(this);
    m_loop->setInterval(core::kTokenCatchTickMs);
    connect(m_loop, &QTimer::timeout, this, &TokenCatchView::onTick);

    m_rewardLabel = new QLabel(this);
    m_rewardLabel->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->addWidget(buildConfigBar());
    root->addWidget(m_board, 0, Qt::AlignHCenter);
    root->addWidget(buildStatusBar());
    root->addWidget(m_rewardLabel);

    reload();
}

QWidget *TokenCatchView::buildConfigBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("难度"), bar));
    m_presetBox = new QComboBox(bar);
    for (const core::TokenCatchPresetDef &p : core::kTokenCatchPresets) {
        m_presetBox->addItem(QStringLiteral("%1（目标 %2 · %3 秒）")
                                 .arg(QString::fromUtf8(p.name))
                                 .arg(p.targetTokens)
                                 .arg(p.durationTicks * core::kTokenCatchTickMs / 1000));
    }
    row->addWidget(m_presetBox);
    row->addStretch();
    outer->addLayout(row);

    m_difficultyLabel = new QLabel(bar);
    outer->addWidget(m_difficultyLabel);

    auto *rule = new QLabel(
        QStringLiteral("← / → 或 A / D 移动接取区（也可点击某列）。接住「币」（Token）+1 分；"
                       "接住「饭」（白饭）本局立即结束；漏接「币」只清空连击。达成目标分即通关。"),
        bar);
    rule->setWordWrap(true);
    outer->addWidget(rule);

    connect(m_presetBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { applyPreset(index); });
    // 下拉框默认会把上下 / 左右方向键当成「切换选项」→ 截获后交给移动逻辑
    // （同 KittenView，见 docs/pitfalls/ TRAP-P6-005）
    m_presetBox->installEventFilter(this);

    return bar;
}

QWidget *TokenCatchView::buildStatusBar()
{
    auto *bar = new QWidget(this);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel(bar);
    row->addWidget(m_statusLabel);
    row->addStretch();

    m_restartButton = new QPushButton(QStringLiteral("重新开始"), bar);
    m_restartButton->setFocusPolicy(Qt::NoFocus); // 空格归「开始 / 重开」，不被按钮吃掉
    connect(m_restartButton, &QPushButton::clicked, this,
            [this] { startNewGame(m_game.preset()); });
    row->addWidget(m_restartButton);

    return bar;
}

void TokenCatchView::reload()
{
    startNewGame(m_game.preset());
}

void TokenCatchView::setRewardText(const QString &text)
{
    if (m_rewardLabel != nullptr) {
        m_rewardLabel->setText(text);
    }
}

// 陪玩自描述（EX4）：把「接 Token」的私有状态折算为**中立的** GameSnapshot。
// 仅此一处与陪玩相关；陪玩侧通用聚合与宿主一行不改即可接入（本插件的立项目的）。
bool TokenCatchView::companionSnapshot(core::GameSnapshot *out) const
{
    if (out == nullptr) {
        return false;
    }
    core::GameSnapshot snap;
    snap.available = true;
    snap.gameId = "tokencatch";
    snap.running = m_game.started(); // 仅「真的在局中」才占用陪玩态（Ready / Ended 不占）
    snap.finished = m_game.ended();
    snap.won = m_game.won();
    snap.level = m_game.level(); // 节奏档：每接满 5 个 Token +1（增大即进阶）
    snap.progressDone = m_game.tokensCaught();
    snap.progressTotal = m_game.targetTokens();
    snap.score = m_game.maxChain(); // 连击峰值
    snap.danger = m_game.danger();  // 白饭进入最后 kTokenCatchDangerRows 行 → 危险
    *out = snap;
    return true;
}

void TokenCatchView::showEvent(QShowEvent *event)
{
    MiniGameView::showEvent(event);
    setFocus(Qt::ActiveWindowFocusReason);
    // 从隐藏恢复：局中被打断的推进继续（与 hideEvent 成对）
    if (!m_finished && m_game.started() && !m_loop->isActive()) {
        m_loop->start();
    }
}

void TokenCatchView::hideEvent(QHideEvent *event)
{
    MiniGameView::hideEvent(event);
    // 宿主只缓存窗口不销毁：隐藏期间必须暂停推进，否则会在后台默默接到白饭结束本局
    if (m_loop->isActive()) {
        m_loop->stop();
    }
}

void TokenCatchView::keyPressEvent(QKeyEvent *event)
{
    if (handleMoveKey(event)) {
        event->accept();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_finished) {
            startNewGame(m_game.preset()); // 结束后一键再来一局
        } else {
            ensureStarted();
        }
        event->accept();
        return;
    default:
        break;
    }
    MiniGameView::keyPressEvent(event);
}

bool TokenCatchView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_presetBox && event->type() == QEvent::KeyPress) {
        if (handleMoveKey(static_cast<QKeyEvent *>(event))) {
            return true; // 吞掉事件：方向键归移动，不得切成另一个难度
        }
    }
    return MiniGameView::eventFilter(watched, event);
}

bool TokenCatchView::handleMoveKey(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        onMoveRequested(-1);
        return true;
    case Qt::Key_Right:
    case Qt::Key_D:
        onMoveRequested(1);
        return true;
    default:
        return false;
    }
}

void TokenCatchView::startNewGame(core::TokenCatchPreset preset)
{
    m_loop->stop(); // 与 finishGame 成对：任何时候重开都先停表
    m_game.setRandom(&m_rng);
    m_game.newGame(preset);
    m_finished = false;
    m_chainAnnounced = 0;

    m_loading = true;
    m_presetBox->setCurrentIndex(core::tokenCatchPresetIndexOf(preset));
    m_loading = false;

    m_board->rebuild();
    m_restartButton->setText(QStringLiteral("重新开始"));
    setRewardText(QString());
    updateDifficultyLabel();
    updateStatus();
    adjustSize();
}

void TokenCatchView::applyPreset(int index)
{
    if (m_loading) {
        return; // reload() 载入控件期间不触发开局
    }
    startNewGame(core::tokenCatchPresetAt(index));
}

void TokenCatchView::ensureStarted()
{
    if (m_finished) {
        return;
    }
    if (m_game.start()) {
        announce(QStringLiteral("game-think"), QString::fromLatin1(core::kTokenCatchSceneStart),
                 4000);
    }
    if (m_game.started() && !m_loop->isActive()) {
        m_loop->start(); // 首次有效输入才开始推进（与扫雷 / 找小猫的计时口径一致）
    }
    updateStatus();
}

void TokenCatchView::onMoveRequested(int delta)
{
    if (m_finished) {
        return;
    }
    ensureStarted();
    if (m_game.moveCatcher(delta)) {
        m_board->refresh();
        updateStatus();
    }
}

void TokenCatchView::onColumnClicked(int column)
{
    if (m_finished) {
        return;
    }
    ensureStarted();
    if (m_game.moveCatcherTo(column)) {
        m_board->refresh();
        updateStatus();
    }
}

void TokenCatchView::onTick()
{
    if (m_finished || !m_game.started()) {
        m_loop->stop();
        return;
    }

    const core::TokenCatchTick step = m_game.tick();
    m_board->refresh();
    updateStatus();

    if (step.levelUp) {
        // 节奏档提升与连击里程碑同一帧时只播一次（档位更有信息量）
        announce(QStringLiteral("game-happy"), QString::fromLatin1(core::kTokenCatchSceneLevelUp),
                 2000);
    } else if (step.caughtTokens > 0
               && m_game.tokensCaught() >= core::kTokenCatchTokensPerLevel
               && m_game.tokensCaught() % core::kTokenCatchTokensPerLevel == 0
               && m_game.tokensCaught() != m_chainAnnounced) {
        m_chainAnnounced = m_game.tokensCaught();
        announce(QStringLiteral("game-happy"), QString::fromLatin1(core::kTokenCatchSceneChain),
                 2000);
    }

    if (step.ended) {
        finishGame();
    }
}

void TokenCatchView::updateDifficultyLabel()
{
    m_difficultyLabel->setText(
        QStringLiteral("当前难度：%1（每帧下落 %2 行 · 白饭 %3%）")
            .arg(QString::fromStdString(core::tokenCatchPresetLabel(m_game.preset())))
            .arg(m_game.presetDef().fallRows)
            .arg(static_cast<int>(m_game.presetDef().riceChance * 100 + 0.5)));
}

void TokenCatchView::updateStatus()
{
    QString state = QStringLiteral("等待开始");
    switch (m_game.status()) {
    case core::TokenCatchStatus::Ready:
        state = QStringLiteral("等待开始");
        break;
    case core::TokenCatchStatus::Playing:
        state = QStringLiteral("进行中");
        break;
    case core::TokenCatchStatus::Ended:
        switch (m_game.endReason()) {
        case core::TokenCatchEnd::TargetReached:
            state = QStringLiteral("达成目标");
            break;
        case core::TokenCatchEnd::RiceCaught:
            state = QStringLiteral("接到白饭");
            break;
        case core::TokenCatchEnd::TimeUp:
            state = QStringLiteral("时间到");
            break;
        case core::TokenCatchEnd::None:
            state = QStringLiteral("已结束");
            break;
        }
        break;
    }

    m_statusLabel->setText(QStringLiteral("得分 %1/%2 · 连击 %3（峰值 %4）· 漏接 %5 · 剩余 %6 秒 · "
                                          "节奏档 %7 · %8")
                               .arg(m_game.tokensCaught())
                               .arg(m_game.targetTokens())
                               .arg(m_game.chain())
                               .arg(m_game.maxChain())
                               .arg(m_game.tokensMissed())
                               .arg((m_game.remainingMs() + 999) / 1000)
                               .arg(m_game.level())
                               .arg(state));
}

void TokenCatchView::finishGame()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_loop->stop();
    m_board->refresh();
    m_restartButton->setText(QStringLiteral("再来一局"));
    updateStatus();

    const core::TokenCatchSummary summary = m_game.summary();
    core::MiniGameResult result =
        core::tokenCatchGameResult(summary, m_game.preset(), m_game.elapsedMs());
    // 难度文案用完整口径（目标 / 时限），与窗口内展示一致
    result.difficultyLabel = core::tokenCatchPresetLabel(m_game.preset());

    // 结束表现：接到白饭 → daily-picnic（本插件的结束约定）；
    // 达成目标 → game-win；超时未达成 → game-lose。
    switch (summary.end) {
    case core::TokenCatchEnd::RiceCaught:
        announce(QString::fromLatin1(core::kTokenCatchRicePose),
                 QString::fromLatin1(core::kTokenCatchSceneRice), 6000);
        break;
    case core::TokenCatchEnd::TargetReached:
        announce(QStringLiteral("game-win"), QString::fromLatin1(core::kTokenCatchSceneWin), 4000);
        break;
    case core::TokenCatchEnd::TimeUp:
        announce(QStringLiteral("game-lose"), QString::fromLatin1(core::kTokenCatchSceneTimeUp),
                 4000);
        break;
    case core::TokenCatchEnd::None:
        break;
    }

    emit gameFinished(result);
}

void TokenCatchView::announce(const QString &pose, const QString &sceneKey, int ttlMs)
{
    if (m_controller != nullptr) {
        m_controller->presentGame(pose, sceneKey, ttlMs);
    }
}

} // namespace whalepet
