#include "view/PetWindow.h"

#include "contextapi/ContextApiService.h"
#include "contextapi/acp/AcpClient.h"
#include "contextapi/acp/AcpSignalSource.h"
#include "core/ChatRules.h"
#include "core/DaySlotRules.h"
#include "core/GameState.h"
#include "core/MiniGameTypes.h"
#include "core/PetTypes.h"
#include "core/WorkPosePool.h"
#include "core/WorkState.h"
#include "gamestate/GameProfile.h"
#include "minigame/MiniGameCompatAdapter.h"
#include "minigame/MiniGamePlugin.h"
#include "model/Database.h"
#include "model/HotwordRepo.h"
#include "model/SettingsData.h"
#include "model/SettingsRepo.h"
#include "platform/EmptyDesktopObserver.h"
#ifdef Q_OS_WIN
#include "platform/Win32DesktopObserver.h"
#endif
#include "plugin/builtin/BuiltinPluginLoader.h"
#include "plugin/dll/DllPluginLoader.h"
#include "plugin/process/ProcessPluginConfig.h"
#include "plugin/process/ProcessPluginLoader.h"
#include "view/ContentPanel.h"
#include "view/DialoguePanel.h"
#include "view/GlobalHotkey.h"
#include "view/HotwordDialog.h"
#include "view/PoseLibrary.h"
#include "view/PoseView.h"
#include "view/SettingsDialog.h"
#include "view/SpeechBubble.h"
#include "view/StatusPanel.h"
#include "view/ui/StatusPanelUiPlugin.h"
#include "view/ui/UiContributionHost.h"
#include "viewmodel/AchievementService.h"
#include "viewmodel/AcpSignalService.h"
#include "viewmodel/ChatService.h"
#include "viewmodel/DialogueService.h"
#include "viewmodel/EasterEggService.h"
#include "viewmodel/EnvironmentService.h"
#include "viewmodel/GameCompanionService.h"
#include "viewmodel/GrowthService.h"
#include "core/IdleRules.h"

#include <QDate>

#include "viewmodel/MiniGameService.h"
#include "viewmodel/RecycleBinService.h"
#include "viewmodel/PetContextProvider.h"
#include "viewmodel/PetController.h"
#include "viewmodel/QuestService.h"
#include "viewmodel/SigninService.h"
#include "viewmodel/StomachService.h"
#include "viewmodel/WeatherService.h"
#include "viewmodel/WorkStateService.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeData>
#include <QGuiApplication>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QStringList>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <cstddef>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
// WM_COPYGLOBALDATA(0x0049)：UIPI 下放行「资源管理器 → 本窗口」拖放所需的未公开常量，
// 公开头文件里没有定义（见 docs/pitfalls/）。
#  ifndef WM_COPYGLOBALDATA
#    define WM_COPYGLOBALDATA 0x0049
#  endif
// 高完整性级别 RID：老的 _WIN32_WINNT 下 winnt.h 可能不提供
#  ifndef SECURITY_MANDATORY_HIGH_RID
#    define SECURITY_MANDATORY_HIGH_RID 0x00003000L
#  endif
#endif

namespace whalepet {

namespace {
const char *kDefaultPose = "idle-cute";
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

#ifdef Q_OS_WIN
// 当前进程是否以「高完整性级别」（管理员 / 被提权父进程创建）运行。
// 用令牌的完整性级别判定，比 IsUserAnAdmin 准确（后者在 UAC 下语义含糊）。
bool isRunningElevated()
{
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return false;
    }
    DWORD size = 0;
    ::GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    bool elevated = false;
    if (size > 0
        && ::GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size) != FALSE) {
        const auto *label = reinterpret_cast<const TOKEN_MANDATORY_LABEL *>(buffer.data());
        if (label->Label.Sid != nullptr) {
            const DWORD count = *::GetSidSubAuthorityCount(label->Label.Sid);
            if (count > 0) {
                const DWORD rid = *::GetSidSubAuthority(label->Label.Sid, count - 1);
                elevated = (rid >= SECURITY_MANDATORY_HIGH_RID);
            }
        }
    }
    ::CloseHandle(token);
    return elevated;
}

// 放行「低完整性进程 → 本窗口」的三条拖放相关消息。
// 提权（High IL）运行时，资源管理器（Medium IL）的拖放会被 UIPI 拦截，
// 本窗口收不到 dragEnterEvent → 光标显示「禁止投放」。放行后即可正常接收。
// 非提权运行时该调用无副作用；调用失败（如句柄无效）也不影响正常路径。
void allowDragDropFromLowerIntegrity(WId windowHandle)
{
    const auto hwnd = reinterpret_cast<HWND>(windowHandle);
    if (hwnd == nullptr) {
        return;
    }
    const UINT messages[] = {WM_DROPFILES, WM_COPYDATA, WM_COPYGLOBALDATA};
    for (UINT message : messages) {
        ::ChangeWindowMessageFilterEx(hwnd, message, MSGFLT_ALLOW, nullptr);
    }
}
#endif

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

    setupMiniGames();  // 必须在构建菜单之前：菜单项由已注册插件动态生成
    setupDllPlugins(); // P7.3：动态插件（DLL）也必须在构建菜单前装载
    setupContextMenu();
    setupTray();
    setupController();
    setupDatabase();        // P9-A：数据库仍由宿主创建（共享基础设施）
    setupBuiltinServices(); // P9-A：宿主服务经 builtin 层注册化（构造期即启动，供后续装配使用）
    setupGrowth();
    setupContent();
    setupStomach();
    setupRecycleBin(); // 立绘激活 18：回收站随机轮询（默认开，设置生效见 applyRecycleBinSettings）
    setupChat();
    setupHotword();
    setupWorkState();  // P7：感知采样 + 工作状态判定（需 m_controller）
    setupContextApi(); // P7：本地 Context API 装配（需 m_growth / 感知 / 判定）
    setupAcp();        // P7.5：ACP / IDE 显式信号装配（默认关）
    setupGameCompanion(); // EX1.4：游戏陪玩采样链路（默认关；需 m_controller / m_contextProvider）
    setupDialogue();   // P8：预设对话（提问面板 + 门槛；服务由 m_controller 持有）
    setupEasterEgg();  // EX 彩蛋：戳一戳 → 低概率在用户工作区源码注释里藏俏皮话
    setupSettings();
    setupRecallEntry();
    // P9-C：UI 面板型插件（服务已就绪）+ 贡献点挂载（右键 / 托盘菜单已构建）
    setupUiPlugins();
    setupUiContributions();

    // 固定尺寸：统一 256x256 画布 → 切换姿态不再 resize
    setFixedSize(kPetWindowSize, kPetWindowSize);
    resetToDefaultPosition();
}

