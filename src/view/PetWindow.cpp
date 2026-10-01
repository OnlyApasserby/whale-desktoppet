#include "view/PetWindow.h"

#include "core/ChatRules.h"
#include "core/MiniGameTypes.h"
#include "core/PetTypes.h"
#include "model/Database.h"
#include "model/HotwordRepo.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"
#include "minigame/MiniGamePlugin.h"
#include "view/ContentPanel.h"
#include "view/GlobalHotkey.h"
#include "view/HotwordDialog.h"
#include "view/PoseLibrary.h"
#include "view/PoseView.h"
#include "view/SettingsDialog.h"
#include "view/SpeechBubble.h"
#include "view/StatusPanel.h"
#include "viewmodel/AchievementService.h"
#include "viewmodel/ChatService.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/MiniGameService.h"
#include "viewmodel/PetController.h"
#include "viewmodel/QuestService.h"
#include "viewmodel/SigninService.h"
#include "viewmodel/StomachService.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QGuiApplication>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <cstddef>
#include <utility>
#include <vector>

namespace whalepet {

namespace {
const char *kDefaultPose = "idle-cute";
// P1 遗留的 QSettings 键：**仅**用于一次性导入（见 importLegacyPositionIfNeeded）。
// P3 起窗口位置存入 settings 表（DATA-MODEL §3.3），不再读写 QSettings。
const char *kLegacyPosKey = "window/position";
// 「热词录入」默认全局热键（P6）。改键位只需改这里（注册失败会自动降级为仅菜单入口，
// 见 GlobalHotkey::registerShortcut）。
const char *kHotwordShortcut = "Ctrl+Alt+K";

// 从拖放数据中提取本地文件/文件夹路径：资源管理器拖拽给的是 text/uri-list（file:// URL）。
// 非本地 URL（如 http:// 或浏览器拖出的文本）一律忽略，避免产出非法路径。
QStringList localPathsFromMime(const QMimeData *mime)
{
    QStringList paths;
    if (mime == nullptr || !mime->hasUrls()) {
        return paths;
    }
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty()) {
                paths << path;
            }
        }
    }
    return paths;
}

// 小游戏结算文案（奖励 / 每日上限 / 纪录），展示在游戏窗口底部
QString describeMiniGameReward(const viewmodel::MiniGameReward &reward)
{
    const int limit = viewmodel::MiniGameService::rewardLimit();
    QStringList parts;
    if (reward.newRecord && reward.bestMs > 0) {
        parts << QStringLiteral("刷新最快纪录 %1 秒").arg(reward.bestMs / 1000.0, 0, 'f', 1);
    }
    if (reward.rewarded) {
        QString delta = QStringLiteral("心情 %1")
                            .arg(reward.mood >= 0 ? QStringLiteral("+%1").arg(reward.mood)
                                                  : QString::number(reward.mood));
        if (reward.affinity != 0) {
            delta += QStringLiteral(" / 好感 %1")
                         .arg(reward.affinity >= 0 ? QStringLiteral("+%1").arg(reward.affinity)
                                                   : QString::number(reward.affinity));
        }
        parts << QStringLiteral("本局奖励：%1").arg(delta);
        parts << QStringLiteral("今日计入 %1/%2 局").arg(reward.rewardsUsedToday).arg(limit);
    } else {
        parts << QStringLiteral("今日奖励已达上限（%1 局），本局只计分（今日第 %2 局）")
                     .arg(limit)
                     .arg(reward.rewardsUsedToday);
    }
    return parts.join(QStringLiteral(" · "));
}
} // namespace

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    m_settingsKey = QStringLiteral("WhalePet");

    setupWindowFlags();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 立绘库：首批同步（保证首帧）+ 其余惰性预载
    m_library = new PoseLibrary(this);

    m_pose = new PoseView(this);
    m_pose->setLibrary(m_library);
    m_pose->setPoseImmediate(QString::fromUtf8(kDefaultPose));
    layout->addWidget(m_pose);

    // 台词气泡：独立工具窗口，跟随立绘
    m_bubble = new SpeechBubble(this);
    m_bubble->attachTo(this);

    setupMiniGames(); // 必须在构建菜单之前：菜单项由已注册插件动态生成
    setupContextMenu();
    setupTray();
    setupController();
    setupGrowth();
    setupContent();
    setupStomach();
    setupChat();
    setupHotword();
    setupSettings();
    setupRecallEntry();

    // 固定尺寸：统一 256x256 画布 → 切换姿态不再 resize
    setFixedSize(kPetWindowSize, kPetWindowSize);
    resetToDefaultPosition();
}

PetWindow::~PetWindow()
{
    if (m_stomach != nullptr) {
        m_stomach->stop(); // 子对象随本窗口析构，这里只停定时器
    }
    if (m_growth != nullptr) {
        m_growth->stopTicking();
        m_growth->flush();
    }
    delete m_statusPanel;
    m_statusPanel = nullptr;
    delete m_contentPanel;
    m_contentPanel = nullptr;
    delete m_settingsDialog; // 持有 m_db 指针，必须先于 m_db 释放
    m_settingsDialog = nullptr;
    // 小游戏窗口持有 m_db / m_controller 指针，必须先于二者释放
    for (MiniGameView *view : std::as_const(m_miniGameViews)) {
        delete view;
    }
    m_miniGameViews.clear();
    delete m_recallButton; // 顶层窗口，无父，需手动释放
    m_recallButton = nullptr;
    delete m_db;   // Database 析构会 close() 并释放连接
    m_db = nullptr;
}

