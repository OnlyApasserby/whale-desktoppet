#pragma once

// 工作状态编排（docs/PLUGIN-ARCHITECTURE.md §6.1）：
//   采样 → core::WorkStateRules 判定（含置信度阈值与滞回）→ 变化时才广播。
//
// 职责边界：本类**不碰 Qt 界面、不做表现映射**；表现由 PetController 依事件驱动状态机。
// 判定算法全部在 core（零 Qt、可脱 UI 单测），本类只做编排与去抖。

#include "core/WorkState.h"
#include "core/WorkStateRules.h"

#include <QObject>

#include <cstdint>

namespace whalepet::viewmodel {

class WorkStateService : public QObject {
    Q_OBJECT
public:
    explicit WorkStateService(QObject *parent = nullptr);
    ~WorkStateService() override;

    // 注入判定参数（默认取 core 常量；单测可整套替换以验证边界）
    void setParams(const core::WorkStateParams &params);
    const core::WorkStateParams &params() const;

    // 输入：一份环境采样（由 EnvironmentService::sampleReady 驱动；单测可直接调用）
    void onSample(const core::EnvSample &sample);

    // ---- 显式外部信号（ACP / IDE）作为**覆盖性输入**（docs/CONTEXT-API.md §6）----
    // 在 holdMs 窗口内直接采用该工作态，不被推断结果改写；窗口过期自动回到推断。
    // atMs <= 0 取系统墙钟；holdMs <= 0 取 core::kWorkStateMinDwellMs。
    // state == Unknown 等价于 clearExternalState()。
    void applyExternalState(core::WorkState state, double confidence, qint64 atMs, qint64 holdMs);
    void clearExternalState();
    bool hasExternalState(qint64 nowMs = 0) const;

    const core::WorkStateSample &current() const { return m_current; }

    // 复位到 Unknown（配合 PetStateMachine::reset；下一次采样会重新判定并上报）
    void reset();

    qint64 sampleCount() const { return m_samples; }
    qint64 changeCount() const { return m_changes; }

signals:
    // 仅在状态**变化**时发出（置信度微调不广播，避免每 1s 一次无意义信号）
    void workStateChanged(core::WorkState state, double confidence, qint64 sinceMs);

private:
    core::WorkStateRules m_rules;
    core::WorkStateSample m_current;
    core::WorkStateSample m_override;   // 显式信号覆盖状态（m_hasOverride 为真时有效）
    qint64 m_overrideUntilMs = 0;       // 覆盖窗口结束时刻（墙钟毫秒）
    bool m_hasOverride = false;
    qint64 m_samples = 0;
    qint64 m_changes = 0;
};

} // namespace whalepet::viewmodel
