#include "viewmodel/FlickerService.h"

#include "core/RandomFlickerRules.h"

#include <QDebug>
#include <QTimer>

namespace whalepet::viewmodel {

FlickerService::FlickerService(QObject *parent)
    : QObject(parent)
{
}

FlickerService::~FlickerService() = default;

void FlickerService::setRandom(core::IRandom *rng)
{
    m_rng = (rng != nullptr) ? rng : &m_systemRng;
}

void FlickerService::setIntervalRange(int minMs, int maxMs)
{
    if (minMs <= 0 || maxMs <= 0 || minMs > maxMs) {
        return; // 非法输入忽略，保持原区间
    }
    m_minMs = minMs;
    m_maxMs = maxMs;
}

void FlickerService::setCanFlicker(std::function<bool()> hook)
{
    m_canFlicker = std::move(hook);
}

void FlickerService::start()
{
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setSingleShot(true); // 每轮结束后重新随机调度
        connect(m_timer, &QTimer::timeout, this, [this]() {
            if (!m_canFlicker || m_canFlicker()) {
                emit flickerRequested();
            } else {
                // 门槛不满足：本轮静默跳过（不打扰），直接进入下一轮随机间隔
                qInfo() << "[FlickerService] 本轮闪现被门槛挡下，等待下一轮";
            }
            scheduleNext();
        });
    }
    scheduleNext();
}

void FlickerService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    m_lastDelayMs = 0;
}

bool FlickerService::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

void FlickerService::flickerNow()
{
    emit flickerRequested();
}

void FlickerService::scheduleNext()
{
    if (m_timer == nullptr) {
        return;
    }
    m_lastDelayMs = core::nextFlickerDelayMs(m_rng, m_minMs, m_maxMs);
    m_timer->start(m_lastDelayMs);
}

} // namespace whalepet::viewmodel
