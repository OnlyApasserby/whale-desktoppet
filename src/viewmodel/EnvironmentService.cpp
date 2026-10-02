#include "viewmodel/EnvironmentService.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace whalepet::viewmodel {

EnvironmentService::EnvironmentService(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(static_cast<int>(core::kWorkSampleIntervalMs));
    connect(m_timer, &QTimer::timeout, this, &EnvironmentService::onTimeout);
}

EnvironmentService::~EnvironmentService() = default;

void EnvironmentService::setObserver(platform::IEnvironmentObserver *observer)
{
    if (observer == m_observer) {
        return;
    }
    // 运行期换观察者：先把旧实现的生命周期收尾，避免系统资源（钩子）泄漏
    const bool wasRunning = running();
    if (wasRunning && m_observer != nullptr) {
        m_observer->setObserving(false);
    }
    m_observer = observer;
    if (wasRunning && m_observer != nullptr) {
        m_observer->setObserving(true);
    }
}

void EnvironmentService::setIntervalMs(int intervalMs)
{
    const int safe = (intervalMs > 0) ? intervalMs : static_cast<int>(core::kWorkSampleIntervalMs);
    if (m_timer != nullptr) {
        m_timer->setInterval(safe);
    }
}

int EnvironmentService::intervalMs() const
{
    return (m_timer == nullptr) ? static_cast<int>(core::kWorkSampleIntervalMs) : m_timer->interval();
}

bool EnvironmentService::available() const
{
    return m_observer != nullptr && m_observer->available();
}

core::EnvSample EnvironmentService::sampleNow(qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : QDateTime::currentMSecsSinceEpoch();
    core::EnvSample sample;
    if (m_observer != nullptr) {
        sample = m_observer->sample(now);
    } else {
        // 无观察者 = 无数据：只带时刻，不伪造前台应用（判定侧会得到 Unknown）
        sample.nowMs = now;
    }
    m_last = sample;
    ++m_count;
    return sample;
}

void EnvironmentService::onTimeout()
{
    emit sampleReady(sampleNow());
}

void EnvironmentService::start()
{
    if (running()) {
        return;
    }
    if (!available()) {
        // 明确说明原因（不静默）：仍按周期采样，但只会产出「无数据」快照
        qInfo() << "[EnvironmentService] 感知不可用（空实现或未接入），采样不会产出真实数据";
    }
    // 先激活观察者（Win32 实现会在此安装低层输入钩子），再开始采样，
    // 保证第一次采样就带真实数据、且**关闭期间不存在任何系统钩子**。
    if (m_observer != nullptr) {
        m_observer->setObserving(true);
    }
    m_timer->start();
}

void EnvironmentService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    if (m_observer != nullptr) {
        m_observer->setObserving(false); // 释放系统资源（卸载低层钩子）并清空采样记忆
    }
}

bool EnvironmentService::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

} // namespace whalepet::viewmodel