PetWindow::~PetWindow()
{
    if (m_stomach != nullptr) {
        m_stomach->stop(); // 子对象随本窗口析构，这里只停定时器
    }
    if (m_environment != nullptr) {
        m_environment->stop();
    }
    if (m_growth != nullptr) {
        m_growth->stopTicking();
        m_growth->flush();
    }
    // P7.4 / P7.5：外部进程插件与 ACP 显式信号持有能力注册表 / 信号源引用，先于它们释放
    if (m_processPlugins != nullptr) {
        m_processPlugins->stop(); // 终止子进程（避免孤儿），并把外部能力标记为不可用
        delete m_processPlugins;
        m_processPlugins = nullptr;
    }
    // 先停 ACP 客户端（它持有子进程，且向 AcpSignalService 投递信号），再释放
    // AcpSignalService —— 避免停止期间的信号投递到已销毁对象。
    if (m_acpClient != nullptr) {
        m_acpClient->stop();
        m_acpClient.reset();
    }
    if (m_acpSignal != nullptr) {
        m_acpSignal->stop();
        delete m_acpSignal;
        m_acpSignal = nullptr;
    }
    m_acpSource.reset();
    // P7：Context API 持有能力注册表（值成员 m_plugins）指针，必须先于它释放；
    // contextProvider 又被上下文能力引用，故紧随其后释放。
    delete m_contextApi;
    m_contextApi = nullptr;
    delete m_contextProvider;
    m_contextProvider = nullptr;
    m_plugins.stopAll();
    delete m_contentPanel;
    m_contentPanel = nullptr;
    delete m_settingsDialog; // 持有 m_db 指针，必须先于 m_db 释放
    m_settingsDialog = nullptr;
    delete m_dialoguePanel; // P8：顶层工具窗口（无父），随本窗口析构显式释放
    m_dialoguePanel = nullptr;
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
    // （视觉遮挡，但不影响菜单点击）——见 docs/pitfalls/ TRAP-P2-010 的后续修正。
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

void PetWindow::setupDatabase()
{
    // P9-A：数据库是各服务的共享基础设施，仍由宿主创建（不属服务插件）。
    m_db = new model::Database;
    if (!m_db->open()) {
        // 已由 Database 内部降级到内存库；此处再补一条，说明养成数据不会跨会话保留
        qWarning() << "[PetWindow] 数据库不可用，养成数据仅在内存中存活";
    }
}

void PetWindow::setupBuiltinServices()
{
    // P9-A：把 5 个无 UI 依赖的服务经 builtin 层注册化（docs/ROADMAP-P9-Fin.md §P9-A）。
    // 宿主只提供两类输入：服务对象的 QObject parent（this）与「静息门槛」窄回调；
    // 服务的创建 / 载入 / 注入 controller / 能力注册全部由插件完成。
    m_serviceHooks.dialogueCanAsk = [this]() { return dialogueCanAsk(); };

    plugin::BuiltinPluginLoader builtin;
    builtin.addRegisterFn([this](plugin::PluginRegistry &registry) {
        return registerBuiltinServicePlugins(registry, this, m_serviceHooks, &m_serviceHandles);
    });
    builtin.load(m_plugins);

    // 构造期即启动：后续 setupContent / setupContextApi 等既有装配需要服务已就绪
    // （如 m_growth->stateChanged 的成就快照、ContextApi 的 pet.status）。
    // 服务插件 start() 幂等，showPet 的统一 startAll 会再次调用而不产生副作用。
    plugin::PluginContext serviceCtx;
    serviceCtx.controller = m_controller;
    serviceCtx.db = m_db;
    m_plugins.startAll(serviceCtx);
}

void PetWindow::setupGrowth()
{
    // P9-A：GrowthService 由 builtin 服务插件创建并注入 controller；
    // 宿主只保留 **UI 反应**接线（状态面板刷新）。
    m_growth = m_serviceHandles.growth;
    if (m_growth == nullptr) {
        qWarning() << "[PetWindow] 养成服务插件未就绪，状态面板将显示默认值";
        return;
    }
    connect(m_growth, &viewmodel::GrowthService::stateChanged, this,
            &PetWindow::syncStatusPanel);
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
    m_lastUnlockedCount = m_achievement->unlockedCount(); // 建立基线，避免启动即播「集齐」表现

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
            [this](const QString &, const QString &) {
                m_achievement->reportQuestCompleted();
                // 2026-10-04 立绘激活 3：每日任务完成 → daily-done（维持 5s）
                if (m_controller != nullptr) {
                    m_controller->handleEvent(core::EventType::QuestDone);
                }
            });
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
            [this](int count) {
                syncContentPanel();
                // 2026-10-04 立绘激活 12：集齐全部成就 → meme-smug（仅本次跨过阈值时触发）
                if (m_controller != nullptr && count >= core::kAchievementCount
                    && count > m_lastUnlockedCount) {
                    m_controller->presentGame(QString::fromLatin1(core::kAllAchievedPose),
                                              QStringLiteral("achv.all"),
                                              static_cast<int>(core::kAllAchievedTtlMs));
                }
                m_lastUnlockedCount = count;
            });

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
    // P9-A：StomachService 由 builtin 服务插件创建（含 stomach 目录校验与逻辑型日志接线）。
    // 宿主只保留句柄引用（供 dropEvent 投喂与 showPet 启停）。
    m_stomach = m_serviceHandles.stomach;
    if (m_stomach == nullptr) {
        qWarning() << "[PetWindow] 胃袋服务插件未就绪，拖拽投喂将不可用";
    }
}

void PetWindow::setupRecycleBin()
{
    // 立绘激活 18：以随机间隔（5–10 分钟）轮询系统回收站；检测到非空时展示 sweep 立绘并提醒。
    // P9-A：服务（含「提醒 → PetController」逻辑接线）由 builtin 服务插件完成；
    // 宿主只保留 **UI 反应**——托盘气泡。
    m_recycleBin = m_serviceHandles.recycleBin;
    if (m_recycleBin == nullptr) {
        qWarning() << "[PetWindow] 回收站服务插件未就绪，回收站提醒将不可用";
        return;
    }
    connect(m_recycleBin, &viewmodel::RecycleBinService::recycleBinNotEmpty, this,
            [this](int itemCount, qint64) {
                // 托盘气泡提醒（系统托盘可用且已显示时；不可用则仅保留桌宠气泡）
                if (m_tray != nullptr && m_tray->isVisible()) {
                    m_tray->showMessage(
                        QStringLiteral("回收站提醒"),
                        QStringLiteral("回收站里有 %1 项待清理，要不去收拾一下？🗑️").arg(itemCount),
                        QSystemTrayIcon::Information, 8000);
                }
            });
}

void PetWindow::setRecycleBinReminder(bool on)
{
    if (m_recycleBinAction != nullptr && m_recycleBinAction->isChecked() != on) {
        m_recycleBinAction->setChecked(on); // 同步勾选态（不递归：isChecked 已比对）
    }
    if (m_recycleBin != nullptr) {
        if (on) {
            m_recycleBin->start();
        } else {
            m_recycleBin->stop();
        }
    }
    // 持久化（保留其它设置项）
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data);
        if (data.recycleBinReminderEnabled != on) {
            data.recycleBinReminderEnabled = on;
            if (!repo.save(data)) {
                qWarning() << "[PetWindow] 回收站清理提醒设置持久化失败";
            }
        }
    }
}

void PetWindow::applyRecycleBinSettings(const model::SettingsData &data)
{
    if (m_recycleBinAction != nullptr
        && m_recycleBinAction->isChecked() != data.recycleBinReminderEnabled) {
        m_recycleBinAction->setChecked(data.recycleBinReminderEnabled); // 触发 toggled → 启停
    }
    setRecycleBinReminder(data.recycleBinReminderEnabled); // 幂等兜底（setChecked 未变时仍需生效）
}

