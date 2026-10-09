#pragma once

// PetWindow：桌宠主窗口（docs/ARCHITECTURE.md §3 View 层）。
//
// 透明 / 无边框 / 置顶 / 不抢焦点（Qt::Tool），承载立绘与台词气泡。
// 窗口尺寸**固定**为 kPetWindowSize：93 张立绘已统一 256x256，
// 切换姿态不再 resize 窗口（动效全部在 PoseView 内容层完成）。
//
// 职责边界：本类只把鼠标手势翻译成 PoseView 的即时反馈 + PetController 的语义事件，
// 不做任何状态判定（状态判定在 core/PetStateMachine）。
//
// 例外：**桌面四边框贴边判定**（docs/PRESENTATION.md §3.1）需要窗口位置与屏幕几何，
// 天然属于本类——判定结果只下发给 PoseView 换图，不进入状态机。

#include "common/PetVisuals.h"
#include "core/DesktopEdge.h"
#include "minigame/MiniGameRegistry.h"
#include "plugin/PluginRegistry.h" // P7：通用能力总线（值成员，需要完整类型）
#include "viewmodel/builtin/BuiltinServicePlugins.h" // P9-A：宿主服务插件句柄 / 宿主回调

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <memory>

class QAction;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMenu;
class QPushButton;
class QSystemTrayIcon;

namespace whalepet {

namespace model {
class Database;
struct SettingsData;
} // namespace model

namespace platform {
class IEnvironmentObserver;
} // namespace platform

namespace plugin {
class ProcessPluginLoader;
class DllPluginLoader; // P7.3：动态插件（DLL）
} // namespace plugin

namespace ui {
class StatusPanelUiPlugin; // P9-C：UI 面板型插件（试点）
class UiContributionHost;  // P9-C：UI 宿主上下文 + 贡献点分发
} // namespace ui

namespace contextapi {
class AcpClient;
class AcpSignalSource;
class ContextApiService;
} // namespace contextapi

namespace viewmodel {
class AchievementService;
class AcpSignalService;
class DialogueService;
class EasterEggService;
class EnvironmentService;
class GameCompanionService;
class GrowthService;
class MiniGameService;
class PetContextProvider;
class QuestService;
class RecycleBinService; // 立绘激活 18：回收站清理提醒
class SigninService;
class StomachService;
class WeatherService;
class WorkStateService;
} // namespace viewmodel

class ContentPanel;
class DialoguePanel;
class GlobalHotkey;
class HotwordDialog;
class MiniGameView;
class PoseLibrary;
class PoseView;
class PetController;
class SettingsDialog;
class SpeechBubble;
class StatusPanel;

class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow() override;

    // 启动立绘预载与定时驱动、按当前屏幕居中并显示窗口与托盘
    void showPet();

    PoseView *poseView() const { return m_pose; }
    SpeechBubble *speechBubble() const { return m_bubble; }
    PetController *controller() const { return m_controller; }
    viewmodel::GrowthService *growthService() const { return m_growth; }
    viewmodel::AchievementService *achievementService() const { return m_achievement; }
    viewmodel::QuestService *questService() const { return m_quest; }
    viewmodel::SigninService *signinService() const { return m_signin; }
    viewmodel::StomachService *stomachService() const { return m_stomach; }
    viewmodel::RecycleBinService *recycleBinService() const { return m_recycleBin; }
    viewmodel::MiniGameService *miniGameService() const { return m_miniGameService; }
    model::Database *database() const { return m_db; }

