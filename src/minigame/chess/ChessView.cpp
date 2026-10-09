#include "minigame/chess/ChessView.h"

#include "minigame/chess/UciEngine.h"
#include "model/Database.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"
#include "viewmodel/PetController.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace whalepet {

namespace {

// 棋符（Unicode）：白方 U+2654–2659，黑方 U+265A–265F。
QString chessGlyph(char piece)
{
    switch (piece) {
    case 'K':
        return QStringLiteral("\u2654");
    case 'Q':
        return QStringLiteral("\u2655");
    case 'R':
        return QStringLiteral("\u2656");
    case 'B':
        return QStringLiteral("\u2657");
    case 'N':
        return QStringLiteral("\u2658");
    case 'P':
        return QStringLiteral("\u2659");
    case 'k':
        return QStringLiteral("\u265A");
    case 'q':
        return QStringLiteral("\u265B");
    case 'r':
        return QStringLiteral("\u265C");
    case 'b':
        return QStringLiteral("\u265D");
    case 'n':
        return QStringLiteral("\u265E");
    case 'p':
        return QStringLiteral("\u265F");
    default:
        return QString();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ChessBoardWidget
// ---------------------------------------------------------------------------

ChessBoardWidget::ChessBoardWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ChessBoard")); // 供全局样式表定位棋盘
    setMouseTracking(true);                      // 拖动 / 悬停都需要 move 事件
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(2);
    rebuild();
}

void ChessBoardWidget::setGame(core::ChessGame *game)
{
    m_game = game;
}

void ChessBoardWidget::setInteractive(bool interactive)
{
    if (m_interactive == interactive) {
        return;
    }
    m_interactive = interactive;
    if (!interactive) {
        // 交给引擎 / 对局已结束：立刻取消选中与拖动，避免残留高亮或浮影
        m_dragState = DragState::Idle;
        m_dragFrom = -1;
        m_pressIndex = -1;
        m_pressOnSelected = false;
        if (m_ghost != nullptr) {
            m_ghost->hide();
        }
        clearSelectionInternal();
    }
    refresh();
}

void ChessBoardWidget::rebuild()
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

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            const int index = rank * 8 + file;
            auto *btn = new QToolButton(this);
            // 格子只负责「显示」：对鼠标透明，全部鼠标事件由棋盘控件统一处理，
            // 避免子控件抢走 press/move/release 导致跨格拖动断续。
            btn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setFixedSize(m_cellSize, m_cellSize);
            btn->setProperty("cellState", QStringLiteral("dark"));
            btn->setProperty("moveHint", QStringLiteral("none"));
            m_grid->addWidget(btn, 7 - rank, file);
            m_cells.append(btn);
        }
    }

    // 尺寸**显式计算**（见 docs/mapinit.md）：不得把 sizeHint() 交给 setFixedSize()。
    const int spacing = m_grid->spacing();
    const int span = 8 * m_cellSize + 7 * spacing;
    setFixedSize(span, span);
    m_grid->activate();
    refresh();
}

void ChessBoardWidget::setLastMove(int from, int to)
{
    m_lastFrom = from;
    m_lastTo = to;
    refresh();
}

void ChessBoardWidget::clearSelection()
{
    clearSelectionInternal();
    refresh();
}

void ChessBoardWidget::clearSelectionInternal()
{
    m_selected = -1;
    m_targets.clear();
}

QRect ChessBoardWidget::cellGeometry(int index) const
{
    QToolButton *btn = m_cells.value(index);
    return (btn == nullptr) ? QRect() : btn->geometry();
}

void ChessBoardWidget::refresh()
{
    for (int i = 0; i < m_cells.size(); ++i) {
        applyCell(i);
    }
}

// ---------------------------------------------------------------------------
// ChessBoardWidget —— 交互（点击选中 / 点击落点 / 拖动落子）
// ---------------------------------------------------------------------------

int ChessBoardWidget::cellAt(const QPoint &localPos) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        QToolButton *btn = m_cells.value(i);
        if (btn != nullptr && btn->geometry().contains(localPos)) {
            return i;
        }
    }
    return -1;
}

