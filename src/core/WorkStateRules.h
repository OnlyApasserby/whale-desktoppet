#pragma once

// 工作状态判定规则：**零 Qt 依赖、无副作用、可注入参数**，可脱 UI 单测。
//
// 归属：docs/PLUGIN-ARCHITECTURE.md §3.1 / §6.1、docs/ROADMAP-P7.md P7.0。
//
// 判定输入是「一次采样 + 上一次结果」——采样自带 nowMs / dwellMs / appSwitches，
// 因此本类**不持有任何历史状态**，同样的输入永远得到同样的输出（便于回归与复现）。
//
// Coding 与 Vibe Coding 的可操作判据（全部来自平台无关的采样量）：
//   * Coding      ：编辑器/终端族 + **采样窗口内输入高强度且切换少**（P7.1 起，
//                    由真实钩子计数回归得到：专注打字实测约 5~10 次/秒），
//                    或同一应用已连续停留 ≥ codingMinDwellMs + 切换少
//                    → 「坐下来精耕细作地写」；
//   * VibeCoding  ：编辑器/终端族 + 采样窗口内输入爆发（≥ vibeMinEvents）
//                    + 频繁切换（≥ vibeMinSwitches）+ 停留短（≤ vibeMaxDwellMs）
//                    → 「快速由 AI 辅助生成、来回试错的跳动式开发」；
//   * Debugging   ：窗口标题含调试语义（debug / 断点 / gdb / 运行测试 …），优先级最高
//                    （调试时的输入节奏与 Vibe 极像，必须先排除）。
//   介于 Coding 与 VibeCoding 之间的「说不清」区间给出低置信度（Reading），
//   低于 kWorkStateMinConfidence 时**不改变现状**——这本身就是一种抖动抑制。

#include "core/WorkState.h"

namespace whalepet::core {

// 可注入参数（默认值即 WorkState.h 的判定常量；测试可整套替换以验证边界）
struct WorkStateParams {
    std::int64_t minDwellMs = kWorkStateMinDwellMs;
    double minConfidence = kWorkStateMinConfidence;
    std::int64_t idleMs = kWorkIdleMs;
    std::int64_t afkMs = kWorkAfkMs;
    std::int64_t readingMaxIdleMs = kWorkReadingMaxIdleMs;
    int readingMaxEvents = kWorkReadingMaxEvents;
    std::int64_t codingMinDwellMs = kWorkCodingMinDwellMs;
    int codingMaxSwitches = kWorkCodingMaxSwitches;
    int vibeMinEvents = kVibeBurstMinEvents;
    int vibeMinSwitches = kVibeMinSwitches;
    std::int64_t vibeMaxDwellMs = kVibeMaxDwellMs;
};

class WorkStateRules {
public:
    explicit WorkStateRules(WorkStateParams params = WorkStateParams());

    // 归一化：把一次采样归纳为「候选状态 + 置信度」（**不含滞回与阈值过滤**，便于逐条单测判据）
    WorkStateSample candidate(const EnvSample &sample) const;

    // 稳定判定：候选 + 上一次结果 → 实际生效的状态（含置信度阈值与最短驻留滞回）。
    //   * Unknown 立即生效（无数据时必须尽快退回既有行为，不等驻留）；
    //   * 置信度 < minConfidence → 保持 prev（不抖动）；
    //   * 与 prev 不同且 prev 驻留未满 minDwellMs → 保持 prev（滞回）；
    //   * prev 为 Unknown 时首次判定无需等待（避免开感知后长时间不动）。
    WorkStateSample evaluate(const EnvSample &sample, const WorkStateSample &prev) const;

    const WorkStateParams &params() const { return m_params; }

private:
    // 编辑器 / 终端族细分：Debugging > VibeCoding > Coding > （低置信度）Reading
    WorkStateSample classifyCodingFamily(const EnvSample &sample) const;

    WorkStateParams m_params;
};

} // namespace whalepet::core
