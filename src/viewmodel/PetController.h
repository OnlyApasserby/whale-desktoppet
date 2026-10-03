#pragma once

// PetController：P2 阶段的总调度（docs/ARCHITECTURE.md §3 ViewModel 层）。
//
// 「用户/定时事件 → PetStateMachine → PosePresenter → 立绘与气泡」的唯一串联点。
// View（PetWindow）只负责把鼠标手势翻译成这里的方法调用，并读取只读状态；
// 状态机与台词表都是纯逻辑，可脱离界面单测。

#include "core/GrowthRules.h"
#include "core/IRandom.h"
#include "core/LineTable.h"
#include "core/PetStateMachine.h"
#include "core/PetTypes.h"

#include <QObject>

class QTimer;

namespace whalepet {

namespace viewmodel {
class ChatService;
class GrowthService;
} // namespace viewmodel

class PosePresenter;
class PoseView;
class SpeechBubble;

class PetController : public QObject {
    Q_OBJECT
public:
    PetController(PoseView *view, SpeechBubble *bubble, QObject *parent = nullptr);

    // 注入养成服务（P3）：交互事件按 whale 数值表落到养成状态，
    // 升级/羁绊提升回灌为 core::EventType::LevelUp 驱动庆祝姿态。
    // 允许为空（P1/P2 的既有测试即不注入），此时行为与 P2 完全一致。
    void setGrowthService(viewmodel::GrowthService *growth);

    // 启动/停止定时驱动（tick + 系统时钟）
    void start();
    void stop();
    bool running() const;

    // ---- 输入 ----
    void handleClick(core::Zone zone);        // 分区单击（内部含三连击判定）
    void handleDragBegin();
    void handleDragEnd();
    void handleMenuAction(core::EventType type); // Feed / Tease / Praise

    // 签到成功的交互广播（供内容层：「今日签到」每日任务 / 成就计数）。
    // 签到走 GrowthService::signIn() + SigninService::markToday() 这条独立链路，
    // **不**经 applyGrowthForEvent（否则养成侧会重复累加 Signin 数值），
    // 故由组合根在签到成功后显式调用本方法，把 Signin 交互播给内容层。
    void reportSignIn();

    // 外部系统事件（P3 起由 GrowthService / ChatService 驱动）
    void handleEvent(core::EventType type);

    // 小游戏（扫雷）表现：把 pose / 台词场景交给状态机与 Presenter。
    // 游戏是用户主动行为 → proactive=false，不受深夜静默与主动台词节流限制；
    // sceneKey 为空时只切立绘、不播台词（保留参考项目「进行游戏时立绘变化」的表现）。
    void presentGame(const QString &pose, const QString &sceneKey, int ttlMs = 0);
    // 关键词 id（如 "omg"）→ 表情立绘 + 梗台词；无立绘/无台词的 id 优雅跳过
    void handleKeywordHit(const QString &keyword);
    // 外部文本（剪贴板）→ 关键词感知；keyword_aware 关闭时不做任何事
    void handleText(const QString &text);
    // 显式录入文本（全局热键 / 菜单「热词录入」）→ 匹配自定义热词 + 内置触发词并触发。
    // 与 handleText 的区别：**不做 keyword_aware 门控**（主动动作，等同点一下桌宠）。
    void handleHotwordInput(const QString &text);

    // 游戏/设置面板打开时抑制主动小剧场
    void setSuppressed(bool suppressed);

    // P7 工作状态（docs/PLUGIN-ARCHITECTURE.md §6.2）：由 viewmodel::WorkStateService 上报。
    //   - 状态未变时直接返回（天然去抖，不打断当前表现）；
    //   - 状态变化 → 经状态机 WorkStateChanged 事件切换立绘并播报一句（受节流/深夜/抑制约束）；
    //   - core::WorkState::Unknown 表示「无感知数据」→ 退回既有行为（零回归）。
    void handleWorkState(core::WorkState state, double confidence = 0.0);
    core::WorkState workState() const { return m_sm.workState(); }

    // EX1.4 游戏陪玩（docs/ROADMAP-ex1.md §2.5）：由 viewmodel::GameCompanionService 上报。
    //   - 持续态/特殊场景未变且无里程碑 → 直接返回（200ms 高频档天然去抖，不打断当前表现）；
    //   - 变化 → 经状态机 GameStateChanged 按**最低让位优先级**切换立绘；
    //   - 仅里程碑（高置信度）才主动播报，且特殊场景下「静默陪伴」（见 PetStateMachine）。
    void handleGameState(const core::GameCompanionSample &stable,
                         const core::GameMilestoneSet &milestones, int specialScene);
    core::GameMood gameMood() const { return m_sm.gameMood(); }
    int gameSpecialScene() const { return m_sm.gameSpecialScene(); }
    bool gameCompanionSilent() const { return m_sm.gameCompanionSilent(); }

    core::PetStateMachine &stateMachine() { return m_sm; }
    const core::LineTable &lineTable() const { return m_lines; }
    viewmodel::ChatService *chatService() const { return m_chat; }

signals:
    // 一次语义交互（摸头/摸肚子/尾巴/戳/投喂/夸夸/三连击/签到）发生后广播。
    // P4 内容层（成就计数 / 每日任务进度）据此上报；与是否注入养成服务无关，
    // 因此既便未接入 GrowthService 也能被单测/内容层观察到。
    void interactionOccurred(core::Interaction type, qint64 nowMs);

private:
    // 分区/菜单事件 → 养成交互（无对应养成交互时不做任何事）；
    // 同时把发生的交互经 interactionOccurred 播出去。
    void applyGrowthForZone(core::Zone zone);
    void applyGrowthForEvent(core::EventType type);

    // 经状态机 speak() 产出一句（姿态可选）+ 台词并交给 Presenter；无台词则不表现
    void presentSpeak(const std::string &pose, const std::string &scene, int ttlMs, bool proactive);
    void onGrowthChanged();

    void onTick();
    void onClockTick();
    void presentCurrent();

    // 统一时间基准：**系统墙钟**（Unix 毫秒）。
    // P2 曾用 QElapsedTimer（进程启动起算），该值经 interactionOccurred 泄漏到内容层，
    // 使任务 / 成就 / 成长日记以「1970 起算」的时间戳落库（见 docs/traps-P4.md）。
    // 状态机只用事件时间差，故切换为墙钟不影响其判定。
    qint64 nowMs() const;

    core::SystemRandom m_rng;
    core::LineTable m_lines;
    core::PetStateMachine m_sm;
    PosePresenter *m_presenter = nullptr;
    viewmodel::ChatService *m_chat = nullptr;
    viewmodel::GrowthService *m_growth = nullptr;

    QTimer *m_tickTimer = nullptr;
    QTimer *m_clockTimer = nullptr;

    int m_clickStreak = 0;
    qint64 m_lastClickMs = -1;
    int m_lastHour = -1;
};

} // namespace whalepet