int ChessBoardWidget::cellAtGlobal(const QPoint &globalPos) const
{
    return cellAt(mapFromGlobal(globalPos));
}

bool ChessBoardWidget::isMovablePiece(int square) const
{
    if (!m_interactive || m_game == nullptr || square < 0) {
        return false;
    }
    const char piece = m_game->pieceAt(square);
    return piece != ' ' && core::chessIsWhitePiece(piece) == m_game->whiteToMove();
}

QList<int> ChessBoardWidget::targetsOf(int from) const
{
    QList<int> targets;
    if (m_game == nullptr || from < 0) {
        return targets;
    }
    for (const core::ChessMove &move : m_game->legalMovesFrom(from)) {
        targets.append(move.to);
    }
    return targets;
}

bool ChessBoardWidget::isTargetOf(int from, int to) const
{
    return from >= 0 && to >= 0 && targetsOf(from).contains(to);
}

void ChessBoardWidget::select(int square)
{
    m_selected = square;
    m_targets = targetsOf(square);
    refresh();
}

void ChessBoardWidget::beginPress(int square, const QPoint &globalPos)
{
    if (m_ghost != nullptr) {
        m_ghost->hide();
    }
    m_pressGlobal = globalPos;
    m_pressIndex = square;
    m_pressOnSelected = false;
    m_dragFrom = -1;

    if (isMovablePiece(square)) {
        // 己方棋子：选中 + 高亮全部合法落点，并允许继续拖动
        m_pressOnSelected = (square == m_selected);
        if (!m_pressOnSelected) {
            select(square);
        }
        m_dragFrom = square;
        m_dragState = DragState::Pressed;
        return;
    }

    // 非己方棋子：可能是「点击合法落点走子」，也可能是点空白取消选中
    m_dragState = DragState::Pressed;
}

void ChessBoardWidget::updateDrag(const QPoint &globalPos)
{
    if (m_dragState == DragState::Pressed && m_dragFrom >= 0
        && (globalPos - m_pressGlobal).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragState = DragState::Dragging;
        showGhost(m_dragFrom, globalPos);
        return;
    }
    if (m_dragState == DragState::Dragging) {
        moveGhost(globalPos);
    }
}

void ChessBoardWidget::endPress(int dropSquare, const QPoint &globalPos)
{
    Q_UNUSED(globalPos);

    // 先固化并复位状态，再提交信号（信号可能触发模态升变对话框）
    const DragState state = m_dragState;
    const int dragFrom = m_dragFrom;
    const int pressIndex = m_pressIndex;
    const bool pressOnSelected = m_pressOnSelected;

    m_dragState = DragState::Idle;
    m_dragFrom = -1;
    m_pressIndex = -1;
    m_pressOnSelected = false;
    if (m_ghost != nullptr) {
        m_ghost->hide();
    }
    refresh(); // 恢复原格棋子显示（拖动期间被清空）

    if (state == DragState::Dragging) {
        // 拖动结束：落点合法才提交；否则棋子回到原格（不移动、不触发 UCI 通信）
        if (dragFrom >= 0 && dropSquare >= 0 && isTargetOf(dragFrom, dropSquare)) {
            clearSelectionInternal();
            refresh();
            emit moveRequested(dragFrom, dropSquare);
        } else {
            clearSelectionInternal();
            refresh();
        }
        return;
    }

    // 点击（未拖动）
    if (pressIndex >= 0 && dropSquare == pressIndex) {
        if (dragFrom >= 0) {
            if (pressOnSelected) {
                // 再次点击同一棋子 → 取消选中
                clearSelectionInternal();
                refresh();
            }
            return; // 否则保持刚选中的高亮
        }
        // 点在不含己方棋子的格子上
        const int from = m_selected;
        if (from >= 0 && dropSquare >= 0 && isTargetOf(from, dropSquare)) {
            clearSelectionInternal();
            refresh();
            emit moveRequested(from, dropSquare);
        } else {
            clearSelectionInternal();
            refresh();
        }
        return;
    }

    // 未达拖动阈值但落在别的格子：按落点处理
    if (m_selected >= 0 && dropSquare >= 0 && isTargetOf(m_selected, dropSquare)) {
        const int from = m_selected;
        clearSelectionInternal();
        refresh();
        emit moveRequested(from, dropSquare);
    } else if (dragFrom < 0) {
        clearSelectionInternal();
        refresh();
    }
}

