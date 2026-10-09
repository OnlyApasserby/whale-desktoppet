#include "minigame/kitten/KittenView.h"

#include "model/Database.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"
#include "viewmodel/PetController.h"

#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>
#include <vector>

namespace whalepet {

namespace {

// 格子按钮：把左键点击翻译成回调（不用信号，避免额外的 moc 依赖）
class KittenCellButton : public QToolButton {
public:
    KittenCellButton(int index, std::function<void(int)> onClick, QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_index(index)
        , m_onClick(std::move(onClick))
    {
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(false);
        setProperty("cellState", QStringLiteral("floor"));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_onClick) {
                m_onClick(m_index);
            }
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }

private:
    int m_index = 0;
    std::function<void(int)> m_onClick;
};

// 难度 → 地图资源前缀（与 assets/maps/kitten_*.txt 的文件名一致）
const char *kRoomFilePrefix[] = {"easy", "normal", "expert"};

} // namespace

// ---------------------------------------------------------------------------
// KittenMapWidget
// ---------------------------------------------------------------------------

KittenMapWidget::KittenMapWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("KittenMap")); // 供全局样式表定位地图格
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(1);
}

void KittenMapWidget::setWorld(core::RfkWorld *world)
{
    m_world = world;
}

void KittenMapWidget::rebuild()
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

    if (m_world == nullptr || !m_world->loaded()) {
        setFixedSize(0, 0);
        return;
    }

    const core::RfkRoom &room = m_world->currentRoom();
    for (int y = 0; y < room.height; ++y) {
        for (int x = 0; x < room.width; ++x) {
            const int index = y * room.width + x;
            auto *btn = new KittenCellButton(index, [this](int i) { onCellClicked(i); }, this);
            btn->setFixedSize(m_cellSize, m_cellSize);
            m_grid->addWidget(btn, y, x);
            m_cells.append(btn);
        }
    }

    refresh();

    // 尺寸必须**显式计算**，不能依赖 m_grid->sizeHint()：
    // 运行中（窗口已显示）重建时该返回值会退化为 (0,0)，而 setFixedSize 是粘性的
    // —— 写死成 0×0 之后 min/max 永久为 0，地图再也显示不出来（必须重启程序才恢复）。
    // 见 docs/pitfalls/ TRAP-P6-005（实测 Bug：切换难度后地图空白）。
    const int spacing = m_grid->spacing();
    const int w = room.width * m_cellSize + std::max(0, room.width - 1) * spacing;
    const int h = room.height * m_cellSize + std::max(0, room.height - 1) * spacing;
    setFixedSize(w, h);

    m_grid->activate(); // 让子控件几何按新尺寸立即生效
}

void KittenMapWidget::refresh()
{
    // 防御：网格必须与当前场景的格子数一致。若不一致（例如切换场景后漏了 rebuild），
    // 按旧行列错位显示就会出现「隐形墙」；此处直接重建以自愈，而不是留给用户排查。
    if (m_world != nullptr && m_world->loaded()
        && m_cells.size() != m_world->currentRoom().cellCount()) {
        rebuild(); // rebuild() 末尾会再次 refresh()，此时数量已相符，不会递归
        return;
    }
    for (int i = 0; i < m_cells.size(); ++i) {
        applyCell(i);
    }
}

void KittenMapWidget::onCellClicked(int index)
{
    if (m_world == nullptr || !m_world->loaded()) {
        return;
    }
    const core::RfkRoom &room = m_world->currentRoom();
    if (index < 0 || index >= room.cellCount()) {
        return;
    }
    const int px = m_world->playerX();
    const int py = m_world->playerY();
    const int x = index % room.width;
    const int y = index / room.width;
    const int dx = x - px;
    const int dy = y - py;
    // 只有与角色相邻（曼哈顿距离 1）的格子才是有效方向
    if (std::abs(dx) + std::abs(dy) != 1) {
        return;
    }
    emit moveRequested(dx, dy);
}