void PetWindow::setupMiniGames()
{
    // 注册内置插件：新增小游戏只需在 registerBuiltinMiniGames() 追加一行。
    // 菜单入口、设置页展示与结算链路全部按接口驱动，宿主无需任何改动。
    registerBuiltinMiniGames(m_miniGames);
    if (m_miniGames.count() == 0) {
        qWarning() << "[PetWindow] 未注册任何小游戏插件";
    }

    // P7：把「进程内静态注册」的内置插件并入通用能力总线（三层中的第一层）。
    // 小游戏插件经 MiniGameCompatAdapter 适配为 IPlugin（能力 id = minigame.<pluginId>），
    // 从而出现在 capabilities.list / MCP tools/list 中——**MiniGameRegistry 本身不做任何改动**
    // （见 docs/PLUGIN-ARCHITECTURE.md §7）。
    plugin::BuiltinPluginLoader builtin;
    builtin.addRegisterFn([this](plugin::PluginRegistry &registry) {
        return registerMiniGamePlugins(m_miniGames, registry);
    });
    builtin.load(m_plugins);
}

void PetWindow::setupDllPlugins()
{
    // P7.3：动态插件（DLL）——扫描 <应用目录>/plugins（三层中的第二层，见
    // docs/PLUGIN-ARCHITECTURE.md §4.1）。
    // 硬约束：任何失败（缺目录 / 非插件 / IID 不匹配 / apiVersion 高于宿主 / 实例化失败）
    // 都只记录并跳过，绝不 Fatal、绝不影响主进程与其它插件。
    const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
    m_dllPlugins = std::make_unique<plugin::DllPluginLoader>(dir);
    const plugin::DllLoadReport report = m_dllPlugins->loadAll(m_plugins);
    qInfo() << "[PetWindow] 动态插件目录:" << dir << "成功" << report.loaded.size() << "跳过"
            << report.skipped.size();
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

        // 下拉只提供**有立绘**的关键词（kKeywordPoses，23 项）：
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
        resetToDefaultPosition(); // 回到屏幕正中央
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
    syncDesktopEdge(); // 尺寸变化会改变与边框的距离，需重新判定

    // 桌宠显隐（联动唤回入口）
    setPetVisible(data.petEnabled);

    // 小游戏入口显隐（minigame_enabled）：未启用时右键 / 托盘菜单不显示「小游戏…」入口
    for (QAction *entry : std::as_const(m_miniGameEntryActions)) {
        entry->setVisible(data.minigameEnabled);
    }

    // P7：工作状态感知与本地 Context API（默认关：不采样、不监听）
    applyWorkStateSettings(data);

    // P8：预设对话 + 彩云天气（默认开；天气 key / 城市为空则完全不联网）
    applyDialogueSettings(data);

    // EX1.4：游戏陪玩（默认关：不建适配器、不打开进程、不采样）
    applyGameCompanionSettings(data);

    // EX 彩蛋：代码彩蛋（工作区为空则不动作；需在 ACP 工作区设置之后读取回落值）
    applyCodeEggSettings(data);

    // 立绘激活 18：回收站清理提醒（默认开；纯读取，无副作用）
    applyRecycleBinSettings(data);

    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
}

void PetWindow::setupWorkState()
{
    // P7.1：Win32 上接入**真实采集**（前台窗口 / 进程名 / 空闲 / 键鼠计数 / 会话状态）。
    // 采集默认关闭：只有用户勾选「工作状态感知」后 EnvironmentService::start() 才会
    // 下发 setObserving(true)，届时才安装低层输入钩子并开始采样；
    // 未启用时（含本函数刚装配完）不占用任何系统资源、不产生采样。
    // 非 Windows 回落空观察者（恒「无数据」→ WorkState::Unknown → 行为与 P6 一致）。
#ifdef Q_OS_WIN
    m_observer.reset(new platform::Win32DesktopObserver);
#else
    m_observer.reset(new platform::EmptyDesktopObserver);
#endif

    m_environment = new viewmodel::EnvironmentService(this);
    m_environment->setObserver(m_observer.get());

    m_workState = new viewmodel::WorkStateService(this);
    connect(m_environment, &viewmodel::EnvironmentService::sampleReady, m_workState,
            &viewmodel::WorkStateService::onSample);
    if (m_controller != nullptr) {
        connect(m_workState, &viewmodel::WorkStateService::workStateChanged, m_controller,
                [this](core::WorkState state, double confidence, qint64) {
                    m_controller->handleWorkState(state, confidence);
                });
    }

    qInfo() << "[PetWindow] 工作状态链路已装配（默认关闭，开启后开始采样）";
}

void PetWindow::setupContextApi()
{
    m_contextProvider = new viewmodel::PetContextProvider(this);
    m_contextProvider->setController(m_controller);
    m_contextProvider->setGrowth(m_growth);
    m_contextProvider->setEnvironment(m_environment);
    m_contextProvider->setWorkState(m_workState);

    m_contextApi = new contextapi::ContextApiService(&m_plugins, m_contextProvider, this);
    connect(m_contextApi, &contextapi::ContextApiService::started, this, [](quint16 port) {
        if (port > 0) {
            qInfo() << "[PetWindow] 本地 Context API 已启动，端口 =" << port;
        } else {
            // port == 0：未配置 context_api_token ⇒ HTTP 通道不监听（仅命名管道）
            qInfo() << "[PetWindow] 本地 Context API 已启动（仅命名管道：未配置令牌，"
                       "不监听 HTTP 端口）";
        }
    });
    connect(m_contextApi, &contextapi::ContextApiService::stopped, this,
            [] { qInfo() << "[PetWindow] 本地 Context API 已停止"; });

    qInfo() << "[PetWindow] Context API 已装配（默认关闭：不监听端口、不注册上下文能力）";
}