void ChessBoardWidget::showGhost(int square, const QPoint &globalPos)
{
    if (m_game == nullptr || m_cells.value(square) == nullptr) {
        return;
    }
    if (m_ghost == nullptr) {
        m_ghost = new QLabel(this);
        m_ghost->setObjectName(QStringLiteral("ChessDragGhost")); // 样式见 project.qss
        m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_ghost->setAlignment(Qt::AlignCenter);
    }
    m_ghost->setText(chessGlyph(m_game->pieceAt(square)));
    m_ghost->setFixedSize(m_cellSize, m_cellSize);
    m_cells.value(square)->setText(QString()); // 原格暂时清空，避免同一棋子出现两次
    moveGhost(globalPos);
    m_ghost->show();
    m_ghost->raise();
}

void ChessBoardWidget::moveGhost(const QPoint &globalPos)
{
    if (m_ghost == nullptr) {
        return;
    }
    const QPoint local = mapFromGlobal(globalPos);
    m_ghost->move(local.x() - m_cellSize / 2, local.y() - m_cellSize / 2);
}

void ChessBoardWidget::hideGhost()
{
    if (m_ghost != nullptr) {
        m_ghost->hide();
    }
}

void ChessBoardWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_interactive) {
        QWidget::mousePressEvent(event);
        return;
    }
    beginPress(cellAt(event->position().toPoint()), event->globalPosition().toPoint());
    QWidget::mousePressEvent(event);
}

void ChessBoardWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_interactive && m_dragState != DragState::Idle) {
        updateDrag(event->globalPosition().toPoint());
    }
    QWidget::mouseMoveEvent(event);
}

void ChessBoardWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_interactive) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    endPress(cellAt(event->position().toPoint()), event->globalPosition().toPoint());
    QWidget::mouseReleaseEvent(event);
}