void KittenMapWidget::applyCell(int index)
{
    QToolButton *btn = m_cells.value(index);
    if (btn == nullptr || m_world == nullptr || !m_world->loaded()) {
        return;
    }
    const core::RfkRoom &room = m_world->currentRoom();
    if (index < 0 || index >= room.cellCount()) {
        return;
    }
    const core::RfkCell &cell = room.cells[static_cast<std::size_t>(index)];

    QString state = QStringLiteral("floor");
    QString tip;
    if (index == m_world->playerIndex()) {
        state = QStringLiteral("player");
        tip = QStringLiteral("鲸鱼娘");
    } else if (cell.kind == core::RfkKind::Blocker) {
        state = QStringLiteral("wall");
        tip = QString::fromStdString(cell.name);
    } else if (cell.consumed) {
        state = QStringLiteral("floor"); // 交互过的物品变成空地
    } else {
        switch (cell.kind) {
        case core::RfkKind::Floor:
            state = QStringLiteral("floor");
            break;
        case core::RfkKind::Player:
            // 地图里的 '@' 只是出生点；角色走开之后这一格就是普通海床，
            // 不能再保留「鲸鱼娘」标记（否则地图上会同时出现两个角色）。
            state = QStringLiteral("floor");
            break;
        case core::RfkKind::Blocker:
            state = QStringLiteral("wall");
            break;
        case core::RfkKind::Toy:
            state = QStringLiteral("toy");
            break;
        case core::RfkKind::Junk:
            state = QStringLiteral("junk");
            break;
        case core::RfkKind::Kitten:
            state = QStringLiteral("kitten");
            break;
        case core::RfkKind::Exit:
            state = QStringLiteral("exit");
            break;
        }
        if (cell.kind != core::RfkKind::Floor && cell.kind != core::RfkKind::Player) {
            tip = QString::fromStdString(cell.name);
        }
    }

    // 方块上不渲染任何文字：物体 / 角色 / 出口一律只用 cellState 的配色与边框表达，
    // 名称改由 tooltip 提供（悬停可见）。这里**无条件**清空文本，
    // 保证移动、捡走物件、切换场景后都不会残留上一次绘制的内容。
    btn->setText(QString());
    btn->setToolTip(tip);
    if (btn->property("cellState").toString() != state) {
        btn->setProperty("cellState", state);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

// ---------------------------------------------------------------------------
// KittenView
// ---------------------------------------------------------------------------

KittenView::KittenView(const MiniGameContext &ctx, QWidget *parent)
    : MiniGameView(parent)
    , m_controller(ctx.controller)
    , m_db(ctx.db)
{
    setWindowTitle(QStringLiteral("鲸鱼娘 · 找小猫"));
    // 方向键需要落到窗口本体（格子与方向按钮都不接收焦点）
    setFocusPolicy(Qt::StrongFocus);

    m_map = new KittenMapWidget(this);
    m_map->setWorld(&m_world);
    connect(m_map, &KittenMapWidget::moveRequested, this, &KittenView::onMoveRequested);

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
    root->addWidget(m_map, 0, Qt::AlignHCenter);
    root->addWidget(buildDirectionPad(), 0, Qt::AlignHCenter);
    root->addWidget(buildStatusBar());
    root->addWidget(m_rewardLabel);

    reload();
}

QWidget *KittenView::buildConfigBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("难度"), bar));
    m_difficultyBox = new QComboBox(bar);
    for (int i = 0; i < core::kRfkDifficultyCount; ++i) {
        const core::RfkDifficultyDef &def = core::kRfkDifficulties[i];
        m_difficultyBox->addItem(QStringLiteral("%1（%2 个场景）")
                                     .arg(QString::fromUtf8(def.name))
                                     .arg(def.rooms));
    }
    row->addWidget(m_difficultyBox);
    row->addStretch();
    outer->addLayout(row);

    m_difficultyLabel = new QLabel(bar);
    outer->addWidget(m_difficultyLabel);

    connect(m_difficultyBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { applyDifficulty(index); });
    // 方向键必须优先交给移动逻辑（见 eventFilter：否则上下键会变成切换难度）
    m_difficultyBox->installEventFilter(this);
    return bar;
}