void PetWindow::setupWindowFlags()
{
    // 透明 + 无边框 + 置顶 + 工具窗口（不进入任务栏）。
    //
    // 额外加 WindowDoesNotAcceptFocus（Windows 上等价 WS_EX_NOACTIVATE）：
    // 桌宠是纯鼠标交互的看板娘，本就不需要键盘焦点。若允许它成为「活动窗口」，
    // 右键菜单这种 Popup 弹出时立绘仍会是活动置顶窗口，Z 序上压住菜单
    // （视觉遮挡，但不影响菜单点击）——见 docs/traps-P2.md TRAP-P2-010 的后续修正。
    // 鼠标事件、拖拽不受该属性影响。
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowTitle(QStringLiteral("WhalePet"));
    // 拖拽投喂：整窗作为放置目标（见 dragEnterEvent / dropEvent）。
    // 窗口未 setMask，透明角落同样参与拖放命中，无需额外区域裁剪。
    setAcceptDrops(true);
}

void PetWindow::setupController()
{
    m_controller = new PetController(m_pose, m_bubble, this);
}

void PetWindow::setupGrowth()
{
    m_db = new model::Database;
    if (!m_db->open()) {
        // 已由 Database 内部降级到内存库；此处再补一条，说明养成数据不会跨会话保留
        qWarning() << "[PetWindow] 数据库不可用，养成数据仅在内存中存活";
    }

    m_growth = new viewmodel::GrowthService(m_db, this);
    m_growth->load();
    m_controller->setGrowthService(m_growth);

    connect(m_growth, &viewmodel::GrowthService::stateChanged, this,
            &PetWindow::syncStatusPanel);

    // 退出前强制落盘（状态变更已即时落盘，这里是最后一道保险）
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_growth != nullptr) {
            m_growth->flush();
        }
        // 记录本次退出时刻，供下次启动判断「离开是否 >= 2 小时」（见 checkComeback）
        if (m_db != nullptr && m_db->isOpen()) {
            m_db->setMeta(QStringLiteral("app.last_seen_ms"),
                          QString::number(QDateTime::currentMSecsSinceEpoch()));
        }
    });
}

void PetWindow::setupContent()
{
    // 三个内容层 Service 共用同一个 Database（P4 复用 v1 已存在的表，无 schema 迁移）
    m_achievement = new viewmodel::AchievementService(m_db, this);
    m_quest = new viewmodel::QuestService(m_db, this);
    m_signin = new viewmodel::SigninService(m_db, this);

    m_achievement->load();
    m_quest->load();
    m_signin->load();

    // 交互上报：一次语义交互同时喂给成就计数与每日任务进度。
    // 未接入养成服务时 PetController 也会广播，故这里不依赖 m_growth。
    connect(m_controller, &PetController::interactionOccurred, this,
            [this](core::Interaction type, qint64 nowMs) {
                m_achievement->reportInteraction(type, nowMs);
                m_quest->reportInteraction(type, nowMs);
                // 跨午夜运行：交互上报时顺便对齐到「今天」
                m_quest->refreshForToday(nowMs);
                m_signin->syncWeek(nowMs);
            });

    // 任务完成 / 跨天全勤 → 成就统计（quest_* 计数器唯一写入方）
    connect(m_quest, &viewmodel::QuestService::questDone, this,
            [this](const QString &, const QString &) { m_achievement->reportQuestCompleted(); });
    connect(m_quest, &viewmodel::QuestService::dayRolled, this,
            [this](bool previousDayFull) { m_achievement->reportQuestFullDay(previousDayFull); });

    // 奖励回灌养成（任务领取 / 周签到里程碑只发 mood/affinity）
    connect(m_quest, &viewmodel::QuestService::rewardGranted, this,
            [this](int mood, int affinity, const QString &) {
                if (m_growth != nullptr) {
                    m_growth->grantReward(mood, affinity);
                }
            });
    connect(m_signin, &viewmodel::SigninService::rewardGranted, this,
            [this](int mood, int affinity, int) {
                if (m_growth != nullptr) {
                    m_growth->grantReward(mood, affinity);
                }
            });

    // 养成状态变化 → 成就快照判定（等级/羁绊/陪伴/连续签到/周签到/心情/饱食）
    connect(m_growth, &viewmodel::GrowthService::stateChanged, this,
            &PetWindow::syncAchievementProgress);

    // 新解锁成就 → 状态机庆祝表现
    connect(m_achievement, &viewmodel::AchievementService::unlocked, this,
            [this](const QString &, const QString &name) {
                qInfo() << "[PetWindow] 成就解锁:" << name;
                m_controller->handleEvent(core::EventType::AchievementUnlocked);
            });

    // 服务状态变化 → 内容面板实时刷新（面板未打开时 syncContentPanel 为空操作）
    connect(m_quest, &viewmodel::QuestService::slotsChanged, this, &PetWindow::syncContentPanel);
    connect(m_signin, &viewmodel::SigninService::boardChanged, this, &PetWindow::syncContentPanel);
    // 周签到板变化 → 状态面板的「今日签到」按钮同步（三处签到显示同一口径）
    connect(m_signin, &viewmodel::SigninService::boardChanged, this, &PetWindow::syncStatusPanel);
    connect(m_achievement, &viewmodel::AchievementService::unlockedCountChanged, this,
            &PetWindow::syncContentPanel);

    // 小游戏（插件化）结算：每日奖励上限 + 个人最快（照搬参考项目 settleGame）。
    // 结算服务只认通用契约 core::MiniGameResult，对具体玩法无依赖。
    m_miniGameService = new viewmodel::MiniGameService(m_db, this);
    m_miniGameService->load();
    // 旧版个人最快键迁移：映射由插件自己声明，升级不丢历史纪录
    for (int i = 0; i < m_miniGames.count(); ++i) {
        IMiniGamePlugin *plugin = m_miniGames.at(i);
        if (plugin == nullptr) {
            continue;
        }
        const QString gameId = plugin->info().id;
        const QList<QPair<QString, QString>> legacy = plugin->legacyBestRecords();
        for (const QPair<QString, QString> &item : legacy) {
            m_miniGameService->adoptLegacyBest(gameId, item.second, item.first);
        }
    }

    syncAchievementProgress();
    checkComeback();
}

