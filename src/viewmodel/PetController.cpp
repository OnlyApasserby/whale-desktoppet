#include "viewmodel/PetController.h"

#include "common/PetVisuals.h"
#include "core/ChatRules.h"
#include "core/IdleRules.h"
#include "viewmodel/ChatService.h"
#include "viewmodel/DialogueService.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/PosePresenter.h"
#include "viewmodel/WeatherService.h"

#include <QDateTime>
#include <QDebug>
#include <QTime>
#include <QTimer>

namespace whalepet {

PetController::PetController(PoseView *view, SpeechBubble *bubble, QObject *parent)
    : QObject(parent)
    , m_sm(&m_rng)
    , m_presenter(new PosePresenter(view, bubble, this))
{
    m_presenter->setRandom(&m_rng);
    m_presenter->setLineTable(&m_lines);

    // 台词语料加载一次：缺失只降级（不说话），不影响立绘与动效
    PosePresenter::loadBundledLines(m_lines);

    // 梗聊天编排（P5）：与 Presenter 共用同一份台词表；场景决策 + 关键词感知
    m_chat = new viewmodel::ChatService(&m_lines, this);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(static_cast<int>(core::kTickMs));
    connect(m_tickTimer, &QTimer::timeout, this, &PetController::onTick);

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(30'000);
    connect(m_clockTimer, &QTimer::timeout, this, &PetController::onClockTick);

    // P8：预设对话（与 ChatService 共用同一份台词表；回答文本以 dialogue.<id>.<slot> 登记）
    m_weather = new viewmodel::WeatherService(this);
    m_dialogue = new viewmodel::DialogueService(&m_lines, this);
    m_dialogue->setWeatherService(m_weather);
    m_dialogue->loadBundled(); // 语料缺失只降级为空池（不提问），不影响既有表现

    // 用户选择回答 → 立绘 + 台词走既有 speak 管线（用户主动交互，不受节流/深夜静默限制）。
    // ttl 用好奇窗口：回答后保持一小会儿，到期自动回落到上下文常驻立绘。
    connect(m_dialogue, &viewmodel::DialogueService::answered, this,
            [this](const QString &pose, const QString &sceneKey) {
                presentGame(pose, sceneKey, static_cast<int>(core::kCuriousWindowMs));
            });
}

qint64 PetController::nowMs() const
{
    // 内容层（成就 / 任务 / 签到 / 成长日记）需要真实 Unix 时间戳落库，
    // 统一以系统墙钟为基准；状态机内部只使用时间差，兼容该基准。
    return QDateTime::currentMSecsSinceEpoch();
}

void PetController::setGrowthService(viewmodel::GrowthService *growth)
{
    m_growth = growth;
    if (m_growth == nullptr) {
        return;
    }

    // 升级 / 羁绊提升 → 庆祝姿态（状态机在 P2 已实现 LevelUp 事件分支）
    connect(m_growth, &viewmodel::GrowthService::levelUp, this, [this](int level) {
        Q_UNUSED(level);
        m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::LevelUp, nowMs())));
    });
    connect(m_growth, &viewmodel::GrowthService::bondUp, this, [this](int level) {
        if (core::bondUnlocks(level).badge) {
            // Lv5 起羁绊提升额外播一次庆祝（同一事件类型，语义差异留给台词表）
            m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::LevelUp, nowMs())));
        }
        // 羁绊专属台词（CHAT.md §3）：跨过 Lv3 / Lv5 / Lv7 时播报一次
        if (m_chat != nullptr) {
            const std::string scene = m_chat->bondSceneFor(level);
            if (!scene.empty()) {
                presentSpeak({}, scene, 0, true);
            }
        }
    });

    // 心情分层（CHAT.md §3）：心情档位变化（Low / High）时播报专属台词
    connect(m_growth, &viewmodel::GrowthService::stateChanged, this,
            &PetController::onGrowthChanged);
    onGrowthChanged(); // 建立心情基线：首次观察只记录，不发言
}

void PetController::onGrowthChanged()
{
    if (m_growth == nullptr) {
        return;
    }
    // 2026-10-04 立绘激活：养成数值注入状态机
    //   - affinity → wink 是否参与日间待机池（门槛 5000）
    //   - mood / satiety → 同时满值时持续摇尾巴（tail-swing）
    const auto &state = m_growth->state();
    m_sm.setAffinity(state.affinity);
    m_sm.setVitals(state.mood, state.satiety);

    if (m_chat == nullptr) {
        return;
    }
    const std::string scene = m_chat->moodSceneFor(state.mood);
    if (!scene.empty()) {
        presentSpeak({}, scene, 0, true);
    }
}

void PetController::presentSpeak(const std::string &pose, const std::string &scene, int ttlMs,
                                 bool proactive)
{
    // 经状态机 speak()：统一走深夜静默 / 面板抑制 / ≥6s 节流，并统一分配表现序号
    const core::PoseResult result = m_sm.speak(
        pose, scene, ttlMs, core::Event::simple(core::EventType::Tick, nowMs()), proactive);
    if (!result.lineKey.empty()) {
        m_presenter->present(result);
    }
}