QWidget *KittenView::buildDirectionPad()
{
    auto *pad = new QWidget(this);
    auto *grid = new QGridLayout(pad);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    auto *up = new QToolButton(pad);
    auto *down = new QToolButton(pad);
    auto *left = new QToolButton(pad);
    auto *right = new QToolButton(pad);
    up->setText(QStringLiteral("↑"));
    down->setText(QStringLiteral("↓"));
    left->setText(QStringLiteral("←"));
    right->setText(QStringLiteral("→"));
    for (QToolButton *button : {up, down, left, right}) {
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(36, 36);
    }
    grid->addWidget(up, 0, 1);
    grid->addWidget(left, 1, 0);
    grid->addWidget(down, 1, 1);
    grid->addWidget(right, 1, 2);

    connect(up, &QToolButton::clicked, this, [this] { onMoveRequested(0, -1); });
    connect(down, &QToolButton::clicked, this, [this] { onMoveRequested(0, 1); });
    connect(left, &QToolButton::clicked, this, [this] { onMoveRequested(-1, 0); });
    connect(right, &QToolButton::clicked, this, [this] { onMoveRequested(1, 0); });
    return pad;
}

QWidget *KittenView::buildStatusBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel(bar);
    outer->addWidget(m_statusLabel);

    auto *row = new QHBoxLayout;
    m_endButton = new QPushButton(QStringLiteral("结束本局"), bar);
    connect(m_endButton, &QPushButton::clicked, this, &KittenView::endCurrentGame);
    row->addWidget(m_endButton);
    row->addStretch();

    m_restartButton = new QPushButton(QStringLiteral("重新开始"), bar);
    connect(m_restartButton, &QPushButton::clicked, this, [this] {
        startNewGame(core::rfkDifficultyOfIndex(m_difficultyBox->currentIndex()));
    });
    row->addWidget(m_restartButton);
    outer->addLayout(row);
    return bar;
}

void KittenView::reload()
{
    int index = 0;
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        if (repo.load(data)) {
            index = data.kittenDifficulty;
        }
    }
    if (index < 0 || index >= core::kRfkDifficultyCount) {
        index = 0;
    }

    m_loading = true;
    m_difficultyBox->setCurrentIndex(index); // m_loading 期间不触发开局
    m_loading = false;

    startNewGame(core::rfkDifficultyOfIndex(index));
}

void KittenView::setRewardText(const QString &text)
{
    if (m_rewardLabel != nullptr) {
        m_rewardLabel->setText(text);
    }
}

// 陪玩自描述（EX4）：把找小猫的私有状态折算为**中立的** GameSnapshot。
bool KittenView::companionSnapshot(core::GameSnapshot *out) const
{
    if (out == nullptr) {
        return false;
    }
    core::GameSnapshot snap;
    snap.available = true;
    snap.gameId = "kitten";
    snap.running = m_world.loaded() && !m_finished;
    snap.finished = m_finished || m_world.finished();
    snap.won = m_world.won();
    snap.level = m_world.roomIndex(); // 场景序号 = 阶段
    snap.progressDone = m_world.visitedCells();
    snap.progressTotal = m_world.floorCells();
    snap.score = m_world.maxChain();
    snap.danger = false;
    *out = snap;
    return true;
}

void KittenView::keyPressEvent(QKeyEvent *event)
{
    if (handleMoveKey(event)) {
        return;
    }
    QDialog::keyPressEvent(event);
}

void KittenView::showEvent(QShowEvent *event)
{
    MiniGameView::showEvent(event);
    // 打开窗口即把焦点放在窗口本体，方向键立刻可用（与 eventFilter 形成双保险）
    setFocus(Qt::ActiveWindowFocusReason);
}

bool KittenView::eventFilter(QObject *watched, QEvent *event)
{
    // 难度下拉框默认会自行消费上下方向键（= 切换选项）。若不拦截，
    // 「向上 / 向下移动」会被解释成「切换难度（= 换地图）」：
    //   * 按上键 → 难度 -1 → 地图被重载（看起来像「退出当前地图」）；
    //   * 已停在最后一档时按下键 → 无变化 → 表现为「下方向键无响应」。
    // 这里把移动键在到达下拉框之前截下来，交给移动逻辑，并吞掉事件。
    if (watched == m_difficultyBox && event->type() == QEvent::KeyPress) {
        if (handleMoveKey(static_cast<QKeyEvent *>(event))) {
            return true;
        }
    }
    return MiniGameView::eventFilter(watched, event);
}