void PetWindow::setupStomach()
{
    // 「胃袋」：拖入的文件/文件夹落到 <安装目录>/stomach，每 5 分钟清空到回收站。
    // 安装目录不可写时仅告警（不崩溃、不改写别处），拖拽投喂的动画/数值仍照常触发。
    m_stomach = new viewmodel::StomachService(this);
    if (!m_stomach->ensureStomachDir()) {
        qWarning() << "[PetWindow] stomach 目录不可用，拖入的文件将无法落盘:"
                   << m_stomach->stomachPath();
    }
    connect(m_stomach, &viewmodel::StomachService::ingested, this, [this](int count) {
        qInfo() << "[PetWindow] 拖拽投喂入胃:" << count << "项 →" << m_stomach->stomachPath();
    });
    connect(m_stomach, &viewmodel::StomachService::trashed, this, [](int count) {
        qInfo() << "[PetWindow] stomach 定时清空，移入回收站:" << count << "项";
    });
}

void PetWindow::setupMiniGames()
{
    // 注册内置插件：新增小游戏只需在 registerBuiltinMiniGames() 追加一行。
    // 菜单入口、设置页展示与结算链路全部按接口驱动，宿主无需任何改动。
    registerBuiltinMiniGames(m_miniGames);
    if (m_miniGames.count() == 0) {
        qWarning() << "[PetWindow] 未注册任何小游戏插件";
    }
}

void PetWindow::setupChat()
{
    // 读取持久化的 keyword_aware（默认关，CHAT.md §4/§7）
    if (m_controller != nullptr && m_controller->chatService() != nullptr && m_db != nullptr) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        if (repo.load(data)) {
            m_controller->chatService()->setKeywordAware(data.keywordAware);
        }
    }
    if (m_keywordAction != nullptr) {
        m_keywordAction->setChecked(keywordAware());
    }

    // 触发源（CHAT.md §4）：本项目无聊天输入，用本地剪贴板文本匹配梗词。
    // 仅当开关开启时才读取剪贴板内容，关闭时不做任何匹配（隐私优先）。
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (!keywordAware() || m_controller == nullptr) {
            return;
        }
        const QString text = QGuiApplication::clipboard()->text();
        if (!text.isEmpty()) {
            m_controller->handleText(text);
        }
    });
}

bool PetWindow::keywordAware() const
{
    return m_controller != nullptr && m_controller->chatService() != nullptr
           && m_controller->chatService()->keywordAware();
}

void PetWindow::setKeywordAware(bool on)
{
    if (m_controller != nullptr && m_controller->chatService() != nullptr) {
        m_controller->chatService()->setKeywordAware(on);
    }
    if (m_keywordAction != nullptr && m_keywordAction->isChecked() != on) {
        m_keywordAction->setChecked(on); // 与菜单勾选态保持同步（不递归：isChecked 已比对）
    }
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data); // 保留其它设置项，只改 keyword_aware
        data.keywordAware = on;
        if (!repo.save(data)) {
            qWarning() << "[PetWindow] keyword_aware 持久化失败";
        }
    }
}

void PetWindow::setupHotword()
{
    // 全局热键（P6）：注册失败只降级（菜单里仍有入口），不影响任何主流程
    m_hotkey = new GlobalHotkey(this);
    QString error;
    if (m_hotkey->registerShortcut(QKeySequence(QString::fromLatin1(kHotwordShortcut)), &error)) {
        qInfo() << "[PetWindow] 全局热键已就绪:" << m_hotkey->shortcutText();
    } else {
        qWarning() << "[PetWindow] 全局热键未生效，降级为仅菜单入口:" << error;
    }
    connect(m_hotkey, &GlobalHotkey::activated, this, &PetWindow::showHotwordDialog);

    reloadHotwords();
}

void PetWindow::showHotwordDialog()
{
    if (m_hotwordDialog == nullptr) {
        m_hotwordDialog = new HotwordDialog(this);

        // 下拉只提供**有立绘**的关键词（kKeywordPoses，21 项）：
        // hug / cute / morning 无立绘、meme.txt 里也没有台词，录了不会有任何反应
        QStringList ids;
        for (std::size_t i = 0; i < core::kKeywordPoseCount; ++i) {
            ids << QString::fromLatin1(core::kKeywordPoses[i].id);
        }
        m_hotwordDialog->setKeywordChoices(ids);

        connect(m_hotwordDialog, &HotwordDialog::triggerRequested, this,
                [this](const QString &text) {
                    if (m_controller == nullptr || m_hotwordDialog == nullptr) {
                        return;
                    }
                    m_controller->handleHotwordInput(text);
                    m_hotwordDialog->showHint(
                        QStringLiteral("已尝试触发「%1」（未命中则不会有反应）").arg(text));
                });

        connect(m_hotwordDialog, &HotwordDialog::saveRequested, this,
                [this](const QString &word, const QString &keywordId) {
                    if (m_hotwordDialog == nullptr) {
                        return;
                    }
                    if (m_db == nullptr || !m_db->isOpen()) {
                        m_hotwordDialog->showHint(QStringLiteral("数据库不可用，无法录入"), true);
                        return;
                    }
                    model::HotwordRepo repo(m_db);
                    if (!repo.upsert(word, keywordId, QDateTime::currentMSecsSinceEpoch())) {
                        m_hotwordDialog->showHint(QStringLiteral("写入失败（见日志）"), true);
                        return;
                    }
                    reloadHotwords();
                    if (m_hotwordDialog != nullptr) {
                        m_hotwordDialog->showHint(
                            QStringLiteral("已录入：%1 → %2")
                                .arg(model::HotwordRepo::normalizeWord(word), keywordId));
                    }
                });

        connect(m_hotwordDialog, &HotwordDialog::removeRequested, this,
                [this](const QString &word) {
                    if (m_hotwordDialog == nullptr) {
                        return;
                    }
                    if (m_db == nullptr || !m_db->isOpen()) {
                        m_hotwordDialog->showHint(QStringLiteral("数据库不可用，无法删除"), true);
                        return;
                    }
                    model::HotwordRepo repo(m_db);
                    if (!repo.remove(word)) {
                        m_hotwordDialog->showHint(QStringLiteral("删除失败（见日志）"), true);
                        return;
                    }
                    reloadHotwords();
                    m_hotwordDialog->showHint(QStringLiteral("已删除：%1").arg(word));
                });
    }

    reloadHotwords(); // 每次打开都从库里刷新（可能被其它窗口/外部改动过）
    m_hotwordDialog->show();
    m_hotwordDialog->raise();
    m_hotwordDialog->activateWindow();
}