void PetController::applyGrowthForZone(core::Zone zone)
{
    core::Interaction type;
    switch (zone) {
    case core::Zone::Head:
    case core::Zone::Body:
        type = core::Interaction::Pat;
        break;
    case core::Zone::Belly:
        type = core::Interaction::Belly;
        break;
    case core::Zone::Tail:
        type = core::Interaction::Tail;
        break;
    case core::Zone::None:
        return; // 未命中任何区域：不是一次交互
    }

    emit interactionOccurred(type, nowMs());
    if (m_growth != nullptr) {
        m_growth->applyInteraction(type);
    }
}

void PetController::applyGrowthForEvent(core::EventType type)
{
    core::Interaction interaction;
    switch (type) {
    case core::EventType::Feed:
        interaction = core::Interaction::Feed;
        break;
    case core::EventType::Tease:
        interaction = core::Interaction::Poke;
        break;
    case core::EventType::Praise:
        interaction = core::Interaction::Praise;
        break;
    case core::EventType::TripleClick:
        interaction = core::Interaction::Triple;
        break;
    default:
        return; // 非交互类事件（LevelUp 等）
    }

    emit interactionOccurred(interaction, nowMs());
    if (m_growth != nullptr) {
        m_growth->applyInteraction(interaction);
    }
}

void PetController::start()
{
    if (running()) {
        return;
    }
    m_sm.reset(nowMs());
    if (m_chat != nullptr) {
        m_chat->reset();
    }
    m_lastHour = -1;
    m_lastClickMs = -1;
    m_clickStreak = 0;

    onClockTick();   // 启动即以当前时段初始化（深夜 → sleep）
    presentCurrent();

    m_tickTimer->start();
    m_clockTimer->start();
}

void PetController::stop()
{
    m_tickTimer->stop();
    m_clockTimer->stop();
}

bool PetController::running() const
{
    return m_tickTimer != nullptr && m_tickTimer->isActive();
}

void PetController::presentCurrent()
{
    m_presenter->present(m_sm.current());
}

void PetController::onTick()
{
    presentCurrent();
    m_presenter->present(m_sm.handle(core::Event::tick(nowMs())));
}

void PetController::onClockTick()
{
    const int hour = QTime::currentTime().hour();
    if (hour == m_lastHour) {
        return;
    }
    m_lastHour = hour;
    m_presenter->present(m_sm.handle(core::Event::clock(hour, nowMs())));

    // 分时问候（CHAT.md §2）：同一时段只问候一次，深夜静默（由 ChatService 判定，
    // 节流/静默再经状态机 speak 统一把关）。2026-10-04 立绘激活 17：问候时显示 greet 立绘。
    if (m_chat != nullptr) {
        const std::string scene = m_chat->greetScene(hour);
        if (!scene.empty()) {
            presentSpeak(core::kGreetPose, scene, 0, true);
        }
    }
}

bool PetController::lateNightWeak() const
{
    return m_sm.lateNightWeak(nowMs());
}

bool PetController::handleClick(core::Zone zone)
{
    // 深夜虚弱（2026-10-04）：角色不再响应鼠标 —— 直接返回 false：
    // 不换立绘 / 不播台词 / **不涨养成** / 不广播 interactionOccurred，
    // 也**不刷新唤醒窗口**（状态机侧再兜一层同样的早退）。
    // View 依据返回值决定是否播放点击反馈动画（PetWindow::mouseReleaseEvent）。
    if (lateNightWeak()) {
        return false;
    }

    const qint64 now = nowMs();

    // 三连击判定（窗口见 PetVisuals.h）
    if (m_lastClickMs >= 0 && (now - m_lastClickMs) <= kMultiClickWindowMs) {
        ++m_clickStreak;
    } else {
        m_clickStreak = 1;
    }
    m_lastClickMs = now;

    m_presenter->present(m_sm.handle(core::Event::click(zone, now)));
    applyGrowthForZone(zone);

    if (m_clickStreak >= kMultiClickCount) {
        m_clickStreak = 0;
        m_presenter->present(
            m_sm.handle(core::Event::simple(core::EventType::TripleClick, now)));
        applyGrowthForEvent(core::EventType::TripleClick);
    }
    return true;
}

bool PetController::handleDragBegin()
{
    if (lateNightWeak()) {
        return false; // 深夜虚弱：不接受拖动（PetWindow 据此不移动窗口、不进拖拽态）
    }
    m_clickStreak = 0;
    m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::DragStart, nowMs())));
    return true;
}

bool PetController::handleDragEnd()
{
    if (lateNightWeak()) {
        return false;
    }
    m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::DragEnd, nowMs())));
    return true;
}