bool KittenView::handleMoveKey(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_W:
        onMoveRequested(0, -1);
        return true;
    case Qt::Key_Down:
    case Qt::Key_S:
        onMoveRequested(0, 1);
        return true;
    case Qt::Key_Left:
    case Qt::Key_A:
        onMoveRequested(-1, 0);
        return true;
    case Qt::Key_Right:
    case Qt::Key_D:
        onMoveRequested(1, 0);
        return true;
    default:
        return false;
    }
}

bool KittenView::loadWorld(core::RfkDifficulty difficulty, QString *error)
{
    // 物体表：外部资源缺失或解析为空 → 降级为内置兜底表（仍可玩，只是物件少）
    core::RfkObjectTable table;
    QFile objectFile(QStringLiteral(":/maps/kitten_objects.txt"));
    if (objectFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        table = core::RfkObjectTable::parse(objectFile.readAll().toStdString());
    }
    if (table.size() == 0) {
        qWarning() << "[KittenView] 物体表缺失或为空，降级为内置兜底物体表";
        table = core::rfkDefaultObjectTable();
    }

    const int index = static_cast<int>(difficulty);
    if (index < 0 || index >= core::kRfkDifficultyCount) {
        if (error != nullptr) {
            *error = QStringLiteral("难度越界");
        }
        return false;
    }

    const QString prefix = QString::fromLatin1(kRoomFilePrefix[index]);
    const int rooms = core::rfkRoomCount(difficulty);
    std::vector<std::string> texts;
    texts.reserve(static_cast<std::size_t>(rooms));
    for (int i = 1; i <= rooms; ++i) {
        const QString path = QStringLiteral(":/maps/kitten_%1_%2.txt").arg(prefix).arg(i);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error != nullptr) {
                *error = QStringLiteral("地图资源缺失：%1").arg(path);
            }
            return false;
        }
        texts.push_back(file.readAll().toStdString());
    }

    std::string parseError;
    if (!m_world.load(table, difficulty, texts, &parseError)) {
        if (error != nullptr) {
            *error = QString::fromStdString(parseError);
        }
        return false;
    }
    return true;
}

void KittenView::startNewGame(core::RfkDifficulty difficulty)
{
    m_timer->stop();
    m_timerRunning = false;
    m_elapsedMs = 0;
    m_finished = false;

    QString error;
    if (!loadWorld(difficulty, &error)) {
        m_finished = true;
        m_map->rebuild();
        m_endButton->setEnabled(false);
        m_restartButton->setText(QStringLiteral("重新开始"));
        setRewardText(QString());
        m_statusLabel->setText(QStringLiteral("地图资源不可用：%1").arg(error));
        adjustSize();
        return;
    }

    m_map->rebuild();
    m_endButton->setEnabled(true);
    m_restartButton->setText(QStringLiteral("重新开始"));
    setRewardText(QString());
    updateDifficultyLabel();
    updateStatus();
    adjustSize();

    announce(QStringLiteral("game-think"), QStringLiteral("kitten.start"), 5000);
}

void KittenView::applyDifficulty(int index)
{
    if (m_loading) {
        return;
    }
    persistDifficulty(index);
    startNewGame(core::rfkDifficultyOfIndex(index));
}

void KittenView::updateDifficultyLabel()
{
    const core::RfkDifficulty difficulty = m_world.difficulty();
    m_difficultyLabel->setText(QStringLiteral("当前难度：%1 · %2 个场景")
                                   .arg(QString::fromUtf8(core::rfkDifficultyName(difficulty)))
                                   .arg(m_world.roomCount()));
}

