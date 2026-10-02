#include "viewmodel/WorkStateService.h"

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
}

void WorkStateService::onSample(const core::EnvSample &sample)
{
    ++m_samples;

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