void PetWindow::reloadHotwords()
{
    if (m_controller == nullptr || m_controller->chatService() == nullptr) {
        return;
    }

    QVector<model::Hotword> items;
    if (m_db != nullptr && m_db->isOpen()) {
        model::HotwordRepo repo(m_db);
        items = repo.loadAll();
    }

    // 顺序即优先级：loadAll 按 id 升序 = 录入顺序
    std::vector<core::CustomHotword> custom;
    custom.reserve(static_cast<std::size_t>(items.size()));
    for (const model::Hotword &item : items) {
        custom.push_back({ item.word.toStdString(), item.keywordId.toStdString() });
    }
    m_controller->chatService()->setCustomHotwords(custom);

    if (m_hotwordDialog != nullptr) {
        m_hotwordDialog->setHotwords(items);
    }
}

void PetWindow::setupSettings()
{
    if (m_db == nullptr || m_achievement == nullptr || m_quest == nullptr || m_signin == nullptr) {
        qWarning() << "[PetWindow] 内容服务不可用，跳过设置面板初始化";
        return;
    }

    m_settingsDialog =
        new SettingsDialog(m_db, m_achievement, m_quest, m_signin, &m_miniGames, nullptr);

    // 设置变化（已落库）→ 应用到界面
    connect(m_settingsDialog, &SettingsDialog::settingsChanged, this, &PetWindow::applySettings);
    // 面板显隐 → 抑制/恢复主动说话（CHAT.md §5）
    connect(m_settingsDialog, &SettingsDialog::visibleChanged, this, [this](bool visible) {
        if (m_controller != nullptr) {
            m_controller->setSuppressed(visible);
        }
    });

    // 面板内的「日常 / 成就墙 / 成长日记」与独立窗口共用同一批 Service
    connect(m_settingsDialog, &SettingsDialog::signInRequested, this, &PetWindow::handleSignIn);
    connect(m_settingsDialog, &SettingsDialog::questClaimRequested, this, [this](int slotIndex) {
        if (m_quest != nullptr && !m_quest->claim(slotIndex)) {
            qInfo() << "[PetWindow] 任务尚不可领取或已领取, slot =" << slotIndex;
        }
    });

    // 数据与重置
    connect(m_settingsDialog, &SettingsDialog::resetPositionRequested, this, [this] {
        resetToDefaultPosition();
        savePosition();
    });
    connect(m_settingsDialog, &SettingsDialog::resetGrowthRequested, this, [this] {
        if (m_growth != nullptr) {
            m_growth->resetToDefaults();
        }
        syncStatusPanel();
        syncContentPanel();
    });
    connect(m_settingsDialog, &SettingsDialog::openDataDirRequested, this,
            &PetWindow::openDataDirectory);
    // 设置面板内的「开始××」与右键 / 托盘菜单同一入口（按插件 id 统一分发）
    connect(m_settingsDialog, &SettingsDialog::openMiniGameRequested, this,
            &PetWindow::showMiniGame);
}

void PetWindow::setupRecallEntry()
{
    // 左下角唤回入口：只在桌宠隐藏时显示（「找不到看板娘」防护，SETTINGS.md §5）
    m_recallButton = new QPushButton(QStringLiteral("唤回鲸鱼娘"));
    m_recallButton->setObjectName(QStringLiteral("RecallEntry"));
    m_recallButton->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    m_recallButton->setAttribute(Qt::WA_ShowWithoutActivating);
    m_recallButton->setFixedSize(120, 36);
    connect(m_recallButton, &QPushButton::clicked, this, [this] { setPetVisible(true); });
    m_recallButton->hide();
    repositionRecallEntry();
}

void PetWindow::repositionRecallEntry()
{
    if (m_recallButton == nullptr) {
        return;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }
    const QRect area = screen->availableGeometry();
    m_recallButton->move(area.left() + 12, area.bottom() - m_recallButton->height() - 12);
}