QString PetWindow::defaultAcpSignalPath() const
{
    if (m_db != nullptr && m_db->mode() != model::StorageMode::Memory) {
        const QString dir = QFileInfo(m_db->location()).absolutePath();
        if (!dir.isEmpty() && dir != QStringLiteral(".")) {
            return dir + QStringLiteral("/acp-signals.jsonl");
        }
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/acp-signals.jsonl");
}

void PetWindow::setupAcp()
{
    // P7.5：ACP / IDE 显式信号（默认关）。未启用时不轮询信号文件——零系统开销。
    m_acpSignal = new viewmodel::AcpSignalService(this);
    if (m_workState != nullptr) {
        // 显式信号作为**覆盖性输入**：holdMs 窗口内直接采用，过期回到推断（见 WorkStateService）
        connect(m_acpSignal, &viewmodel::AcpSignalService::workStateOverride, m_workState,
                [this](core::WorkState state, double confidence, qint64 atMs, qint64 holdMs) {
                    m_workState->applyExternalState(state, confidence, atMs, holdMs);
                });
    }
    if (m_controller != nullptr) {
        // 2026-10-04 立绘激活 16：工作报错信号 → 显示 failure 立绘
        connect(m_acpSignal, &viewmodel::AcpSignalService::errorSignal, m_controller,
                [this] { m_controller->handleWorkError(); });
    }

    m_acpSource = std::make_unique<contextapi::AcpSignalSource>(QStringLiteral("acp"),
                                                               defaultAcpSignalPath());
    m_acpSignal->setSource(m_acpSource.get());

    // P7.6：ACP（Agent Client Protocol）客户端 —— 仅在配置了 dsh 路径时才启动子进程；
    // 未配置时零开销（只保留上面的文件信号源）。
    // 事件链路：Agent session/update → AcpEventMapper → CoreSignal → AcpSignalService::submitSignal
    //           → AcpSignalRules → WorkStateService::applyExternalState（覆盖窗口）
    m_acpClient = std::make_unique<contextapi::AcpClient>();
    connect(m_acpClient.get(), &contextapi::AcpClient::signalMapped, m_acpSignal,
            &viewmodel::AcpSignalService::submitSignal);
    connect(m_acpClient.get(), &contextapi::AcpClient::failed, this,
            [](const QString &message) { qWarning() << "[PetWindow] ACP 客户端:" << message; });
    connect(m_acpClient.get(), &contextapi::AcpClient::processExited, this,
            [](int code, int status) {
                qInfo() << "[PetWindow] ACP Agent 已退出：code =" << code << "status =" << status;
            });

    qInfo() << "[PetWindow] ACP 显式信号链路已装配（默认关闭），信号文件 ="
            << m_acpSource->filePath();
}

QString PetWindow::acpWorkspacePath() const
{
    if (!m_acpWorkspace.isEmpty()) {
        return m_acpWorkspace;
    }
    if (m_db != nullptr && m_db->mode() != model::StorageMode::Memory) {
        const QString dir = QFileInfo(m_db->location()).absolutePath();
        if (!dir.isEmpty() && dir != QStringLiteral(".")) {
            return dir;
        }
    }
    return QDir::currentPath();
}

void PetWindow::applyAcpClientConfig(const model::SettingsData &data)
{
    m_acpDshPath = data.acpDshPath;
    m_acpProfile = data.acpProfile;
    m_acpWorkspace = data.acpWorkspace;
}

void PetWindow::startAcpClient()
{
    if (m_acpClient == nullptr) {
        return;
    }
    if (m_acpDshPath.isEmpty()) {
        qInfo() << "[PetWindow] 未配置 ACP dsh 路径（acp_dsh_path），仅使用文件信号源";
        return;
    }
    if (m_acpClient->running()) {
        return;
    }

    // ACP 的 stdio 服务由客户端以子进程方式拉起：node <dsh>/lib/bin.js --profile acp
    m_acpClient->setProgram(QStringLiteral("node"));
    m_acpClient->setArguments({ m_acpDshPath, QStringLiteral("--profile"),
                                m_acpProfile.isEmpty() ? QStringLiteral("acp") : m_acpProfile });
    m_acpClient->setWorkingDirectory(acpWorkspacePath());

    if (!m_acpClient->start()) {
        qWarning() << "[PetWindow] ACP 客户端启动失败:" << m_acpClient->lastError();
        return;
    }
    attachAcpSession();
}

void PetWindow::attachAcpSession()
{
    if (m_acpClient == nullptr || !m_acpClient->running()) {
        return;
    }
    const QString workspace = acpWorkspacePath();

    // 优先「接管」已有会话（旁观既有的 agent 工作），失败再新建会话。
    if (m_acpClient->supportsSessionResume()) {
        const QStringList ids = m_acpClient->listSessionIds();
        if (!ids.isEmpty() && m_acpClient->resumeSession(ids.first(), workspace)) {
            qInfo() << "[PetWindow] 已接管 ACP 会话:" << ids.first();
            return;
        }
    }
    if (m_acpClient->newSession(workspace)) {
        qInfo() << "[PetWindow] 已新建 ACP 会话:" << m_acpClient->sessionId();
    } else {
        qWarning() << "[PetWindow] ACP 会话建立失败:" << m_acpClient->lastError();
    }
}

void PetWindow::stopAcpClient()
{
    if (m_acpClient != nullptr && m_acpClient->running()) {
        m_acpClient->stop();
    }
}

void PetWindow::setupProcessPlugins()
{
    // P9-B：外部进程插件（MCP Client）。宿主只保留「定位配置 → 加载 → 注册 → 启动」编排；
    // 解析已下沉到 plugin::ProcessPluginConfig（纯逻辑、可脱 UI 单测），
    // 运行状态经 ProcessPluginLoader::sessionStates() 在设置页只读展示。
    // 不存在配置时**不启动任何外部进程**（零开销）；单个插件失败只记录并跳过。
    if (m_processPlugins == nullptr) {
        m_processPlugins = new plugin::ProcessPluginLoader(this);
    }

    QString path;
    if (m_db != nullptr && m_db->mode() != model::StorageMode::Memory) {
        path = QFileInfo(m_db->location()).absolutePath() + QStringLiteral("/plugins.json");
    }

    plugin::ProcessPluginConfig config;
    QString error;
    if (!plugin::ProcessPluginConfig::loadFromFile(path, &config, &error)) {
        qInfo() << "[PetWindow] 外部插件配置不可用，跳过外部进程插件:" << error;
        return;
    }
    for (const QString &warning : config.warnings) {
        qWarning() << "[PetWindow] 外部插件配置告警:" << warning;
    }

    m_processPlugins->clear();
    for (const plugin::ProcessServerSpec &spec : config.servers) {
        m_processPlugins->addServer(spec);
    }

    connect(m_processPlugins, &plugin::ProcessPluginLoader::capabilityAvailabilityChanged, this,
            [](const QString &capabilityId, bool available) {
                qInfo() << "[PetWindow] 外部能力可用性变化:" << capabilityId << available;
            });

    const int started = m_processPlugins->start(m_plugins.capabilities());
    qInfo() << "[PetWindow] 外部进程插件接入:" << started << "/" << m_processPlugins->serverCount()
            << "（配置:" << path << "）";
}

void PetWindow::applyWorkStateSettings(const model::SettingsData &data)
{
    // 勾选态对齐（setChecked 触发 toggled → 走各自开关函数，内部幂等）
    if (m_workAwareAction != nullptr && m_workAwareAction->isChecked() != data.workAwareEnabled) {
        m_workAwareAction->setChecked(data.workAwareEnabled);
    }
    if (m_contextApiAction != nullptr && m_contextApiAction->isChecked() != data.contextApiEnabled) {
        m_contextApiAction->setChecked(data.contextApiEnabled);
    }
    if (m_acpAction != nullptr && m_acpAction->isChecked() != data.acpEnabled) {
        m_acpAction->setChecked(data.acpEnabled);
    }

    // 菜单 setChecked 未触发 toggled 时仍需生效，故显式应用一次（幂等）
    setWorkAware(data.workAwareEnabled);

    if (m_contextApi != nullptr) {
        m_contextApi->setHttpPort(static_cast<quint16>(data.contextApiPort));
        m_contextApi->setToken(data.contextApiToken);
    }
    // ACP 信号文件路径可由设置覆盖（空 = 保持默认路径）
    if (m_acpSource != nullptr && !data.acpSignalPath.isEmpty()
        && m_acpSource->filePath() != data.acpSignalPath) {
        m_acpSource->setFilePath(data.acpSignalPath);
    }
    applyAcpClientConfig(data);
    setContextApiEnabled(data.contextApiEnabled);
    setAcpEnabled(data.acpEnabled);
}

void PetWindow::setWorkAware(bool on)
{
    if (m_workAwareAction != nullptr && m_workAwareAction->isChecked() != on) {
        m_workAwareAction->setChecked(on); // 同步勾选态（不递归：isChecked 已比对）
    }

    if (m_environment != nullptr && m_workState != nullptr) {
        if (on) {
            // 先复位判定状态；start() 会激活观察者（Win32 下安装低层钩子），
            // 随后立即采一份并判定，避免开启后一整个周期没有结果，也保证首份就用真实计数。
            m_workState->reset();
            m_environment->start();
            m_workState->onSample(m_environment->sampleNow());
        } else {
            m_environment->stop();
            m_workState->reset();
            if (m_controller != nullptr) {
                // 关闭感知 → 立即退出工作态分支，行为回到 P6
                m_controller->handleWorkState(core::WorkState::Unknown, 0.0);
            }
        }
    }

    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data); // 保留其它设置项，只改 work_aware_enabled
        if (data.workAwareEnabled != on) {
            data.workAwareEnabled = on;
            if (!repo.save(data)) {
                qWarning() << "[PetWindow] work_aware_enabled 持久化失败";
            }
        }
    }
}