void ChessBoardWidget::applyCell(int index)
{
    QToolButton *btn = m_cells.value(index);
    if (btn == nullptr || m_game == nullptr) {
        return;
    }
    const int file = index % 8;
    const int rank = index / 8;
    const char piece = m_game->pieceAt(index);

    btn->setText(chessGlyph(piece));

    const QString cellState =
        ((file + rank) % 2 == 1) ? QStringLiteral("light") : QStringLiteral("dark");

    QString hint = QStringLiteral("none");
    if (index == m_selected) {
        hint = QStringLiteral("selected");
    } else if (m_targets.contains(index)) {
        hint = QStringLiteral("target");
    } else if (index == m_lastFrom || index == m_lastTo) {
        hint = QStringLiteral("lastmove");
    } else if ((piece == 'K' && m_game->inCheck(true))
               || (piece == 'k' && m_game->inCheck(false))) {
        hint = QStringLiteral("check");
    }

    QString tip = QString::fromStdString(core::chessSquareName(index));
    if (piece != ' ') {
        tip += QStringLiteral(" · ") + QString::fromLatin1(&piece, 1);
    }
    btn->setToolTip(tip);

    bool changed = false;
    if (btn->property("cellState").toString() != cellState) {
        btn->setProperty("cellState", cellState);
        changed = true;
    }
    if (btn->property("moveHint").toString() != hint) {
        btn->setProperty("moveHint", hint);
        changed = true;
    }
    if (changed) {
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

// ---------------------------------------------------------------------------
// ChessView
// ---------------------------------------------------------------------------

ChessView::ChessView(const MiniGameContext &ctx, QWidget *parent)
    : MiniGameView(parent)
    , m_controller(ctx.controller)
    , m_db(ctx.db)
{
    setWindowTitle(QStringLiteral("鲸鱼娘 · 国际象棋"));

    m_engine = new UciEngine(this);
    connect(m_engine, &UciEngine::ready, this, &ChessView::onEngineReady);
    connect(m_engine, &UciEngine::bestMove, this, &ChessView::onEngineBestMove);
    connect(m_engine, &UciEngine::failed, this, &ChessView::onEngineFailed);
    connect(m_engine, &UciEngine::logLine, this,
            [](const QString &line) { qDebug() << "[chess/engine]" << line; });

    m_board = new ChessBoardWidget(this);
    m_board->setGame(&m_game);
    connect(m_board, &ChessBoardWidget::moveRequested, this, &ChessView::onMoveRequested);

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

ChessView::~ChessView() = default;

QWidget *ChessView::buildConfigBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(QStringLiteral("引擎路径"), bar));
    m_enginePathEdit = new QLineEdit(bar);
    m_enginePathEdit->setPlaceholderText(QStringLiteral("UCI 引擎可执行文件（如 stockfish.exe）"));
    row1->addWidget(m_enginePathEdit, 1);
    m_browseButton = new QPushButton(QStringLiteral("浏览…"), bar);
    m_browseButton->setFocusPolicy(Qt::NoFocus);
    row1->addWidget(m_browseButton);
    outer->addLayout(row1);

    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(QStringLiteral("难度"), bar));
    m_levelBox = new QComboBox(bar);
    for (int i = 0; i < core::kChessLevelCount; ++i) {
        m_levelBox->addItem(QString::fromUtf8(core::kChessLevels[i].label));
    }
    row2->addWidget(m_levelBox);
    row2->addSpacing(12);
    row2->addWidget(new QLabel(QStringLiteral("我执"), bar));
    m_sideBox = new QComboBox(bar);
    m_sideBox->addItem(QStringLiteral("白方（先手）"));
    m_sideBox->addItem(QStringLiteral("黑方（后手）"));
    row2->addWidget(m_sideBox);
    row2->addStretch();
    outer->addLayout(row2);

    m_engineLabel = new QLabel(bar);
    m_engineLabel->setWordWrap(true);
    m_engineLabel->setText(QStringLiteral("尚未启动引擎"));
    outer->addWidget(m_engineLabel);

    connect(m_browseButton, &QPushButton::clicked, this, [this] {
        const QString startDir = QFileInfo(m_enginePathEdit->text().trimmed()).absolutePath();
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 UCI 引擎可执行文件"), startDir,
            QStringLiteral("可执行文件 (*.exe);;所有文件 (*)"));
        if (file.isEmpty()) {
            return;
        }
        m_enginePathEdit->setText(file);
        if (m_loading) {
            return;
        }
        persistSettings();
        ensureEngine();
    });
    connect(m_enginePathEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_loading) {
            return;
        }
        persistSettings();
        ensureEngine();
    });
    connect(m_levelBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_loading) {
            return;
        }
        m_levelIndex = index;
        persistSettings();
        applyEngineLevel();
        updateStatus();
    });
    connect(m_sideBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_loading) {
            return;
        }
        m_humanIsWhite = (index == 0);
        persistSettings();
        startNewGame();
    });

    return bar;
}

QWidget *ChessView::buildStatusBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel(bar);
    m_statusLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel);

    auto *row = new QHBoxLayout;
    m_resignButton = new QPushButton(QStringLiteral("认输"), bar);
    connect(m_resignButton, &QPushButton::clicked, this, &ChessView::resignGame);
    row->addWidget(m_resignButton);
    row->addStretch();

    m_newGameButton = new QPushButton(QStringLiteral("新局"), bar);
    connect(m_newGameButton, &QPushButton::clicked, this, &ChessView::startNewGame);
    row->addWidget(m_newGameButton);
    outer->addLayout(row);
    return bar;
}

void ChessView::reload()
{
    int level = 0;
    bool humanWhite = true;
    QString path;
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        if (repo.load(data)) {
            level = data.chessDifficulty;
            humanWhite = data.chessHumanIsWhite;
            path = data.chessEnginePath;
        }
    }
    if (level < 0 || level >= core::kChessLevelCount) {
        level = 0;
    }
    if (path.isEmpty()) {
        path = defaultChessEnginePath();
    }

    m_loading = true;
    m_levelIndex = level;
    m_humanIsWhite = humanWhite;
    m_levelBox->setCurrentIndex(level);
    m_sideBox->setCurrentIndex(humanWhite ? 0 : 1);
    m_enginePathEdit->setText(path);
    m_loading = false;

    ensureEngine();
    applyEngineLevel();
    startNewGame();
}