void PetWindow::applySettings(const model::SettingsData &data)
{
    // 立绘：尺寸 / 粒子 / 拖拽惯性
    if (m_pose != nullptr) {
        m_pose->setDisplaySize(data.poseSize);
        m_pose->setParticlesEnabled(data.particlesEnabled);
        m_pose->setDragInertiaEnabled(data.dragInertia);
    }
    // 台词气泡
    if (m_bubble != nullptr) {
        m_bubble->setSuppressed(!data.bubbleEnabled);
    }
    // 深夜静默
    if (m_controller != nullptr) {
        m_controller->stateMachine().setNightQuiet(data.nightQuiet);
    }
    // 关键词感知：与右键菜单勾选态同步（内部幂等落库）
    setKeywordAware(data.keywordAware);

    // 立绘尺寸变化 → 主窗口跟随（PoseView 自身已 setFixedSize）
    const int side = data.poseSize + 2 * kPetMargin;
    setFixedSize(side, side);

    // 桌宠显隐（联动唤回入口）
    setPetVisible(data.petEnabled);

    // 小游戏入口显隐（minigame_enabled）：未启用时右键 / 托盘菜单不显示「小游戏…」入口
    for (QAction *entry : std::as_const(m_miniGameEntryActions)) {
        entry->setVisible(data.minigameEnabled);
    }

    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
}

void PetWindow::setPetVisible(bool visible)
{
    m_petEnabled = visible;
    setVisible(visible);

    if (!visible && m_bubble != nullptr) {
        m_bubble->hideLine();
    }
    // 桌宠隐藏时显示左下角唤回入口，可见时收起
    if (m_recallButton != nullptr) {
        if (!visible) {
            repositionRecallEntry();
        }
        m_recallButton->setVisible(!visible);
    }
}

void PetWindow::clampToVisibleArea()
{
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return;
    }
    const QRect area = screen->availableGeometry();
    const int minVisible = 40; // 至少露出这么多像素，保证还能被再次拖回
    QPoint p = pos();
    if (p.x() + width() < area.left() + minVisible) {
        p.setX(area.left() - width() + minVisible);
    }
    if (p.x() > area.right() - minVisible) {
        p.setX(area.right() - minVisible);
    }
    if (p.y() + height() < area.top() + minVisible) {
        p.setY(area.top() - height() + minVisible);
    }
    if (p.y() > area.bottom() - minVisible) {
        p.setY(area.bottom() - minVisible);
    }
    if (p != pos()) {
        move(p);
    }
}

void PetWindow::openDataDirectory()
{
    if (m_db == nullptr || m_db->mode() == model::StorageMode::Memory) {
        qWarning() << "[PetWindow] 内存模式无数据目录可打开";
        return;
    }
    const QString dir = QFileInfo(m_db->location()).absolutePath();
    if (dir.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        qWarning() << "[PetWindow] 打开数据目录失败:" << dir;
    }
}

void PetWindow::showSettingsDialog()
{
    if (m_settingsDialog == nullptr) {
        qWarning() << "[PetWindow] 设置面板不可用（服务未就绪）";
        return;
    }
    m_settingsDialog->reload();
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void PetWindow::showMiniGame(const QString &pluginId)
{
    IMiniGamePlugin *plugin = m_miniGames.find(pluginId);
    if (plugin == nullptr) {
        qWarning() << "[PetWindow] 未知的小游戏插件:" << pluginId;
        return;
    }

    MiniGameView *view = m_miniGameViews.value(pluginId);
    if (view == nullptr) {
        MiniGameContext ctx;
        ctx.controller = m_controller;
        ctx.db = m_db;
        view = plugin->createView(ctx, nullptr);
        if (view == nullptr) {
            qWarning() << "[PetWindow] 小游戏插件未能创建窗口:" << pluginId;
            return;
        }
        m_miniGameViews.insert(pluginId, view);
        // 所有插件共用同一条结算链路（奖励 / 成就 / 文案），宿主不区分具体玩法
        connect(view, &MiniGameView::gameFinished, this, &PetWindow::settleMiniGame);
    }

    view->reload(); // 每次打开都按持久化配置重开一局
    view->show();
    view->raise();
    view->activateWindow();
}

void PetWindow::settleMiniGame(const core::MiniGameResult &result)
{
    // 一局结算 → 养成奖励（每日 3 局上限，所有小游戏共用）+ 小游戏成就 + 结算文案
    viewmodel::MiniGameReward reward;
    if (m_miniGameService != nullptr) {
        reward = m_miniGameService->settle(result);
        // 养成奖励复用 GrowthService::grantReward（夹取 / 升级 / 落盘链路）
        if (m_growth != nullptr) {
            m_growth->grantReward(reward.mood, reward.affinity);
        }
    }
    // 成就计数不受每日奖励上限约束（与参考项目一致：成就独立判定）
    if (m_achievement != nullptr) {
        m_achievement->reportMiniGame(result.won, result.expert, result.perfect, result.maxChain);
    }
    MiniGameView *view = m_miniGameViews.value(QString::fromStdString(result.gameId));
    if (view != nullptr && m_miniGameService != nullptr) {
        view->setRewardText(describeMiniGameReward(reward));
    }
    syncContentPanel(); // 成就墙 / 状态面板实时刷新
}

void PetWindow::syncAchievementProgress()
{
    if (m_achievement == nullptr || m_growth == nullptr) {
        return;
    }
    const model::PetStateData &state = m_growth->state();
    const int weekSigninDays = (m_signin != nullptr) ? m_signin->signedCount() : 0;
    m_achievement->setProgress(state.level, state.bondLevel, state.companionMs, state.streakDays,
                               weekSigninDays, state.mood, state.satiety);
}

void PetWindow::syncContentPanel()
{
    if (m_contentPanel != nullptr) {
        m_contentPanel->refreshAll();
    }
    // 设置面板内嵌的是**另一个** ContentPanel 实例：不同步刷新它，
    // 会导致「状态 / 日常 / 设置」三处签到状态各说各话。
    if (m_settingsDialog != nullptr) {
        m_settingsDialog->refreshContent();
    }
}

void PetWindow::handleSignIn()
{
    if (m_growth == nullptr) {
        return;
    }
    // 一次操作同时驱动两套签到：
    //   GrowthService::signIn()  → 连续天数 streak_days（跨天幂等）
    //   SigninService::markToday() → 点亮本周签到板的今天 + 发里程碑奖励
    // 只有 GrowthService 认为「本次真的签到了」才点亮周板，避免两边口径打架。
    if (m_growth->signIn()) {
        if (m_signin != nullptr && !m_signin->markToday()) {
            qWarning() << "[PetWindow] 养成签到成功但周签到板点亮失败";
        }
        // 「今日签到」每日任务（signin-1，每天固定占 slot 0）以 Signin 交互计量；
        // 签到不走点击/菜单链路，必须显式广播，否则该任务永远停在 0/1。
        if (m_controller != nullptr) {
            m_controller->reportSignIn();
        }
    } else {
        qInfo() << "[PetWindow] 今日已签到";
    }
    syncStatusPanel();
    syncContentPanel();
}

void PetWindow::checkComeback()
{
    if (m_db == nullptr || !m_db->isOpen() || m_achievement == nullptr) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString lastSeen = m_db->meta(QStringLiteral("app.last_seen_ms"));
    m_db->setMeta(QStringLiteral("app.last_seen_ms"), QString::number(now));

    if (lastSeen.isEmpty()) {
        return; // 首次运行：不算「回来」
    }
    const qint64 previous = lastSeen.toLongLong();
    if (previous > 0 && now - previous >= 2 * 60 * 60 * 1000) {
        m_achievement->reportComeback(now);
    }
}

QString PetWindow::storageInfo() const
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return QStringLiteral("存储：不可用（未连接数据库）");
    }
    QString modeText;
    switch (m_db->mode()) {
    case model::StorageMode::InstallDir:
        modeText = QStringLiteral("安装目录");
        break;
    case model::StorageMode::UserDir:
        modeText = QStringLiteral("用户目录（已降级）");
        break;
    case model::StorageMode::Memory:
        modeText = QStringLiteral("内存模式（不落盘）");
        break;
    }
    return QStringLiteral("存储：%1\n%2").arg(modeText, m_db->location());
}

