#include "core/WorkStateRules.h"

namespace whalepet::core {

namespace {

WorkStateSample make(WorkState state, double confidence, std::int64_t nowMs)
{
    WorkStateSample sample;
    sample.state = state;
    sample.confidence = confidence;
    sample.sinceMs = nowMs;
    return sample;
}

} // namespace

WorkStateRules::WorkStateRules(WorkStateParams params)
    : m_params(params)
{
}

WorkStateSample WorkStateRules::classifyCodingFamily(const EnvSample &sample) const
{
    // 1) 调试优先：调试时的输入节奏与 Vibe Coding 极像，必须先排除
    if (windowTitleHasAny(sample.windowTitle, kDebugKeywords, kDebugKeywordCount)) {
        return make(WorkState::Debugging, 0.8, sample.nowMs);
    }

    const bool burst = sample.inputEvents >= m_params.vibeMinEvents;
    const bool jumpy = sample.appSwitches >= m_params.vibeMinSwitches;
    const bool shortDwell = sample.dwellMs <= m_params.vibeMaxDwellMs;
    const bool fewSwitches = sample.appSwitches <= m_params.codingMaxSwitches;

    // 2) Vibe Coding：输入爆发 + 频繁切换 + 停留短（跳动式、试错式开发）
    if (burst && jumpy && shortDwell) {
        return make(WorkState::VibeCoding, 0.72, sample.nowMs);
    }

    // 3) Coding（实时高强度单应用输入）：P7.1 用真实钩子计数回归后的补充判据——
    //    专注打字实测约 5~10 次/秒（10s 窗口 ≥ burst 阈值），且切换少；
    //    原判据要求「同一应用连续停留 ≥ 60s」，会让刚进入编码的前一分钟完全没有状态，
    //    与真实使用不符（用户往往十几秒内就已在连续输入）。
    //    注意：jumpy 为真时不会走到这里（已在上面判为 Vibe Coding）。
    if (burst && fewSwitches) {
        return make(WorkState::Coding, 0.72, sample.nowMs);
    }

    // 4) Coding：同一应用持续停留且切换少（专注）
    const bool sustained = sample.dwellMs >= m_params.codingMinDwellMs;
    // 有输入才谈得上「在写」；空闲的情况在 candidate() 已被 Idle/Afk 拦下
    if (sustained && fewSwitches && sample.inputEvents > 0) {
        return make(WorkState::Coding, 0.78, sample.nowMs);
    }

    // 5) 说不清（刚切到编辑器 / 中等节奏 / 无输入）：低置信度 → 不足以改变现状
    return make(WorkState::Reading, 0.5, sample.nowMs);
}

WorkStateSample WorkStateRules::candidate(const EnvSample &sample) const
{
    // 会话锁定 / 屏保 / 全屏独占：人在不在电脑前是确定的，优先于其它信号。
    // 必须排在 isEmpty() **之前**：锁屏时前台窗口读不到（appId / windowTitle 为空、
    // 也无输入），若先判 isEmpty 会把「明确离开」误降级成「无数据 → Unknown」
    // （P7.1 接入真实采集后暴露，见 docs/pitfalls/ TRAP-P7-006）。
    if (sample.systemPaused) {
        return make(WorkState::Afk, 0.9, sample.nowMs);
    }
    // 无任何数据（未启用感知 / 采样不可用）→ Unknown，不改变任何既有行为
    if (sample.isEmpty()) {
        return make(WorkState::Unknown, 0.0, sample.nowMs);
    }
    if (sample.idleMs >= m_params.afkMs) {
        return make(WorkState::Afk, 0.95, sample.nowMs);
    }
    if (!sample.hasInput && sample.idleMs >= m_params.idleMs) {
        return make(WorkState::Idle, 0.7, sample.nowMs);
    }

    switch (sample.category) {
    case AppCategory::Game:
        return make(WorkState::Game, 0.85, sample.nowMs);
    case AppCategory::Meeting:
        return make(WorkState::Meeting, 0.8, sample.nowMs);
    case AppCategory::Editor:
    case AppCategory::Terminal:
        return classifyCodingFamily(sample);
    case AppCategory::Browser:
    case AppCategory::Office:
        if (sample.inputEvents <= m_params.readingMaxEvents
            && sample.idleMs <= m_params.readingMaxIdleMs) {
            return make(WorkState::Reading, 0.65, sample.nowMs);
        }
        return make(WorkState::Browsing, 0.6, sample.nowMs);
    case AppCategory::Unknown:
        // 有采样但识别不出应用类别（不伪造状态）
        return make(WorkState::Unknown, 0.0, sample.nowMs);
    case AppCategory::Other:
        break;
    }

    // 其它应用：能判断「在动」，但不确定在做什么 —— 低置信度，通常不足以改变现状
    if (sample.inputEvents > m_params.readingMaxEvents) {
        return make(WorkState::Browsing, 0.5, sample.nowMs);
    }
    return make(WorkState::Reading, 0.5, sample.nowMs);
}

WorkStateSample WorkStateRules::evaluate(const EnvSample &sample, const WorkStateSample &prev) const
{
    const WorkStateSample cand = candidate(sample);

    if (cand.state == prev.state) {
        WorkStateSample kept = prev;
        kept.confidence = cand.confidence;
        if (kept.sinceMs <= 0) {
            kept.sinceMs = sample.nowMs;
        }
        return kept;
    }

    // 无数据：立即生效（不等驻留、不看置信度），尽快回到既有行为
    if (cand.state == WorkState::Unknown) {
        return make(WorkState::Unknown, 0.0, sample.nowMs);
    }

    // 置信度不足：保持现状（避免抖动）
    if (cand.confidence < m_params.minConfidence) {
        return prev;
    }

    // 滞回：现状尚未驻留满最短时长则保持（prev 为 Unknown 时不等待，避免开感知后长期无反应）
    if (prev.state != WorkState::Unknown && prev.sinceMs > 0
        && (sample.nowMs - prev.sinceMs) < m_params.minDwellMs) {
        return prev;
    }

    return make(cand.state, cand.confidence, sample.nowMs);
}

} // namespace whalepet::core