void ChessView::setRewardText(const QString &text)
{
    if (m_rewardLabel != nullptr) {
        m_rewardLabel->setText(text);
    }
}

// 陪玩自描述（EX4）：把象棋的私有状态折算为**中立的** GameSnapshot（危险 = 被将军）。
bool ChessView::companionSnapshot(core::GameSnapshot *out) const
{
    if (out == nullptr) {
        return false;
    }
    const bool over = m_finished || m_game.gameOver();
    core::GameSnapshot snap;
    snap.available = true;
    snap.gameId = "chess";
    snap.running = !over;
    snap.finished = over;
    // 胜负归属：status() 表示「该走棋一方被将死」；被将死方是人类则人类负。
    snap.won = m_game.status() == core::ChessStatus::Checkmate
               && (m_game.whiteToMove() != m_humanIsWhite);
    snap.level = 0; // 象棋无阶段概念
    snap.progressDone = m_game.capturedValue(m_humanIsWhite);
    snap.progressTotal = 39; // 对手全部子力点值（P1 N3 B3 R5 Q9 各两套）
    snap.score = m_game.maxCaptureStreak(m_humanIsWhite);
    snap.danger = !over && m_game.inCheck(m_humanIsWhite);
    *out = snap;
    return true;
}

void ChessView::ensureEngine()
{
    const QString path = m_enginePathEdit->text().trimmed();
    if (path.isEmpty()) {
        m_engine->stop();
        setEngineStatus(QStringLiteral(
            "未配置引擎：请把 UCI 引擎（如 stockfish.exe）放入安装目录的 engine 文件夹，"
            "或用「浏览…」手动指定（见 README「国际象棋引擎」）。"));
        announce(QStringLiteral("meme-shock"), QStringLiteral("chess.noengine"), 4000);
        return;
    }
    if (m_engine->isRunning() && m_engine->enginePath() == QFileInfo(path).absoluteFilePath()) {
        return; // 引擎已就绪且路径未变
    }

    QString error;
    if (!m_engine->start(path, &error)) {
        setEngineStatus(QStringLiteral("引擎启动失败：%1").arg(error));
        announce(QStringLiteral("meme-shock"), QStringLiteral("chess.noengine"), 4000);
    } else {
        setEngineStatus(QStringLiteral("正在启动引擎…"));
    }
}

void ChessView::applyEngineLevel()
{
    // 只有握手完成（readyok）后才能下发 setoption。
    if (!m_engine->isReady()) {
        return;
    }
    m_engine->setOption(QStringLiteral("Skill Level"),
                        QString::number(core::chessLevelOfIndex(m_levelIndex).skill));
}

void ChessView::startNewGame()
{
    m_game.reset();
    m_lastMove = core::ChessMove{};
    m_finished = false;
    m_engineThinking = false;
    m_moveCount = 0;
    m_elapsedMs = 0;
    m_timerRunning = false;
    m_timer->stop();

    setRewardText(QString());
    m_resignButton->setEnabled(true);
    m_board->clearSelection();
    m_board->setLastMove(-1, -1);
    updateBoard();
    updateStatus();

    announce(QStringLiteral("game-think"), QStringLiteral("chess.start"), 5000);

    // 玩家执黑时由引擎先行。
    if (m_game.whiteToMove() != m_humanIsWhite) {
        requestEngineMove();
    }
}

void ChessView::resignGame()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_engineThinking = false;
    m_timer->stop();
    m_timerRunning = false;
    m_resignButton->setEnabled(false);
    updateBoard();
    updateStatus();

    core::MiniGameResult result =
        core::chessGameResult(m_game, m_humanIsWhite, m_levelIndex, m_elapsedMs);
    result.won = false;
    result.perfect = false;
    emit gameFinished(result);
    announce(QStringLiteral("game-lose"), QStringLiteral("chess.lose"), 3500);
}