void PetWindow::setContextApiEnabled(bool on)
{
    if (m_contextApiAction != nullptr && m_contextApiAction->isChecked() != on) {
        m_contextApiAction->setChecked(on);
    }

    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data);
        bool dirty = false;
        if (data.contextApiEnabled != on) {
            data.contextApiEnabled = on;
            dirty = true;
        }
        // 【安全】HTTP 通道必须有非空令牌才监听（SECURITY-REVIEW.md #1）：浏览器可向
        // 127.0.0.1:<port> 发跨站请求，回环绑定不是授权。用户未配置令牌时生成一个并
        // 落盘（否则 HTTP 通道将永远不监听，功能形同虚设）。
        if (on && data.contextApiToken.trimmed().isEmpty() && m_contextApi != nullptr) {
            data.contextApiToken = contextapi::ContextApiService::generateToken();
            dirty = true;
        }
        if (dirty) {
            if (repo.save(data)) {
                if (on && m_contextApi != nullptr) {
                    m_contextApi->setToken(data.contextApiToken);
                }
            } else {
                qWarning() << "[PetWindow] context_api 设置持久化失败";
            }
        }
    }

    if (m_contextApi != nullptr) {
        if (on) {
            if (!m_contextApi->start()) {
                qWarning() << "[PetWindow] 本地 Context API 启动失败:"
                           << m_contextApi->errorString();
            }
        } else {
            m_contextApi->stop(); // 同时把上下文能力标记为不可用
        }
    }
}