void PetWindow::syncStatusPanel()
{
    if (m_statusPanel == nullptr || m_growth == nullptr) {
        return;
    }
    m_statusPanel->updateFrom(m_growth->state(), m_growth->bondUnlocks(), storageInfo());
    // 「今日签到」按钮与「日常 / 设置」面板共用 SigninService 的口径
    m_statusPanel->setTodaySigned(m_signin != nullptr && m_signin->isTodaySigned());
}

void PetWindow::showStatusPanel()
{
    if (m_statusPanel == nullptr) {
        m_statusPanel = new StatusPanel(nullptr);
        // 状态面板与内容面板的「今日签到」走同一条链路（handleSignIn），
        // 保证 GrowthService / SigninService 两侧口径一致、不重复发奖。
        connect(m_statusPanel, &StatusPanel::signInRequested, this, &PetWindow::handleSignIn);
    }

    syncStatusPanel();
    m_statusPanel->show();
    m_statusPanel->raise();
    m_statusPanel->activateWindow();
}

void PetWindow::showContentPanel()
{
    if (m_contentPanel == nullptr) {
        m_contentPanel = new ContentPanel(m_achievement, m_quest, m_signin, m_db, nullptr);
        connect(m_contentPanel, &ContentPanel::signInRequested, this, &PetWindow::handleSignIn);
        connect(m_contentPanel, &ContentPanel::questClaimRequested, this, [this](int slotIndex) {
            // claim 在 DB 条件更新成功时才返回 true，天然幂等（不会重复发奖）
            if (m_quest == nullptr) {
                return;
            }
            if (!m_quest->claim(slotIndex)) {
                qInfo() << "[PetWindow] 任务尚不可领取或已领取, slot =" << slotIndex;
            }
        });
    }

    m_contentPanel->showStandalone();
}

void PetWindow::configurePopupMenu(QMenu *menu)
{
    // 立绘窗口本身是 WindowStaysOnTopHint 的置顶窗口；菜单默认只是 Qt::Popup，
    // 会被压在立绘下面（目视验收：右键菜单被立绘遮挡）。
    // 因此所有弹出菜单（右键 / 托盘 / 「小游戏…」子菜单）都必须同样置顶；
    // 同时用事件过滤器在显示时再 raise 一次——Windows 上 Popup 的真实窗口
    // 在弹出阶段才创建，仅设置 flag 不足以保证层级。
    menu->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    menu->installEventFilter(this);
}