void ChessView::finishGame()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_engineThinking = false;
    m_timer->stop();
    m_timerRunning = false;
    m_resignButton->setEnabled(false);
    updateBoard();
    updateStatus();

    const core::ChessStatus status = m_game.status();
    const core::MiniGameResult result =
        core::chessGameResult(m_game, m_humanIsWhite, m_levelIndex, m_elapsedMs);
    emit gameFinished(result);

    switch (status) {
    case core::ChessStatus::Checkmate:
        if (result.won) {
            announce(QStringLiteral("game-win"), QStringLiteral("chess.win"), 4000);
        } else {
            announce(QStringLiteral("game-lose"), QStringLiteral("chess.lose"), 3500);
        }
        break;
    case core::ChessStatus::Stalemate:
    case core::ChessStatus::Draw:
        announce(QStringLiteral("meme-wakuwaku"), QStringLiteral("chess.draw"), 3000);
        break;
    case core::ChessStatus::Playing:
        // 引擎认输 / 无着可走：视为玩家获胜收尾。
        announce(QStringLiteral("game-win"), QStringLiteral("chess.win"), 4000);
        break;
    }
}

void ChessView::onEngineReady()
{
    applyEngineLevel();
    m_engine->newGame();
    setEngineStatus(QStringLiteral("引擎就绪"));
    if (!m_finished && m_game.whiteToMove() != m_humanIsWhite) {
        requestEngineMove();
    }
}

void ChessView::onEngineFailed(const QString &reason)
{
    m_engineThinking = false;
    setEngineStatus(QStringLiteral("引擎不可用：%1").arg(reason));
    updateStatus();
}

void ChessView::onEngineBestMove(const QString &uci)
{
    m_engineThinking = false;
    if (m_finished) {
        return;
    }
    if (m_game.whiteToMove() == m_humanIsWhite) {
        return; // 不是引擎回合（乱序保护）
    }
    if (uci.isEmpty()) {
        finishGame(); // 引擎认输 / 无处可走
        return;
    }

    bool ok = false;
    const core::ChessMove move = core::chessMoveFromUci(uci.toStdString(), &ok);
    if (!ok || !m_game.isLegalMove(move)) {
        // 引擎着法必须经本层校验：非法则拒绝，避免污染棋局。
        qWarning() << "[ChessView] 引擎返回非法着法:" << uci;
        setEngineStatus(QStringLiteral("引擎返回非法着法：%1").arg(uci));
        updateStatus();
        return;
    }
    applyMove(move, false);
}

void ChessView::onMoveRequested(int from, int to)
{
    // 棋盘已确认 from→to 落在合法落点上；此处再校验一次并处理升变，然后提交。
    if (m_finished || m_game.whiteToMove() != m_humanIsWhite) {
        return;
    }
    std::vector<core::ChessMove> candidates;
    for (const core::ChessMove &move : m_game.legalMovesFrom(from)) {
        if (move.to == to) {
            candidates.push_back(move);
        }
    }
    if (candidates.empty()) {
        return; // 防御：棋盘只应上报合法着法
    }

    core::ChessMove move = candidates.front();
    if (candidates.size() > 1 && candidates.front().promotion != 0) {
        const char promo = askPromotion();
        for (const core::ChessMove &candidate : candidates) {
            if (candidate.promotion == promo) {
                move = candidate;
                break;
            }
        }
    }
    applyMove(move, true);
}

void ChessView::applyMove(const core::ChessMove &move, bool byHuman)
{
    const bool capture = m_game.isCaptureMove(move);
    if (!m_game.makeMove(move)) {
        return;
    }

    m_lastMove = move;
    m_board->clearSelection();
    m_board->setLastMove(move.from, move.to);
    ++m_moveCount;

    if (!m_timerRunning) {
        m_timerRunning = true;
        m_timer->start();
    }

    updateBoard();

    if (capture) {
        announce(byHuman ? QStringLiteral("game-happy") : QStringLiteral("meme-doubt"),
                 QStringLiteral("chess.capture"), 2500);
    }
    if (m_game.gameOver()) {
        finishGame();
        return;
    }
    if (m_game.inCheck(m_game.whiteToMove())) {
        announce(QStringLiteral("meme-shock"), QStringLiteral("chess.check"), 2500);
    }
    if (m_game.whiteToMove() != m_humanIsWhite) {
        requestEngineMove();
    }
    updateStatus();
}