void PetWindow::setAcpEnabled(bool on)
{
    if (m_acpAction != nullptr && m_acpAction->isChecked() != on) {
        m_acpAction->setChecked(on); // 同步勾选态（不递归：isChecked 已比对）
    }

    if (m_acpSignal != nullptr) {
        if (on) {
            m_acpSignal->start(); // 文件信号源（可选）
            startAcpClient();     // ACP 客户端（仅在配置了 dsh 路径时真正启动子进程）
        } else {
            stopAcpClient();
            m_acpSignal->stop();
        }
    }

    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data); // 保留其它设置项，只改 acp_enabled
        if (data.acpEnabled != on) {
            data.acpEnabled = on;
            if (!repo.save(data)) {
                qWarning() << "[PetWindow] acp_enabled 持久化失败";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// EX1.4：游戏陪玩（默认关。未启用时：不建适配器、不打开进程、不启动采样定时器）
// ---------------------------------------------------------------------------
void PetWindow::setupGameCompanion()
{
    m_gameCompanion = new viewmodel::GameCompanionService(this);
    if (m_controller != nullptr) {
        connect(m_gameCompanion, &viewmodel::GameCompanionService::gameStateChanged, this,
                [this](const core::GameCompanionSample &stable,
                       const core::GameMilestoneSet &milestones, const core::GameSample &sample,
                       qint64) {
                    // 特殊场景取本轮读数：静默陪伴判定与里程碑播报都在状态机内完成
                    m_controller->handleGameState(stable, milestones, sample.specialScene);
                });
    }
    // 适配器失效（目标游戏已关闭等）→ 自动停用并把菜单勾选态同步回「关」，避免「勾着却没在跑」
    connect(m_gameCompanion, &viewmodel::GameCompanionService::companionStopped, this, [this] {
        qInfo() << "[PetWindow] 游戏陪玩已自动停用（适配器失效或读取失败）";
        if (m_gameCompanionAction != nullptr && m_gameCompanionAction->isChecked()) {
            m_gameCompanionAction->setChecked(false);
        }
    });
    // 供 Context API 投影 game.* 分组（未启用时 game.available == false，不伪造数据）
    if (m_contextProvider != nullptr) {
        m_contextProvider->setGameCompanion(m_gameCompanion);
    }

    qInfo() << "[PetWindow] 游戏陪玩链路已装配（默认关闭：不打开任何游戏进程、不采样）";
}

viewmodel::DialogueService *PetWindow::dialogueService() const
{
    return (m_controller != nullptr) ? m_controller->dialogueService() : nullptr;
}

viewmodel::WeatherService *PetWindow::weatherService() const
{
    return (m_controller != nullptr) ? m_controller->weatherService() : nullptr;
}

void PetWindow::setupDialogue()
{
    if (m_controller == nullptr) {
        return;
    }

    // 提问面板：独立工具窗口（不进任务栏、不抢焦点），紧贴桌宠显示
    m_dialoguePanel = new DialoguePanel(nullptr);
    m_dialoguePanel->attachTo(this);

    viewmodel::DialogueService *dialogue = m_controller->dialogueService();
    if (dialogue == nullptr) {
        qWarning() << "[PetWindow] 问答服务不可用，跳过装配";
        return;
    }

    // P9-A：敏感题配额（Database）/ 好感度来源（养成服务）/ 静息门槛（宿主窄回调）
    // 已由 builtin 的 DialogueServicePlugin 配置完成，宿主不再承担服务的装配。

    // 选项池就绪 → 面板显示「主人的问题」（五选一；不可用项禁用并给出原因）。
    // 面板弹出**不换立绘**：立绘在用户真正选择问题后才切到该问题的独立立绘池。
    connect(dialogue, &viewmodel::DialogueService::optionsOffered, this,
            [this](const QStringList &questions, const QList<bool> &enabled,
                   const QStringList &hints) {
                if (m_dialoguePanel != nullptr) {
                    m_dialoguePanel->showOptions(questions, enabled, hints);
                }
            });
    connect(m_dialoguePanel, &DialoguePanel::chosen, this, [this](int index) {
        // 用户选定问题 → DialogueService 随机取一个预设回答 →
        // answered → PetController 切该问题的独立立绘 + 气泡输出文字
        if (viewmodel::DialogueService *service = dialogueService(); service != nullptr) {
            service->choose(index);
        }
    });
    connect(m_dialoguePanel, &DialoguePanel::dismissed, this, [this]() {
        if (viewmodel::DialogueService *service = dialogueService(); service != nullptr) {
            service->cancel(); // 这次不问：不消耗任何配额
        }
    });

    qInfo() << "[PetWindow] 问答链路已装配（用户提问 / 五选一；天气与敏感题按条件禁用）";
}

bool PetWindow::dialogueCanAsk() const
{
    if (m_controller == nullptr) {
        return false;
    }
    if (!m_petEnabled || !isVisible()) {
        return false; // 桌宠隐藏：不打扰
    }
    if (m_dialoguePanel != nullptr && m_dialoguePanel->optionsVisible()) {
        return false; // 面板已打开（有一批问题在等选择）
    }
    if (m_bubble != nullptr && m_bubble->bubbleVisible()) {
        return false; // 气泡占用：不插话
    }
    const core::WorkState work = m_controller->workState();
    if (core::workStateIsBusy(work) || core::workStateIsCoding(work)) {
        return false; // 工作（含编程 / 调试 / 会议）时不打扰
    }
    if (m_controller->gameCompanionSilent()) {
        return false; // 游戏静默陪伴（CG / 影片 / 对话演出）
    }
    const int hour = QTime::currentTime().hour();
    if (core::daySlotOf(hour) == core::DaySlot::LateNight) {
        return false; // 深夜（23:00–06:59）不主动提问
    }
    return true;
}

void PetWindow::askDialogueNow()
{
    viewmodel::DialogueService *service = dialogueService();
    if (service == nullptr) {
        return;
    }
    if (!m_petEnabled) {
        setPetVisible(true); // 桌宠隐藏时先唤回，用户主动要求提问
    }
    // force=true：用户主动要求，跳过「静息」门槛（工作 / 深夜也可提问）
    service->offerOptions(true);
}

void PetWindow::applyDialogueSettings(const model::SettingsData &data)
{
    if (m_controller == nullptr) {
        return;
    }
    if (viewmodel::WeatherService *weather = m_controller->weatherService()) {
        const bool changed = (weather->key() != data.weatherKey)
                             || (weather->location() != data.weatherLocation);
        weather->setConfig(data.weatherKey, data.weatherLocation);
        if (changed && weather->configured()) {
            weather->refresh(); // 配置变化后立刻取一次（未配置则完全不联网）
        }
    }
    if (m_dialogueAction != nullptr && m_dialogueAction->isChecked() != data.dialogueEnabled) {
        m_dialogueAction->setChecked(data.dialogueEnabled); // 触发 toggled → setDialogueEnabled
    }
    setDialogueEnabled(data.dialogueEnabled); // 幂等兜底（setChecked 未变时仍需生效）
}

void PetWindow::setupEasterEgg()
{
    // P9-A：EasterEggService 由 builtin 服务插件创建，并已接好
    // 「PetController::interactionOccurred → Poke → poke()」逻辑；
    // 宿主只保留句柄引用（开关与目标工作区由 applyCodeEggSettings 注入）。
    m_easterEgg = m_serviceHandles.easterEgg;
    if (m_easterEgg == nullptr) {
        qWarning() << "[PetWindow] 代码彩蛋服务插件未就绪，彩蛋注入将不可用";
    }
}

void PetWindow::applyCodeEggSettings(const model::SettingsData &data)
{
    if (m_easterEgg == nullptr) {
        return;
    }
    // 工作区优先级：显式配置 > ACP 会话工作目录（两者都要求是「已存在的目录」）。
    // 都为空 → 服务内部视作「不动作」，绝不猜测安装目录 / 数据目录。
    QString workspace = data.codeEggWorkspace;
    if (workspace.isEmpty()) {
        workspace = m_acpWorkspace;
    }
    m_easterEgg->setWorkspace(workspace);
    m_easterEgg->setEnabled(data.codeEggEnabled);
}

void PetWindow::setDialogueEnabled(bool on)
{
    viewmodel::DialogueService *service = dialogueService();
    if (service == nullptr) {
        return;
    }
    if (m_dialogueAction != nullptr && m_dialogueAction->isChecked() != on) {
        m_dialogueAction->setChecked(on); // 同步勾选态（不递归：已比对）
    }

    if (on) {
        // 天气：仅在「key + 城市都填写」时联网刷新；否则保持完全离线
        if (viewmodel::WeatherService *weather = weatherService();
            weather != nullptr && weather->configured()) {
            weather->refresh();
        }
        service->start();
        return;
    }

    service->stop();
    if (m_dialoguePanel != nullptr && m_dialoguePanel->optionsVisible()) {
        m_dialoguePanel->closePanel();
    }
}

QString PetWindow::defaultGameProfilePath() const
{
    if (m_db != nullptr && m_db->mode() != model::StorageMode::Memory) {
        const QString dir = QFileInfo(m_db->location()).absolutePath();
        if (!dir.isEmpty() && dir != QStringLiteral(".")) {
            return dir + QStringLiteral("/game-profile.json");
        }
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/game-profile.json");
}

void PetWindow::applyGameCompanionSettings(const model::SettingsData &data)
{
    // 勾选态对齐（setChecked 触发 toggled → 走 setGameCompanion，内部幂等）
    if (m_gameCompanionAction != nullptr
        && m_gameCompanionAction->isChecked() != data.gameCompanionEnabled) {
        m_gameCompanionAction->setChecked(data.gameCompanionEnabled);
    }
    // 菜单 setChecked 未触发 toggled 时仍需生效，故显式应用一次（幂等）
    setGameCompanion(data.gameCompanionEnabled);
}

void PetWindow::setGameCompanion(bool on)
{
    if (m_gameCompanionAction != nullptr && m_gameCompanionAction->isChecked() != on) {
        m_gameCompanionAction->setChecked(on); // 同步勾选态（不递归：isChecked 已比对）
    }

    // 档案路径：优先取库中设置，空则回落数据目录下的 game-profile.json
    QString profilePath;
    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data);
        profilePath = data.gameProfilePath;
    }
    if (profilePath.isEmpty()) {
        profilePath = defaultGameProfilePath();
    }

    // 退出游戏分支的统一出口：上报 Unknown（不伪造数据）→ 状态机清空陪玩态，行为回到 EX1 前。
    const auto leaveGameBranch = [this] {
        if (m_controller != nullptr) {
            m_controller->handleGameState(core::GameCompanionSample(), core::GameMilestoneSet(), 0);
        }
    };

    if (m_gameCompanion != nullptr) {
        if (on) {
            gamestate::GameProfile profile;
            QString error;
            if (!gamestate::ProfileLoader::loadFromFile(profilePath, &profile, &error)) {
                qWarning() << "[PetWindow] 游戏档案加载失败:" << profilePath << error;
                if (m_gameCompanionAction != nullptr && m_gameCompanionAction->isChecked()) {
                    m_gameCompanionAction->setChecked(false); // 启用失败 → 勾选态回滚
                }
                leaveGameBranch();
                return;
            }
            if (!m_gameCompanion->start(profile, &error)) {
                qWarning() << "[PetWindow] 游戏陪玩启动失败:" << error;
                if (m_gameCompanionAction != nullptr && m_gameCompanionAction->isChecked()) {
                    m_gameCompanionAction->setChecked(false);
                }
                leaveGameBranch();
                return;
            }
            qInfo() << "[PetWindow] 游戏陪玩已启动，引擎 ="
                    << QString::fromStdString(profile.engine) << "档案 =" << profilePath;
        } else {
            m_gameCompanion->stop();
            leaveGameBranch();
        }
    }

    if (m_db != nullptr && m_db->isOpen()) {
        model::SettingsRepo repo(m_db);
        model::SettingsData data;
        repo.load(data); // 保留其它设置项，只改 game_companion_enabled
        if (data.gameCompanionEnabled != on) {
            data.gameCompanionEnabled = on;
            if (!repo.save(data)) {
                qWarning() << "[PetWindow] game_companion_enabled 持久化失败";
            }
        }
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

// ---------------------------------------------------------------------------
// 桌面四边框贴边（docs/PRESENTATION.md §3.1）
// ---------------------------------------------------------------------------

core::DesktopEdge PetWindow::currentDesktopEdge() const
{
    // 以窗口中心所在屏幕的**可用区域**（已扣除任务栏）为判定基准
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return core::DesktopEdge::None;
    }
    const QRect area = screen->availableGeometry();
    const QPoint p = pos();
    return core::detectDesktopEdge(p.x(), p.y(), width(), height(),
                                   area.x(), area.y(), area.width(), area.height(),
                                   kEdgeAttachPx);
}

core::DesktopEdge PetWindow::desktopEdge() const
{
    return (m_pose != nullptr) ? m_pose->edgeAttachment() : core::DesktopEdge::None;
}

void PetWindow::syncDesktopEdge()
{
    if (m_pose == nullptr) {
        return;
    }
    // 幂等：方向未变时 PoseView 内部直接返回，不会重建位图
    m_pose->setEdgeAttachment(currentDesktopEdge());
}

void PetWindow::snapToDesktopEdge()
{
    const core::DesktopEdge edge = currentDesktopEdge();
    if (edge == core::DesktopEdge::None) {
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        return;
    }

    // 吸附为**完全贴合**：立绘的可见内容因此正好落在桌面边框上（贴边表现的前提）
    const QRect area = screen->availableGeometry();
    QPoint p = pos();
    switch (edge) {
    case core::DesktopEdge::Top:
        p.setY(area.y());
        break;
    case core::DesktopEdge::Bottom:
        p.setY(area.y() + area.height() - height());
        break;
    case core::DesktopEdge::Left:
        p.setX(area.x());
        break;
    case core::DesktopEdge::Right:
        p.setX(area.x() + area.width() - width());
        break;
    case core::DesktopEdge::None:
        return;
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
    // P9-B：设置页「外部插件」只读列表——每次打开刷新当前会话状态
    m_settingsDialog->setProcessPluginStatuses(
        (m_processPlugins != nullptr) ? m_processPlugins->sessionStates()
                                      : QList<plugin::ProcessPluginStatus>());
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
    // 2026-10-04 立绘激活 15：当日第 3 局游戏完成 → celebrate（维持 10s），每天只播一次
    if (m_miniGameService != nullptr && m_controller != nullptr
        && reward.rewardsUsedToday >= m_miniGameService->rewardLimit()) {
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        if (m_celebrateDayKey != today) {
            m_celebrateDayKey = today;
            m_controller->presentGame(QString::fromLatin1(core::kCelebratePose),
                                      QStringLiteral("game.alldone"),
                                      static_cast<int>(core::kCelebrateTtlMs));
        }
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
    // P9-C：状态面板由 UI 插件拥有（延迟创建）；仅在面板已存在时刷新
    fillStatusPanel(m_statusUiPlugin != nullptr ? m_statusUiPlugin->panel() : nullptr);
}

void PetWindow::fillStatusPanel(StatusPanel *panel)
{
    if (panel == nullptr || m_growth == nullptr) {
        return;
    }
    panel->updateFrom(m_growth->state(), m_growth->bondUnlocks(), storageInfo());
    // 「今日签到」按钮与「日常 / 设置」面板共用 SigninService 的口径
    panel->setTodaySigned(m_signin != nullptr && m_signin->isTodaySigned());
}

void PetWindow::showStatusPanel()
{
    // P9-C：状态面板由 builtin UI 插件创建并拥有；此处仅作为「程序化展示」入口
    // （右键 / 托盘入口已由 UI 贡献点驱动）。签到链路在插件内接回 handleSignIn。
    StatusPanel *panel = (m_statusUiPlugin != nullptr) ? m_statusUiPlugin->panel() : nullptr;
    if (panel == nullptr) {
        return;
    }
    fillStatusPanel(panel);
    if (m_uiHost != nullptr) {
        m_uiHost->presentPanel(panel);
    }
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

void PetWindow::setupUiPlugins()
{
    // P9-C：注册 UI 面板型插件（builtin 层）。在服务装配之后调用，
    // 因此注入的窄回调可安全引用 m_growth / m_signin（运行期才被调用）。
    m_uiHost = std::make_unique<ui::UiContributionHost>(this);

    plugin::BuiltinPluginLoader loader;
    loader.addRegisterFn([this](plugin::PluginRegistry &registry) {
        ui::StatusPanelUiPlugin::Hooks hooks;
        hooks.refresh = [this](StatusPanel *panel) { fillStatusPanel(panel); };
        hooks.signIn = [this] { handleSignIn(); };
        auto plugin = std::make_unique<ui::StatusPanelUiPlugin>(std::move(hooks));
        m_statusUiPlugin = plugin.get(); // 非拥有：所有权随后移交 registry
        return registry.add(std::move(plugin)) ? 1 : 0;
    });
    const int registered = loader.load(m_plugins);
    qInfo() << "[PetWindow] UI 面板型插件已注册:" << registered;
}

void PetWindow::setupUiContributions()
{
    // P9-C：把插件声明的 UI 贡献点挂载到已有菜单（右键 / 托盘）。
    // 插件此时尚未 start，但贡献点是「声明 + 延迟视图」，可安全收集。
    if (m_uiHost == nullptr) {
        return;
    }
    if (m_menu != nullptr) {
        const int count = m_uiHost->appendToMenu(m_plugins, plugin::ContributionKind::ContextMenu,
                                                 m_menu, m_contextQuitAction);
        qInfo() << "[PetWindow] 右键菜单 UI 贡献点:" << count;
    }
    if (m_trayMenu != nullptr) {
        const int count = m_uiHost->appendToMenu(m_plugins, plugin::ContributionKind::TrayMenu,
                                                 m_trayMenu, m_trayQuitAction);
        qInfo() << "[PetWindow] 托盘菜单 UI 贡献点:" << count;
    }
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
        resetToDefaultPosition(); // 回到屏幕正中央
    });

    // P9-C：原「状态」项已迁移为 UI 贡献点（由 setupUiContributions 挂载）

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

    // P7 工作状态感知（默认关：隐私优先；开启后采集前台应用与输入活跃度，只计数不含内容）
    m_workAwareAction = m_menu->addAction(QStringLiteral("工作状态感知"));
    m_workAwareAction->setCheckable(true);
    connect(m_workAwareAction, &QAction::toggled, this, &PetWindow::setWorkAware);

    // P7 本地 Context API（默认关：关闭时不监听任何端口、不注册上下文能力）
    m_contextApiAction = m_menu->addAction(QStringLiteral("本地 Context API"));
    m_contextApiAction->setCheckable(true);
    connect(m_contextApiAction, &QAction::toggled, this, &PetWindow::setContextApiEnabled);

    // P7.5 ACP / IDE 显式信号（默认关：轮询信号文件，并把显式状态作为覆盖性输入）
    m_acpAction = m_menu->addAction(QStringLiteral("ACP / IDE 信号"));
    m_acpAction->setCheckable(true);
    connect(m_acpAction, &QAction::toggled, this, &PetWindow::setAcpEnabled);

    // EX1.4 游戏陪玩（默认关：只读感知目标游戏状态，关闭时不打开任何进程、不采样）
    m_gameCompanionAction = m_menu->addAction(QStringLiteral("游戏陪玩"));
    m_gameCompanionAction->setCheckable(true);
    connect(m_gameCompanionAction, &QAction::toggled, this, &PetWindow::setGameCompanion);

    // 立绘激活 18：回收站清理提醒（默认开；非空时展示 sweep 立绘 + 清理提醒）
    m_recycleBinAction = m_menu->addAction(QStringLiteral("回收站清理提醒"));
    m_recycleBinAction->setCheckable(true);
    connect(m_recycleBinAction, &QAction::toggled, this, &PetWindow::setRecycleBinReminder);

    // P8 问答系统（默认开：只在静息时低频提醒；面板为「主人的问题」五选一）
    m_dialogueAction = m_menu->addAction(QStringLiteral("我可以提问（主人的问题）"));
    m_dialogueAction->setCheckable(true);
    connect(m_dialogueAction, &QAction::toggled, this, &PetWindow::setDialogueEnabled);
    m_dialogueAskAction = m_menu->addAction(QStringLiteral("我想问鲸鱼娘…"));
    connect(m_dialogueAskAction, &QAction::triggered, this, &PetWindow::askDialogueNow);

    m_menu->addSeparator();

    m_contextQuitAction = m_menu->addAction(QStringLiteral("退出"));
    connect(m_contextQuitAction, &QAction::triggered, qApp, &QApplication::quit);
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

    m_trayMenu = new QMenu(this);
    QMenu *trayMenu = m_trayMenu;
    // 与右键菜单同理：托盘菜单也要置顶，否则同样可能被置顶立绘压住
    configurePopupMenu(trayMenu);
    QAction *toggle = trayMenu->addAction(QStringLiteral("显示 / 隐藏"));
    connect(toggle, &QAction::triggered, this, [this] { setPetVisible(!m_petEnabled); });

    // P9-C：原「状态」项已迁移为 UI 贡献点（由 setupUiContributions 挂载）

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

    m_trayQuitAction = trayMenu->addAction(QStringLiteral("退出"));
    connect(m_trayQuitAction, &QAction::triggered, qApp, &QApplication::quit);

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

    // 启动位置：**不**恢复上次保存的坐标，而是按「当前」屏幕配置把桌宠放到主屏正中央。
    // 分辨率调整 / 监视器增删后旧坐标可能落在可视区域之外，表现为「立绘无法显示」；
    // 每次启动按此刻的主屏可用区域重新居中，即可适配任意分辨率与显示器布局（见 defaultPosition）。
    resetToDefaultPosition();
    clampToVisibleArea(); // 位置越界防护（SETTINGS.md §5）
    show();

#ifdef Q_OS_WIN
    // 拖拽投喂可用性（见 docs/pitfalls/）：提权（High IL）运行时，资源管理器
    // （Medium IL）的拖放被 UIPI 拦截，窗口收不到 dragEnterEvent → 光标显示「禁止投放」。
    // 这里对窗口放行相关消息（尽力而为），并对「以管理员身份运行」给出可观测告警。
    allowDragDropFromLowerIntegrity(winId());
    if (isRunningElevated()) {
        qWarning() << "[PetWindow] 以管理员（高完整性级别）身份运行：Windows 会拦截资源管理器的拖放，"
                      "拖拽投喂可能不可用；请改用普通用户身份启动（安装器完成页已改为普通用户启动）。";
    }
#endif

    m_controller->start();

    // P7：启动已注册插件（生命周期钩子；内置层当前无 start 逻辑，此处保证钩子被调用）
    {
        plugin::PluginContext pluginCtx;
        pluginCtx.controller = m_controller;
        pluginCtx.db = m_db;
        m_plugins.startAll(pluginCtx);
    }

    // P7.4：按 <数据目录>/plugins.json 拉起外部进程插件（MCP Client；无配置则零开销）
    setupProcessPlugins();

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

    // applySettings 可能按 pose_size 改变窗口尺寸：按最终尺寸再居中一次，保证精确居中。
    resetToDefaultPosition();
    clampToVisibleArea();
    syncDesktopEdge(); // 尺寸 / 位置都定型后再判定一次贴边

    // 运行期分辨率 / 显示器变化：保持桌宠可见（避免「改完分辨率立绘就看不见」）。
    watchScreenChanges();
}

QPoint PetWindow::defaultPosition() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QPoint(100, 100);
    }
    // 「初始位置」= 当前主屏**可用区域**（已扣除任务栏）的几何中心：
    // 每次启动都按此刻的屏幕配置重新计算，故分辨率调整、显示器增删后依旧精确居中。
    const QRect area = screen->availableGeometry();
    const QPoint center = area.center();
    return QPoint(center.x() - width() / 2, center.y() - height() / 2);
}

void PetWindow::resetToDefaultPosition()
{
    move(defaultPosition());
    if (m_bubble != nullptr) {
        m_bubble->reposition();
    }
    // 位置未变（已居中）时不会触发 moveEvent，这里显式判定一次
    syncDesktopEdge();
}

// 运行期分辨率 / 显示器变化：只做「夹回可见区域」的最小纠正（不强行居中，
// 以免把用户拖好的位置无端重置）。覆盖两类事件：
//   1) 已有屏幕的 geometry / availableGeometry 变化（改分辨率、任务栏位置变化）；
//   2) 显示器增删（screenAdded / screenRemoved）——移除后主屏可能改变，必须重新夹回。
void PetWindow::watchScreenChanges()
{
    if (m_screenWatchInstalled) {
        return;
    }
    m_screenWatchInstalled = true;

    const auto keepVisible = [this] {
        clampToVisibleArea();
        repositionRecallEntry();
        // 屏幕几何 / 可用区域变化会改变「贴合哪条边框」，位置未变时也要重判
        syncDesktopEdge();
    };
    const auto watchScreen = [this, keepVisible](QScreen *screen) {
        if (screen == nullptr) {
            return;
        }
        connect(screen, &QScreen::geometryChanged, this,
                [keepVisible](const QRect &) { keepVisible(); });
        connect(screen, &QScreen::availableGeometryChanged, this,
                [keepVisible](const QRect &) { keepVisible(); });
    };

    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        watchScreen(screen);
    }
    connect(qApp, &QGuiApplication::screenAdded, this,
            [watchScreen, keepVisible](QScreen *screen) {
                watchScreen(screen);
                keepVisible();
            });
    connect(qApp, &QGuiApplication::screenRemoved, this,
            [keepVisible](QScreen *) { keepVisible(); });
}

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 深夜虚弱（2026-10-04）：角色不再响应鼠标 —— 本次按压整体作废：
        // 不拖窗、不进拖拽态、不触发点击反馈与交互（因此也不会刷新唤醒窗口）。
        if (m_controller != nullptr && m_controller->lateNightWeak()) {
            event->accept();
            return;
        }
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
        snapToDesktopEdge();  // 贴近桌面边框 → 吸附为完全贴合（贴边立绘贴齐边框）
        clampToVisibleArea(); // 松手后夹回可见区域，避免拖出屏幕找不到
        syncDesktopEdge();    // 吸附 / 夹回后按最终位置刷新贴边方向
    } else {
        // 单击：分区命中 → 即时反馈 + 语义事件。
        // 深夜虚弱时 handleClick 返回 false（角色不响应）→ 连点击反馈动画也不播放。
        const core::Zone zone = m_pose->zoneAt(m_pressViewPos);
        if (m_controller == nullptr || m_controller->handleClick(zone)) {
            m_pose->clickFeedback();
        }
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
    // 深夜虚弱（2026-10-04）：不响应投喂 —— 在「入胃」之前就拒绝，
    // 避免出现「文件已经进了肚子、角色却毫无反应」的不一致状态。
    if (m_controller != nullptr && m_controller->lateNightWeak()) {
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
    // 位置变化即重新判定贴边：拖拽过程中立绘会随靠近边框实时切换为探头立绘
    syncDesktopEdge();
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