void KittenView::updateStatus()
{
    QString state = QStringLiteral("探索中");
    if (!m_world.loaded()) {
        state = QStringLiteral("地图不可用");
    } else if (m_world.won()) {
        state = QStringLiteral("找到小猫");
    } else if (m_finished) {
        state = QStringLiteral("已结束");
    }

    m_statusLabel->setText(QStringLiteral("场景 %1/%2 · 步数 %3 · 已探索 %4/%5 · 用时 %6 秒 · %7")
                               .arg(m_world.loaded() ? m_world.roomIndex() + 1 : 0)
                               .arg(m_world.roomCount())
                               .arg(m_world.steps())
                               .arg(m_world.visitedCells())
                               .arg(m_world.floorCells())
                               .arg(m_elapsedMs / 1000)
                               .arg(state));
}

void KittenView::persistDifficulty(int index)
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    repo.load(data); // 保留其它设置项与 json_ext 中的未知键
    data.kittenDifficulty = index;
    if (!repo.save(data)) {
        qWarning() << "[KittenView] 难度配置持久化失败";
    }
}

void KittenView::onMoveRequested(int dx, int dy)
{
    if (m_finished || !m_world.loaded()) {
        return;
    }
    if (!m_timerRunning) {
        m_timerRunning = true;
        m_timer->start();
    }

    const core::RfkMove move = m_world.move(dx, dy);
    if (!move.moved && !move.blocked) {
        return; // 非法方向：无任何反馈
    }

    if (move.sceneChanged) {
        // 场景切换后必须按【新场景】重建网格：各场景宽高不同（深海遗迹 11×8 → 13×8 → 13×9），
        // 只 refresh 会把新场景的格子按旧网格的行列错位显示 —— 视觉上是空地、判定却是墙，
        // 即实测到的「隐形墙」。见 docs/pitfalls/ TRAP-P6-006。
        m_map->rebuild();
        adjustSize(); // 地图尺寸随场景变化，窗口跟随
    } else {
        m_map->refresh();
    }
    updateStatus();

    if (move.blocked) {
        announce(QStringLiteral("meme-shock"), QStringLiteral("kitten.blocked"), 2000);
        return;
    }
    if (move.sceneChanged) {
        announce(QStringLiteral("meme-wakuwaku"), QStringLiteral("kitten.scene"), 2500);
    }
    if (move.interacted) {
        // 不同类别 → 不同立绘 + 物体专属台词（台词场景 key 由物体表决定）
        switch (move.interactKind) {
        case core::RfkKind::Kitten:
            announce(QStringLiteral("meme-kyun"), QString::fromStdString(move.interactScene), 3500);
            break;
        case core::RfkKind::Toy:
            announce(QStringLiteral("curious"), QString::fromStdString(move.interactScene), 2500);
            break;
        case core::RfkKind::Junk:
            announce(QStringLiteral("meme-doubt"), QString::fromStdString(move.interactScene), 2500);
            break;
        default:
            break;
        }
    }
    if (move.won) {
        finishGame();
    }
}

void KittenView::endCurrentGame()
{
    if (m_finished || !m_world.loaded()) {
        return;
    }
    m_world.abandon(); // 未找到小猫 → 按已探索进度结算
    finishGame();
}

void KittenView::finishGame()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_timer->stop();
    m_timerRunning = false;
    m_map->refresh();
    m_endButton->setEnabled(false);
    updateStatus();

    const core::RfkSummary summary = m_world.summary();
    // 折算为通用结算契约；难度文案用实际难度名
    core::MiniGameResult result = core::rfkGameResult(summary, m_world.difficulty(), m_elapsedMs);
    result.difficultyLabel = core::rfkDifficultyName(m_world.difficulty());
    emit gameFinished(result);

    if (summary.won) {
        // 找到小猫：发现小猫的专属对话已在移动时播报，这里再补一次通关庆祝。
        // 用 m_finished / m_world.won() 守卫：期间若已开新局则不再补播。
        QTimer::singleShot(1500, this, [this] {
            if (m_finished && m_world.won()) {
                announce(QStringLiteral("game-win"), QStringLiteral("kitten.win"), 4000);
            }
        });
    } else {
        announce(QStringLiteral("game-lose"), QStringLiteral("kitten.lose"), 3000);
    }
}

void KittenView::announce(const QString &pose, const QString &sceneKey, int ttlMs)
{
    if (m_controller != nullptr) {
        m_controller->presentGame(pose, sceneKey, ttlMs);
    }
}

} // namespace whalepet