bool PetController::handleMenuAction(core::EventType type)
{
    if (lateNightWeak()) {
        return false; // 深夜虚弱：投喂 / 戳 / 夸夸一律无反应（含不涨养成）
    }
    m_presenter->present(m_sm.handle(core::Event::simple(type, nowMs())));
    applyGrowthForEvent(type);
    return true;
}

void PetController::reportSignIn()
{
    // 只广播交互，不再走 m_growth->applyInteraction：
    // 养成侧 Signin 数值已由 GrowthService::signIn() 落定，重复施加会双倍加心情。
    emit interactionOccurred(core::Interaction::Signin, nowMs());
}

void PetController::handleEvent(core::EventType type)
{
    m_presenter->present(m_sm.handle(core::Event::simple(type, nowMs())));
}

void PetController::presentGame(const QString &pose, const QString &sceneKey, int ttlMs)
{
    // 与 presentSpeak 的差别：无条件把结果交给 Presenter——pose 非空即会切立绘，
    // 即使该场景没有候选台词（Presenter 内 lineKey 为空时只切立绘）。
    const core::PoseResult result = m_sm.speak(
        pose.toStdString(), sceneKey.toStdString(), ttlMs,
        core::Event::simple(core::EventType::Tick, nowMs()), false);
    m_presenter->present(result);
}

void PetController::handleKeywordHit(const QString &keyword)
{
    const std::string id = keyword.toStdString();
    // 立绘映射由 core/ChatRules.h 的 kKeywordPoses 决定（23 项，见 CHAT.md §4）：
    // 如 kyun→meme-kyun、smilepain→meme-smile-pain、deploy→work-deploy。
    // hug / cute / morning 既无立绘、meme.txt 里也没有对应台词 → 无任何可见表现，
    // 故直接跳过（热词录入的下拉里同样不提供这三项，避免「录了却没反应」）。
    const char *pose = core::keywordPose(id);
    if (pose == nullptr) {
        return;
    }
    // 关键词命中源自用户主动输入 → proactive=false，不受深夜静默/节流影响
    presentSpeak(pose, core::keywordSceneKey(id), static_cast<int>(core::kCuriousWindowMs), false);
}

void PetController::handleHotwordInput(const QString &text)
{
    if (m_chat == nullptr) {
        return;
    }
    // 显式录入（全局热键 / 菜单）：**不受 keyword_aware 被动监听开关限制**，
    // 用户主动录入即视为明确意图（理由见 ChatService::matchHotword 注释）
    const std::string id = m_chat->matchHotword(text);
    if (id.empty()) {
        return;
    }
    handleKeywordHit(QString::fromStdString(id));
}

void PetController::handleText(const QString &text)
{
    if (m_chat == nullptr) {
        return;
    }
    const std::string id = m_chat->matchText(text); // keyword_aware 关闭时恒为空
    if (id.empty()) {
        return;
    }
    handleKeywordHit(QString::fromStdString(id));
}

void PetController::setSuppressed(bool suppressed)
{
    m_sm.setSuppressed(suppressed);
}

void PetController::handleWorkState(core::WorkState state, double confidence)
{
    Q_UNUSED(confidence); // 置信度用于 WorkStateService 的阈值判定，表现层不再二次过滤

    // 状态未变：不做任何表现（既避免每 1s 重推，也避免打断正在进行的一次性表现）
    if (state == m_sm.workState()) {
        return;
    }
    m_presenter->present(
        m_sm.handle(core::Event::workStateChanged(static_cast<int>(state), nowMs())));
}

void PetController::handleWorkError()
{
    // 工作侧报错：一次性表现 failure，直接经状态机 → Presenter（不走台词节流）
    m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::WorkError, nowMs())));
}

void PetController::presentRecycleBinReminder(int itemCount)
{
    Q_UNUSED(itemCount); // 数量仅用于日志 / 托盘文案，立绘与台词不区分数量
    // 立绘激活 18：展示 sweep 立绘 + sweep.remind 清理提醒（台词见 assets/lines/lines.txt）
    presentGame(QStringLiteral("sweep"), QStringLiteral("sweep.remind"),
                static_cast<int>(core::kCuriousWindowMs));
}

void PetController::handleGameState(const core::GameCompanionSample &stable,
                                    const core::GameMilestoneSet &milestones, int specialScene)
{
    // EX1.4：与 handleWorkState 同构的「不变则不动」语义——采样为 200ms 高频档，
    // 若每轮都推立绘会与一次性表现/拖拽打架，故仅在**持续态或特殊场景变化**、
    // 或**有里程碑**时才交给状态机（状态机内部再按让位优先级与节流决定是否表现）。
    const bool changed = (stable.mood != m_sm.gameMood())
                         || (specialScene != m_sm.gameSpecialScene());
    if (!changed && !milestones.any()) {
        return;
    }
    m_presenter->present(m_sm.handle(core::Event::gameStateChanged(
        static_cast<int>(stable.mood), specialScene, milestones, nowMs())));
}

} // namespace whalepet