    // ---- P7：工作状态感知与本地 Context API ----
    viewmodel::EnvironmentService *environmentService() const { return m_environment; }
    viewmodel::WorkStateService *workStateService() const { return m_workState; }
    contextapi::ContextApiService *contextApiService() const { return m_contextApi; }
    viewmodel::AcpSignalService *acpSignalService() const { return m_acpSignal; }
    contextapi::AcpClient *acpClient() const { return m_acpClient.get(); }
    // EX1.4：游戏陪玩采样服务（默认关闭；供诊断与单测）
    viewmodel::GameCompanionService *gameCompanionService() const { return m_gameCompanion; }
    // P8：预设对话展示面板与编排服务（供诊断与单测）
    DialoguePanel *dialoguePanel() const { return m_dialoguePanel; }
    viewmodel::DialogueService *dialogueService() const;
    viewmodel::WeatherService *weatherService() const;
    // EX 彩蛋：代码彩蛋服务（供诊断与单测）
    viewmodel::EasterEggService *easterEggService() const { return m_easterEgg; }
    plugin::ProcessPluginLoader *processPluginLoader() const { return m_processPlugins; }
    plugin::PluginRegistry *pluginRegistry() { return &m_plugins; }

    // 回到「初始位置」：当前主屏可用区域的几何中心（每次启动都会调用）
    void resetToDefaultPosition();

    // 当前贴合的桌面边框（None = 未贴边）；供诊断与单测
    core::DesktopEdge desktopEdge() const;

    // 状态面板（P3）：显示并刷新
    void showStatusPanel();

    // 内容面板（P4）：日常 / 成就墙 / 成长日记
    void showContentPanel();

    // 设置面板（P6）：陪伴表现 / 日常·成就·日记 / 小游戏 / 数据与重置
    void showSettingsDialog();

