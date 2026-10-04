#include "viewmodel/AcpSignalService.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace whalepet::viewmodel {

namespace {

// 轮询周期：与核心采样周期同量级（1s 级），不引入额外轮询热点
constexpr int kDefaultIntervalMs = 1000;
// 单轮最多消费的信号条数：外部进程一次灌入大量行时避免阻塞 GUI 线程
constexpr int kMaxBatchPerPoll = 64;

} // namespace

AcpSignalService::AcpSignalService(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kDefaultIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &AcpSignalService::onTimeout);
}

AcpSignalService::~AcpSignalService() = default;

void AcpSignalService::setSource(contextapi::ISignalSource *source)
{
    m_source = source;
}

void AcpSignalService::setIntervalMs(int intervalMs)
{
    const int safe = (intervalMs > 0) ? intervalMs : kDefaultIntervalMs;
    if (m_timer != nullptr) {
        m_timer->setInterval(safe);
    }
}

int AcpSignalService::intervalMs() const
{
    return (m_timer == nullptr) ? kDefaultIntervalMs : m_timer->interval();
}

void AcpSignalService::start()
{
    if (running()) {
        return;
    }
    if (m_source == nullptr || !m_source->available()) {
        // 明确说明原因（不静默）：仍启动定时器，等信号文件出现后即可读到
        qInfo() << "[AcpSignalService] 显式信号源当前不可用（无信号文件），定时器照常运行";
    }
    m_timer->start();
}

void AcpSignalService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

bool AcpSignalService::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

int AcpSignalService::pollNow()
{
    if (m_source == nullptr || !m_source->available()) {
        return 0;
    }

    const qint64 before = m_overrideCount;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    contextapi::CoreSignal signal;
    for (int i = 0; i < kMaxBatchPerPoll && m_source->poll(now, signal); ++i) {
        submitSignal(signal);
    }
    return static_cast<int>(m_overrideCount - before);
}

void AcpSignalService::submitSignal(const contextapi::CoreSignal &signal)
{
    ++m_signalCount;
    emit signalReceived(signal);

    // 工作报错信号：无论是否映射为工作态都要广播（表现层据其显示 failure 立绘）
    if (contextapi::isErrorSignal(signal)) {
        emit errorSignal();
    }

    const contextapi::SignalStateMapping mapping = contextapi::mapSignalToWorkState(signal);
    if (!mapping.mapped) {
        return; // 未识别信号：忽略（不覆盖推断）
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 atMs = (signal.atMs > 0) ? signal.atMs : now;
    ++m_overrideCount;
    emit workStateOverride(mapping.state, mapping.confidence, atMs, mapping.holdMs);
}

void AcpSignalService::onTimeout()
{
    pollNow();
}

} // namespace whalepet::viewmodel