void PetWindow::setupContextMenu()
{
    m_menu = new QMenu(this);
    configurePopupMenu(m_menu);

    QAction *feed = m_menu->addAction(QStringLiteral("投喂"));
    connect(feed, &QAction::triggered, this, [this] {
        emit feedRequested();
        m_controller->handleMenuAction(core::EventType::Feed);
    });

    QAction *poke = m_menu->addAction(QStringLiteral("戳一下"));
    connect(poke, &QAction::triggered, this, [this] {
        emit pokeRequested();
        m_controller->handleMenuAction(core::EventType::Tease);
    });

    QAction *praise = m_menu->addAction(QStringLiteral("夸夸"));
    connect(praise, &QAction::triggered, this, [this] {
        emit praiseRequested();
        m_controller->handleMenuAction(core::EventType::Praise);
    });

    m_menu->addSeparator();

    QAction *reset = m_menu->addAction(QStringLiteral("回原位"));
    connect(reset, &QAction::triggered, this, [this] {
        resetToDefaultPosition();
        savePosition();
    });

    QAction *status = m_menu->addAction(QStringLiteral("状态"));
    connect(status, &QAction::triggered, this, &PetWindow::showStatusPanel);

    QAction *content = m_menu->addAction(QStringLiteral("日常"));
    connect(content, &QAction::triggered, this, &PetWindow::showContentPanel);

    QAction *settings = m_menu->addAction(QStringLiteral("设置…"));
    connect(settings, &QAction::triggered, this, &PetWindow::showSettingsDialog);

    // 小游戏入口：「小游戏…」子菜单，列表内容由已注册插件动态生成。
    // 交互全部由 QMenu 原生提供：悬停展开 / 离开收起、上下键 + 左右键键盘导航、点击展开。
    m_miniGameMenu = m_menu->addMenu(QStringLiteral("小游戏…"));
    m_miniGameMenu->setObjectName(QStringLiteral("MiniGameMenu"));
    configurePopupMenu(m_miniGameMenu);
    m_miniGameEntryActions.append(m_miniGameMenu->menuAction());
    for (int i = 0; i < m_miniGames.count(); ++i) {
        IMiniGamePlugin *plugin = m_miniGames.at(i);
        if (plugin == nullptr) {
            continue;
        }
        const MiniGameInfo info = plugin->info();
        QAction *action = m_miniGameMenu->addAction(info.menuLabel);
        const QString id = info.id;
        connect(action, &QAction::triggered, this, [this, id] { showMiniGame(id); });
    }

    // P5 关键词感知开关（默认关，CHAT.md §4/§7）：勾选后剪贴板文本命中梗词会切表情 + 说梗台词
    m_keywordAction = m_menu->addAction(QStringLiteral("关键词感知（梗表情）"));
    m_keywordAction->setCheckable(true);
    connect(m_keywordAction, &QAction::toggled, this, &PetWindow::setKeywordAware);

    // P6 热词录入入口（Ctrl+Alt+K 的全局热键与这里是同一条路径）
    m_hotwordAction = m_menu->addAction(
        QStringLiteral("热词录入…\t%1").arg(QString::fromLatin1(kHotwordShortcut)));
    connect(m_hotwordAction, &QAction::triggered, this, &PetWindow::showHotwordDialog);

    m_menu->addSeparator();

    QAction *quit = m_menu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);
}

void PetWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "[PetWindow] 系统托盘不可用，跳过托盘初始化";
        return;
    }

    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("WhalePet"));

    QPixmap icon = m_pose->pixmap();
    if (!icon.isNull()) {
        m_tray->setIcon(QIcon(icon));
    }

    auto *trayMenu = new QMenu(this);
    // 与右键菜单同理：托盘菜单也要置顶，否则同样可能被置顶立绘压住
    configurePopupMenu(trayMenu);
    QAction *toggle = trayMenu->addAction(QStringLiteral("显示 / 隐藏"));
    connect(toggle, &QAction::triggered, this, [this] { setPetVisible(!m_petEnabled); });

    QAction *status = trayMenu->addAction(QStringLiteral("状态"));
    connect(status, &QAction::triggered, this, &PetWindow::showStatusPanel);

    QAction *content = trayMenu->addAction(QStringLiteral("日常"));
    connect(content, &QAction::triggered, this, &PetWindow::showContentPanel);

    QAction *settings = trayMenu->addAction(QStringLiteral("设置…"));
    connect(settings, &QAction::triggered, this, &PetWindow::showSettingsDialog);

    // 与右键菜单同一入口（同一门控）：同样用「小游戏…」子菜单
    m_trayMiniGameMenu = trayMenu->addMenu(QStringLiteral("小游戏…"));
    m_trayMiniGameMenu->setObjectName(QStringLiteral("TrayMiniGameMenu"));
    configurePopupMenu(m_trayMiniGameMenu);
    m_miniGameEntryActions.append(m_trayMiniGameMenu->menuAction());
    for (int i = 0; i < m_miniGames.count(); ++i) {
        IMiniGamePlugin *plugin = m_miniGames.at(i);
        if (plugin == nullptr) {
            continue;
        }
        const MiniGameInfo info = plugin->info();
        QAction *action = m_trayMiniGameMenu->addAction(info.menuLabel);
        const QString id = info.id;
        connect(action, &QAction::triggered, this, [this, id] { showMiniGame(id); });
    }

    QAction *quit = trayMenu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(trayMenu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    setPetVisible(!m_petEnabled);
                }
            });

    m_tray->show();
}

void PetWindow::showPet()
{
    m_library->startPreload();
    importLegacyPositionIfNeeded();
    restorePosition();
    clampToVisibleArea(); // 位置越界防护（SETTINGS.md §5）
    show();
    m_controller->start();

    // 养成结算：每 60s 一次（饱食衰减 + 陪伴时长累计），见 ROADMAP-P3 §3
    if (m_growth != nullptr) {
        m_growth->startTicking();
    }

    // 「胃袋」定时清空：每 5 分钟把 stomach/ 内所有条目移入回收站
    if (m_stomach != nullptr) {
        m_stomach->start();
    }

    // 应用持久化设置；放在 show() 之后，使 pet_enabled == false 时能覆盖显示
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data);
        applySettings(data);
    }
}

QPoint PetWindow::defaultPosition() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QPoint(100, 100);
    }
    const QRect area = screen->availableGeometry();
    const int x = area.right() - width() - 40;
    const int y = area.bottom() - height() - 40;
    return QPoint(qMax(area.left(), x), qMax(area.top(), y));
}

void PetWindow::resetToDefaultPosition()
{
    move(defaultPosition());
    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
}