    // 小游戏（插件化）：按插件 id 显示窗口（右键 / 托盘菜单与设置面板共用入口）
    void showMiniGame(const QString &pluginId);

signals:
    void feedRequested();      // 投喂
    void pokeRequested();      // 戳一下
    void praiseRequested();    // 夸夸

protected:
    // 用于把右键菜单/托盘菜单抬到置顶立绘之上（见 setupContextMenu）
    bool eventFilter(QObject *watched, QEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    // 拖拽投喂：判定区域 = 本窗口的整个矩形（桌宠所在区域）。
    // 仅接受含本地文件/文件夹（text/uri-list）的拖放；落点触发与「投喂」完全相同的表现与数值。
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupWindowFlags();
    // 装配一个弹出菜单（右键 / 托盘 / 「小游戏…」子菜单共用）：置顶 + 显示时 raise
    void configurePopupMenu(QMenu *menu);
    void setupContextMenu();
    void setupTray();
    void setupController();
    // P9-A：数据库属共享基础设施，仍由宿主创建（不作为服务插件）
    void setupDatabase();
    // P9-A：把宿主服务（养成 / 胃袋 / 对话 / 彩蛋 / 回收站）经 builtin 层注册化
    void setupBuiltinServices();
    void setupGrowth();
    void setupContent();
    void setupStomach(); // 拖拽投喂：stomach 目录 + 每 5 分钟清空到回收站
    void setupRecycleBin(); // 立绘激活 18：随机轮询回收站 → sweep 立绘 + 清理提醒
    void setupChat();    // P5：加载 keyword_aware 并挂接剪贴板触发源
    void setupHotword(); // P6：注册全局热键 + 载入自定义热词（失败仅降级为菜单入口）
    void setupMiniGames();   // 小游戏插件：注册内置插件（必须在构建菜单之前调用）
    // P7.3：动态插件（DLL）——扫描 <应用目录>/plugins 并装载（必须在构建菜单之前调用）
    void setupDllPlugins();
    // P9-C：注册 UI 面板型插件（builtin 层；在服务装配之后调用）
    void setupUiPlugins();
    // P9-C：把插件声明的 UI 贡献点挂载到右键 / 托盘菜单（在菜单构建之后调用）
    void setupUiContributions();
    void setupSettings();    // P6：设置面板（懒创建在 showSettingsDialog）
    void setupRecallEntry(); // P6：左下角唤回入口（桌宠隐藏时显示）
    // P7：感知采样 + 工作状态判定链路（依赖 m_controller）
    void setupWorkState();
    // P7：本地 Context API 装配（依赖 m_growth / m_environment / m_workState / m_controller）
    void setupContextApi();
    // P7.5：ACP / IDE 显式信号装配（轮询信号源 → 覆盖性工作态）
    void setupAcp();
    // EX1.4：游戏陪玩装配（依赖 m_controller / m_contextProvider）
    void setupGameCompanion();
    // EX4：按「是否有小游戏窗口可见」启停陪玩采样（零开销：无小游戏时不采样）
    void syncGameCompanion();
    // P8：预设对话装配（提问面板 + 门槛回调 + 设置应用；依赖 m_controller）
    void setupDialogue();
    void applyDialogueSettings(const model::SettingsData &data);
    // EX 彩蛋：代码彩蛋装配（依赖 m_controller；开关与工作区由 applySettings 注入）
    void setupEasterEgg();
    void applyCodeEggSettings(const model::SettingsData &data);
    // 立绘激活 18：回收站清理提醒（设置生效 / 启停）
    void applyRecycleBinSettings(const model::SettingsData &data);
    void setRecycleBinReminder(bool on);
    void setDialogueEnabled(bool on); // 启停低频提问（并落库由 SettingsDialog 负责）
    bool dialogueCanAsk() const;      // 主动提问门槛：静息 / 非深夜 / 气泡空闲 / 桌宠可见
    void askDialogueNow();            // 「现在就聊一句」：跳过静息门槛（用户主动要求）
    // P7.6：ACP 客户端（子进程）——配置 dsh 路径后启动，并建立 / 接管会话
    void startAcpClient();
    void stopAcpClient();
    void attachAcpSession();
    void applyAcpClientConfig(const model::SettingsData &data);
    QString acpWorkspacePath() const; // 会话工作目录（配置为空时回落数据目录 / 当前目录）
    // P7.4：外部进程插件（MCP Client）装配：读 <数据目录>/plugins.json 并拉起
    void setupProcessPlugins();
    QString defaultAcpSignalPath() const; // 数据目录 / 用户目录下的 acp-signals.jsonl

    QPoint defaultPosition() const;   // 当前主屏可用区域的几何中心
    void watchScreenChanges();        // 运行期分辨率 / 显示器变化时保持桌宠可见

    // 桌面四边框贴边（docs/PRESENTATION.md §3.1）
    void syncDesktopEdge();   // 判定窗口贴合了哪条边框并下发 PoseView
    void snapToDesktopEdge(); // 拖拽松手：贴近边框时对齐为完全贴合
    core::DesktopEdge currentDesktopEdge() const; // 纯判定，不改动任何状态

    // P6 设置生效与「找不到看板娘」防护
    void applySettings(const model::SettingsData &data); // 把库中设置应用到界面
    void setPetVisible(bool visible);                    // 桌宠显隐（联动托盘与唤回入口）
    void repositionRecallEntry();                        // 左下角唤回入口定位
    void clampToVisibleArea();                           // 位置越界夹回可见区域
    void openDataDirectory();                            // 打开 data/ 目录

    // 小游戏插件统一结算：奖励（每日上限）+ 成就 + 文案回填
    void settleMiniGame(const core::MiniGameResult &result);

    // P7：把库中的感知 / Context API 设置应用到运行期（默认关：不采样、不监听）
    void applyWorkStateSettings(const model::SettingsData &data);
    void setWorkAware(bool on);          // 工作状态感知开关（采样 + 判定 + 持久化）
    void setContextApiEnabled(bool on);  // 本地 Context API 开关（监听 + 能力可用性 + 持久化）
    void setAcpEnabled(bool on);         // ACP / IDE 显式信号开关（轮询 + 覆盖性工作态 + 持久化）

    // 【EX3 已移除】原 EX1.4「外部游戏陪玩」的设置生效 / 启停 / 档案路径回落已删除；
    //   陪玩管线（服务 / 状态机通道 / 判定）保留，EX4 由小游戏陪玩接管。

    void syncStatusPanel();
    void fillStatusPanel(StatusPanel *panel); // P9-C：把最新养成数据写入指定面板
    void syncContentPanel();
    bool keywordAware() const;          // 关键词感知当前是否开启
    void setKeywordAware(bool on);      // 应用 + 持久化 keyword_aware
    void showHotwordDialog();           // 打开/抬起「热词录入」面板（全局热键与菜单共用）
    void reloadHotwords();              // hotwords 表 → ChatService + 面板列表
    void syncAchievementProgress();
    void handleSignIn();       // 一次签到同时驱动 GrowthService 与 SigninService
    void checkComeback();      // 离开 2 小时后回来 → 「欢迎回来」成就
    QString storageInfo() const;

    static constexpr int kDragThreshold = 4;   // 移动超过 4px 才进入拖拽
    static constexpr int kVelocityResetMs = 120; // 停顿超过该时长则松手速度视为 0

    PoseLibrary *m_library = nullptr;
    PoseView *m_pose = nullptr;
    SpeechBubble *m_bubble = nullptr;
    PetController *m_controller = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_keywordAction = nullptr; // 「关键词感知」勾选项（P5）
    QAction *m_recycleBinAction = nullptr; // 「回收站清理提醒」勾选项（立绘激活 18）
    QAction *m_hotwordAction = nullptr; // 「热词录入」入口（P6）
    QMenu *m_miniGameMenu = nullptr;         // 右键菜单「小游戏…」子菜单（内容由插件动态生成）
    QMenu *m_trayMiniGameMenu = nullptr;     // 托盘菜单「小游戏…」子菜单
    QList<QAction *> m_miniGameEntryActions; // 各小游戏入口项（受 minigame_enabled 统一门控）
    GlobalHotkey *m_hotkey = nullptr;   // 系统级热键（P6）
    HotwordDialog *m_hotwordDialog = nullptr; // 懒创建，随主窗口析构
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr; // P9-C：托盘菜单（UI 贡献点挂载点）
    ContentPanel *m_contentPanel = nullptr;
    SettingsDialog *m_settingsDialog = nullptr; // 懒创建，随主窗口析构
    QHash<QString, MiniGameView *> m_miniGameViews; // 懒创建的小游戏窗口（先于 m_db 释放）
    MiniGameRegistry m_miniGames;                   // 已注册的小游戏插件
    QPushButton *m_recallButton = nullptr;      // P6：左下角唤回入口
    bool m_petEnabled = true;                   // 设置项 pet_enabled 的运行时镜像

    // ---- P7：通用能力总线 + 感知 / 工作状态 / Context API ----
    // P7.3：动态插件装载器必须**先于** m_plugins 声明（成员逆序析构）：
    // 先销毁插件实例（m_plugins），再卸载 DLL（QPluginLoader 析构），否则会卸载仍在使用的代码。
    std::unique_ptr<plugin::DllPluginLoader> m_dllPlugins;
    // P9-C：UI 宿主上下文（G1）+ 贡献点分发（G2）。先于 m_plugins 声明 → 后于其析构，
    // 保证插件实例先释放其界面资源，再释放贡献点宿主。
    std::unique_ptr<ui::UiContributionHost> m_uiHost;
    plugin::PluginRegistry m_plugins; // 能力总线（小游戏适配 + 上下文能力 + P9-A 宿主服务 + P9-C UI 面板）
    // P9-A：宿主服务插件（builtin 层）。句柄由各插件 start() 回填，宿主据此做 UI 反应接线。
    BuiltinServiceHooks m_serviceHooks;
    BuiltinServiceHandles m_serviceHandles;
    // P9-C：UI 面板型插件（非拥有；由 m_plugins 持有）与贡献点插入锚点
    ui::StatusPanelUiPlugin *m_statusUiPlugin = nullptr;
    QAction *m_contextQuitAction = nullptr; // 右键菜单「退出」项（贡献点插在其前）
    QAction *m_trayQuitAction = nullptr;    // 托盘菜单「退出」项
    std::unique_ptr<platform::IEnvironmentObserver> m_observer; // Win32：真实采集；其它平台：空实现
    viewmodel::EnvironmentService *m_environment = nullptr;      // 采样调度
    viewmodel::WorkStateService *m_workState = nullptr;          // 状态判定与上报
    viewmodel::PetContextProvider *m_contextProvider = nullptr;  // IContextProvider 实现
    contextapi::ContextApiService *m_contextApi = nullptr;       // 通道装配与门控
    // P7.5：ACP / IDE 显式信号（默认关；轮询信号文件并把显式状态作为覆盖性输入）
    std::unique_ptr<contextapi::AcpSignalSource> m_acpSource;
    viewmodel::AcpSignalService *m_acpSignal = nullptr;
    // P7.6：ACP（Agent Client Protocol）客户端 —— 由 dsh 提供实时 agent 工作状态
    std::unique_ptr<contextapi::AcpClient> m_acpClient;
    QString m_acpDshPath;   // 空 = 不启动子进程
    QString m_acpProfile;   // 空 = "acp"
    QString m_acpWorkspace; // 空 = 数据目录
    // P7.4：外部进程插件（MCP Client；按 <数据目录>/plugins.json 拉起）
    plugin::ProcessPluginLoader *m_processPlugins = nullptr;
    QAction *m_workAwareAction = nullptr;   // 「工作状态感知」勾选项
    QAction *m_contextApiAction = nullptr;  // 「本地 Context API」勾选项
    QAction *m_acpAction = nullptr;         // 「ACP / IDE 信号」勾选项

    // 陪玩管线（EX3：外部数据源已移除；EX4 由小游戏陪玩接入数据源）
    viewmodel::GameCompanionService *m_gameCompanion = nullptr; // 采样调度与判定编排

    // P8：预设对话（提问面板 + 开关 / 立即提问入口；服务由 PetController 持有）
    DialoguePanel *m_dialoguePanel = nullptr;
    QAction *m_dialogueAction = nullptr;    // 「预设对话（陪我聊聊）」勾选项
    QAction *m_dialogueAskAction = nullptr; // 「现在就聊一句」即时入口

    // EX 彩蛋：代码彩蛋（戳一戳 → 5% 概率在用户工作区源码注释里藏俏皮话；工作区为空则不动作）
    viewmodel::EasterEggService *m_easterEgg = nullptr;

    model::Database *m_db = nullptr;
    viewmodel::GrowthService *m_growth = nullptr;
    viewmodel::AchievementService *m_achievement = nullptr;
    viewmodel::QuestService *m_quest = nullptr;
    viewmodel::SigninService *m_signin = nullptr;
    viewmodel::StomachService *m_stomach = nullptr; // 「胃袋」：拖入落盘 + 定时清空
    viewmodel::RecycleBinService *m_recycleBin = nullptr; // 立绘激活 18：回收站清理提醒
    viewmodel::MiniGameService *m_miniGameService = nullptr; // 扫雷结算：奖励上限 + 个人最快

    // 2026-10-04 立绘激活：一次性表现出场记录
    int m_lastUnlockedCount = 0;  // 成就解锁数（用于「集齐全部成就 → meme-smug」只触发一次）
    QString m_celebrateDayKey;    // 每日 3 局庆祝所属自然日（每天只播一次）

    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobalPos;
    QPoint m_pressViewPos;
    QPoint m_dragOffset;

    // 松手惯性用的速度估计（px/ms，指数平滑）
    QElapsedTimer m_dragClock;
    QPoint m_lastMovePos;
    qint64 m_lastMoveMs = 0;
    QPointF m_velocity;

    bool m_screenWatchInstalled = false; // watchScreenChanges 只装配一次
};

} // namespace whalepet
