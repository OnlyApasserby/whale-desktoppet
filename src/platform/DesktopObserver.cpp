#include "platform/DesktopObserver.h"

namespace whalepet::platform {

bool CompositeDesktopObserver::available() const
{
    if (m_foreground != nullptr && m_foreground->available()) {
        return true;
    }
    if (m_activity != nullptr && m_activity->available()) {
        return true;
    }
    if (m_systemStatus != nullptr && m_systemStatus->available()) {
        return true;
    }
    return false;
}

core::EnvSample CompositeDesktopObserver::sample(std::int64_t nowMs)
{
    core::EnvSample out;
    out.nowMs = nowMs;

    // 1) 子采样器：各自只填自己负责的字段（失败即该维度保持「无数据」，不伪造）
    if (m_foreground != nullptr && m_foreground->available()) {
        if (!m_foreground->sampleForeground(out)) {
            out.appId.clear();
            out.windowTitle.clear();
        }
    }

    bool instantInput = false;
    int eventsDelta = 0;
    if (m_activity != nullptr && m_activity->available()) {
        core::EnvSample activity;
        activity.nowMs = nowMs;
        if (m_activity->sampleActivity(activity)) {
            instantInput = activity.hasInput;
            eventsDelta = activity.inputEvents;
            out.idleMs = activity.idleMs;
        }
    }

    if (m_systemStatus != nullptr && m_systemStatus->available()) {
        core::EnvSample status;
        status.nowMs = nowMs;
        if (m_systemStatus->sampleSystemStatus(status)) {
            out.systemPaused = status.systemPaused;
        }
    }

    // 2) 滚动窗口：切换数与事件数按 kWorkWindowMs 累计（判定规则依赖窗口语义）
    if (m_windowStartMs <= 0 || (nowMs - m_windowStartMs) >= core::kWorkWindowMs) {
        m_windowStartMs = nowMs;
        m_windowEvents = 0;
        m_windowSwitches = 0;
    }
    if (eventsDelta > 0) {
        m_windowEvents += eventsDelta;
    } else if (instantInput && m_windowEvents == 0) {
        // 采样器只报「有输入」而不报计数时，窗口至少记 1 次，避免活跃被误判为空闲
        m_windowEvents = 1;
    }

    // 3) 前台应用切换与同应用停留
    std::int64_t dwell = 0;
    if (!out.appId.empty()) {
        const bool changed = m_hasLast && !m_last.appId.empty() && out.appId != m_last.appId;
        if (changed) {
            ++m_windowSwitches;
        }
        if (changed || !m_hasLast || m_appSinceMs <= 0) {
            m_appSinceMs = nowMs;
        }
        dwell = nowMs - m_appSinceMs;
        if (dwell < 0) {
            dwell = 0; // 时钟回拨等异常输入：不产出负值
        }
    } else {
        m_appSinceMs = 0;
    }

    out.inputEvents = m_windowEvents;
    out.appSwitches = m_windowSwitches;
    out.dwellMs = dwell;
    out.hasInput = instantInput || m_windowEvents > 0;
    out.category = core::classifyApp(out.appId, out.windowTitle);

    m_last = out;
    m_hasLast = true;
    return out;
}

void CompositeDesktopObserver::setObserving(bool active)
{
    if (m_foreground != nullptr) {
        m_foreground->setObserving(active);
    }
    if (m_activity != nullptr) {
        m_activity->setObserving(active);
    }
    if (m_systemStatus != nullptr) {
        m_systemStatus->setObserving(active);
    }
    if (!active) {
        // 停止观察：清空跨采样记忆，下次开始时不把「关闭期间」算作切换/停留
        m_hasLast = false;
        m_windowStartMs = 0;
        m_windowEvents = 0;
        m_windowSwitches = 0;
        m_appSinceMs = 0;
        m_last = core::EnvSample{};
    }
}

} // namespace whalepet::platform