void PetWindow::importLegacyPositionIfNeeded()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    const bool hadRow = repo.load(data);
    if (hadRow && data.hasPosition) {
        return; // 已是新存储，后续不再读取 QSettings
    }

    QSettings legacy(m_settingsKey, m_settingsKey);
    const QVariant pos = legacy.value(QString::fromUtf8(kLegacyPosKey));
    if (!pos.isValid()) {
        return;
    }

    data.hasPosition = true;
    data.posX = pos.toPoint().x();
    data.posY = pos.toPoint().y();
    if (repo.save(data)) {
        qInfo() << "[PetWindow] 已从 P1 的 QSettings 一次性导入窗口位置:" << pos.toPoint();
    }
    // 刻意不清除旧键：保留一份可回滚的旧值，但程序不再读取
}

void PetWindow::restorePosition()
{
    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    if (repo.load(data) && data.hasPosition) {
        const QPoint p(data.posX, data.posY);
        // 简单校验：落在任一屏幕可用区域内才采用，否则回默认位置
        bool onScreen = false;
        const auto screens = QGuiApplication::screens();
        for (QScreen *screen : screens) {
            if (screen->availableGeometry().contains(p)) {
                onScreen = true;
                break;
            }
        }
        if (onScreen) {
            move(p);
            return;
        }
        qWarning() << "[PetWindow] 保存的位置不在任何屏幕内，回退到默认位置:" << p;
    }

    resetToDefaultPosition();
}

void PetWindow::savePosition()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }

    model::SettingsRepo repo(m_db);
    model::SettingsData data;
    repo.load(data);   // 保留其它设置项；无记录时以默认值起底
    data.hasPosition = true;
    data.posX = pos().x();
    data.posY = pos().y();
    repo.save(data);
}

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_dragging = false;
        m_pressGlobalPos = event->globalPosition().toPoint();
        m_pressViewPos = event->position().toPoint();
        m_dragOffset = m_pressGlobalPos - frameGeometry().topLeft();

        m_dragClock.start();
        m_lastMovePos = m_pressGlobalPos;
        m_lastMoveMs = 0;
        m_velocity = QPointF(0.0, 0.0);

        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && (event->buttons() & Qt::LeftButton)) {
        const QPoint globalPos = event->globalPosition().toPoint();

        if (!m_dragging) {
            const int distance = (globalPos - m_pressGlobalPos).manhattanLength();
            if (distance > kDragThreshold) {
                m_dragging = true;
                m_pose->beginDrag();
                m_controller->handleDragBegin();
            }
        }

        if (m_dragging) {
            move(globalPos - m_dragOffset);
            m_pose->dragMove(globalPos - m_pressGlobalPos);
            m_bubble->reposition();

            // 速度估计（px/ms，指数平滑），松手时用于惯性滑行
            const qint64 now = m_dragClock.elapsed();
            const qint64 dt = now - m_lastMoveMs;
            if (dt > 0) {
                const QPointF v((globalPos - m_lastMovePos).x() / static_cast<qreal>(dt),
                                (globalPos - m_lastMovePos).y() / static_cast<qreal>(dt));
                m_velocity = m_velocity * 0.6 + v * 0.4;
                m_lastMovePos = globalPos;
                m_lastMoveMs = now;
            }
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasDragging = m_dragging;
    m_pressed = false;
    m_dragging = false;

    if (wasDragging) {
        // 停顿超过阈值则视为「已停下」，不做惯性
        QPointF velocity = m_velocity;
        if (m_dragClock.elapsed() - m_lastMoveMs > kVelocityResetMs) {
            velocity = QPointF(0.0, 0.0);
        }
        m_pose->endDrag(velocity * 1000.0); // 转换到 px/s
        m_controller->handleDragEnd();
        clampToVisibleArea(); // 松手后夹回可见区域，避免拖出屏幕找不到
        savePosition();
    } else {
        // 单击：分区命中 → 即时反馈 + 语义事件
        const core::Zone zone = m_pose->zoneAt(m_pressViewPos);
        m_pose->clickFeedback();
        m_controller->handleClick(zone);
    }

    event->accept();
}

bool PetWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 菜单弹出后再抬一次层级：保证菜单完整可见、可点击（不被立绘遮挡）。
    // 同时抬升其原生 QWindow —— 仅 widget 层的 raise() 不总是能改到真实窗口的 Z 序。
    if (event->type() == QEvent::Show) {
        if (auto *menu = qobject_cast<QMenu *>(watched)) {
            menu->raise();
            if (QWindow *handle = menu->windowHandle()) {
                handle->raise();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_menu) {
        m_menu->exec(event->globalPos());
        event->accept();
    }
}

void PetWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // 判定区域：本窗口的整个矩形即「桌宠所在区域」（与鼠标点击/拖窗命中的窗口一致）。
    // 只接受含本地文件/文件夹的拖放，其余（纯文本、http 链接等）不响应。
    if (!localPathsFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void PetWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!localPathsFromMime(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void PetWindow::dropEvent(QDropEvent *event)
{
    const QStringList paths = localPathsFromMime(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();

    // 1) 入胃：移动到 <安装目录>/stomach（详见 StomachService）。
    // 2) 复用「投喂」链路：经 PetController::handleMenuAction(Feed) 触发
    //    eat 姿态 + menu.feed 台词，并施加与投喂**完全相同**的数值与内容计数
    //    （mood/affinity/satiety、首投成就、投喂每日任务等）。
    if (m_stomach != nullptr) {
        m_stomach->ingest(paths);
    }
    if (m_controller != nullptr) {
        m_controller->handleMenuAction(core::EventType::Feed);
    }
}

void PetWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
}

void PetWindow::closeEvent(QCloseEvent *event)
{
    // 托盘常驻：关窗只隐藏，退出走菜单/托盘
    m_controller->stop();
    m_bubble->hideLine();
    event->ignore();
    hide();
}

} // namespace whalepet
