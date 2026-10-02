#include "viewmodel/WorkStateService.h"

#include <QDateTime>
#include <QDebug>

namespace whalepet::viewmodel {

WorkStateService::WorkStateService(QObject *parent)
    : QObject(parent)
    , m_rules()
{
}

WorkStateService::~WorkStateService() = default;

void WorkStateService::setParams(const core::WorkStateParams &params)
{
    m_rules = core::WorkStateRules(params);
}

const core::WorkStateParams &WorkStateService::params() const
{
    return m_rules.params();
}

void WorkStateService::reset()
{
    m_current = core::WorkStateSample{};
    m_override = core::WorkStateSample{};
    m_overrideUntilMs = 0;
    m_hasOverride = false;
}

void WorkStateService::applyExternalState(core::WorkState state, double confidence, qint64 atMs,
                                          qint64 holdMs)
{
    if (state == core::WorkState::Unknown) {
        clearExternalState();
        return;
    }

    const qint64 now = (atMs > 0) ? atMs : QDateTime::currentMSecsSinceEpoch();
    m_override.state = state;
    m_override.confidence = confidence;
    m_override.sinceMs = now;
    m_overrideUntilMs = now + ((holdMs > 0) ? holdMs : core::kWorkStateMinDwellMs);
    m_hasOverride = true;

    if (m_current.state == state && m_current.confidence == confidence
        && m_current.sinceMs == m_override.sinceMs) {
        return; // 与当前一致：不重复广播
    }

    m_current = m_override;
    ++m_changes;
    qInfo() << "[WorkStateService] 显式信号覆盖工作状态:" << core::workStateId(state)
            << "置信度 =" << confidence;
    emit workStateChanged(m_current.state, m_current.confidence, m_current.sinceMs);
}

void WorkStateService::clearExternalState()
{
    if (!m_hasOverride) {
        return;
    }
    m_hasOverride = false;
    qInfo() << "[WorkStateService] 显式信号覆盖已清除（下一次采样回到推断）";
    // 不在此处改写 m_current：避免在无新采样时凭空切换状态造成抖动；
    // 下一次 onSample() 会按推断重新判定。
}

bool WorkStateService::hasExternalState(qint64 nowMs) const
{
    if (!m_hasOverride) {
        return false;
    }
    const qint64 now = (nowMs > 0) ? nowMs : QDateTime::currentMSecsSinceEpoch();
    return now < m_overrideUntilMs;
}

void WorkStateService::onSample(const core::EnvSample &sample)
{
    ++m_samples;

    // 显式信号覆盖窗口：窗口内保持外部状态，不做推断（显式告知优先于推断）
    if (m_hasOverride) {
        const qint64 now = (sample.nowMs > 0) ? sample.nowMs : QDateTime::currentMSecsSinceEpoch();
        if (now < m_overrideUntilMs) {
            return;
        }
        m_hasOverride = false; // 窗口过期：回到推断
        qInfo() << "[WorkStateService] 显式信号覆盖窗口结束，回到推断";
    }

    const core::WorkStateSample prev = m_current;
    const core::WorkStateSample next = m_rules.evaluate(sample, prev);

    if (next.state == prev.state && next.confidence == prev.confidence
        && next.sinceMs == prev.sinceMs) {
        return; // 完全无变化：不广播
    }

    const bool stateChanged = (next.state != prev.state);
    m_current = next;
    if (stateChanged) {
        ++m_changes;
        qInfo() << "[WorkStateService] 工作状态变化:" << core::workStateId(next.state)
                << "置信度 =" << next.confidence << "应用 =" << sample.appId.c_str();
        emit workStateChanged(m_current.state, m_current.confidence, m_current.sinceMs);
    }
}

} // namespace whalepet::viewmodel
