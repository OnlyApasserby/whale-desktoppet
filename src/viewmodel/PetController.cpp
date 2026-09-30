#include "viewmodel/PetController.h"

#include "common/PetVisuals.h"
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
    });
}

void PetController::applyGrowthForZone(core::Zone zone)
{
    if (m_growth == nullptr) {
        return;
    }
    switch (zone) {
    case core::Zone::Head:
    case core::Zone::Body:
        m_growth->applyInteraction(core::Interaction::Pat);
        break;
    case core::Zone::Belly:
        m_growth->applyInteraction(core::Interaction::Belly);
        break;
    case core::Zone::Tail:
        m_growth->applyInteraction(core::Interaction::Tail);
        break;
    case core::Zone::None:
        break;
    }
}

void PetController::applyGrowthForEvent(core::EventType type)
{
    if (m_growth == nullptr) {
        return;
    }
    switch (type) {
    case core::EventType::Feed:
        m_growth->applyInteraction(core::Interaction::Feed);
        break;
    case core::EventType::Tease:
        m_growth->applyInteraction(core::Interaction::Poke);
        break;
    case core::EventType::Praise:
        m_growth->applyInteraction(core::Interaction::Praise);
        break;
    case core::EventType::TripleClick:
        m_growth->applyInteraction(core::Interaction::Triple);
        break;
    default:
        break;
    }
}

void PetController::start()
{
    if (running()) {
        return;
    }
    m_sm.reset(nowMs());
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
    m_presenter->present(
        m_sm.handle(core::Event::keywordHit(keyword.toStdString(), nowMs())));
}

void PetController::setSuppressed(bool suppressed)
{
    m_sm.setSuppressed(suppressed);
}

} // namespace whalepet