void ChessView::requestEngineMove()
{
    if (m_finished || m_game.gameOver()) {
        return;
    }
    if (m_game.whiteToMove() == m_humanIsWhite) {
        return;
    }
    // 必须等握手完成（readyok）再下发 position / go；未就绪时由 onEngineReady() 触发。
    if (!m_engine->isReady()) {
        if (!m_engine->isRunning()) {
            setEngineStatus(QStringLiteral("引擎不可用，无法应招：请检查引擎路径后点「新局」重试。"));
            updateStatus();
        }
        return;
    }
    m_engine->setPosition(QString::fromStdString(m_game.fen()));
    m_engine->goMoveTime(core::chessLevelOfIndex(m_levelIndex).moveTimeMs);
    m_engineThinking = true;
    updateStatus();
}

void ChessView::updateBoard()
{
    // 状态切换的唯一出口：只有「轮到玩家且对局未结束」才接受棋盘交互，
    // 否则 setInteractive(false) 会清空选中与拖动，避免引擎回合残留高亮。
    m_board->setInteractive(!m_finished && !m_game.gameOver()
                            && m_game.whiteToMove() == m_humanIsWhite);
    m_board->refresh();
}

void ChessView::updateStatus()
{
    const QString side = m_game.whiteToMove() ? QStringLiteral("白方") : QStringLiteral("黑方");
    const QString who = (m_game.whiteToMove() == m_humanIsWhite) ? QStringLiteral("你")
                                                                 : QStringLiteral("引擎");
    QString state;
    if (m_finished) {
        switch (m_game.status()) {
        case core::ChessStatus::Checkmate:
            state = QStringLiteral("将死");
            break;
        case core::ChessStatus::Stalemate:
            state = QStringLiteral("逼和");
            break;
        case core::ChessStatus::Draw:
            state = QStringLiteral("和棋");
            break;
        case core::ChessStatus::Playing:
            state = QStringLiteral("已结束");
            break;
        }
    } else if (m_engineThinking) {
        state = QStringLiteral("引擎思考中");
    } else if (m_game.inCheck(m_game.whiteToMove())) {
        state = QStringLiteral("将军！");
    } else {
        state = (m_game.whiteToMove() == m_humanIsWhite) ? QStringLiteral("轮到你走")
                                                         : QStringLiteral("等待引擎");
    }

    m_statusLabel->setText(QStringLiteral("走子：%1（%2） · 步数 %3 · 用时 %4 秒 · %5")
                               .arg(side, who)
                               .arg(m_moveCount)
                               .arg(m_elapsedMs / 1000)
                               .arg(state));
}

void ChessView::setEngineStatus(const QString &text)
{
    if (m_engineLabel != nullptr) {
        m_engineLabel->setText(text);
    }
}

void ChessView::persistSettings()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    repo.load(data); // 保留其它设置项
    data.chessEnginePath = m_enginePathEdit->text().trimmed();
    data.chessDifficulty = m_levelIndex;
    data.chessHumanIsWhite = m_humanIsWhite;
    if (!repo.save(data)) {
        qWarning() << "[ChessView] 设置持久化失败";
    }
}

void ChessView::announce(const QString &pose, const QString &sceneKey, int ttlMs)
{
    if (m_controller != nullptr) {
        m_controller->presentGame(pose, sceneKey, ttlMs);
    }
}

char ChessView::askPromotion()
{
    const QStringList items{QStringLiteral("后"), QStringLiteral("车"), QStringLiteral("象"),
                            QStringLiteral("马")};
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, QStringLiteral("兵升变"),
                                                 QStringLiteral("选择升变棋子："), items, 0, false, &ok);
    if (!ok) {
        return 'q';
    }
    const int index = items.indexOf(choice);
    switch (index) {
    case 1:
        return 'r';
    case 2:
        return 'b';
    case 3:
        return 'n';
    default:
        return 'q';
    }
}

} // namespace whalepet
