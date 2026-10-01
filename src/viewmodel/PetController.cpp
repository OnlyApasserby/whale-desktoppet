#include "viewmodel/PetController.h"

#include "common/PetVisuals.h"
#include "core/ChatRules.h"
#include "viewmodel/ChatService.h"
#include "viewmodel/GrowthService.h"
#include "viewmodel/PosePresenter.h"

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

    m_clock.start();

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(static_cast<int>(core::kTickMs));
    connect(m_tickTimer, &QTimer::timeout, this, &PetController::onTick);

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(30'000);
    connect(m_clockTimer, &QTimer::timeout, this, &PetController::onClockTick);
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
    if (m_growth == nullptr || m_chat == nullptr) {
        return;
    }
    const std::string scene = m_chat->moodSceneFor(m_growth->state().mood);
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
    // 节流/静默再经状态机 speak 统一把关）
    if (m_chat != nullptr) {
        const std::string scene = m_chat->greetScene(hour);
        if (!scene.empty()) {
            presentSpeak({}, scene, 0, true);
        }
    }
}

void PetController::handleClick(core::Zone zone)
{
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
}

void PetController::handleDragBegin()
{
    m_clickStreak = 0;
    m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::DragStart, nowMs())));
}

void PetController::handleDragEnd()
{
    m_presenter->present(m_sm.handle(core::Event::simple(core::EventType::DragEnd, nowMs())));
}

void PetController::handleMenuAction(core::EventType type)
{
    m_presenter->present(m_sm.handle(core::Event::simple(type, nowMs())));
    applyGrowthForEvent(type);
}

void PetController::handleEvent(core::EventType type)
{
    m_presenter->present(m_sm.handle(core::Event::simple(type, nowMs())));
}

void PetController::handleKeywordHit(const QString &keyword)
{
    const std::string id = keyword.toStdString();
    // 立绘映射由 core/ChatRules.h 的 kKeywordPoses 决定（21 项，见 CHAT.md §4）：
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

} // namespace whalepet
