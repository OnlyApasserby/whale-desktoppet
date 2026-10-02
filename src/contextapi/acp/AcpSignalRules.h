#pragma once

// 显式信号 → 工作态的映射规则（docs/CONTEXT-API.md §6、docs/PLUGIN-ARCHITECTURE.md §6.1）。
//
// 定位：platform 层（Win32 采集）是「**推断**」，ACP 显式信号是「**显式告知**」。
// 显式信号优先于推断 —— 由 viewmodel::WorkStateService 的覆盖窗口落地：
//   收到信号 → 在该信号的 holdMs 窗口内直接采用其工作态，不被推断结果覆盖；
//   窗口过期 → 自动回到推断（因此 IDE 关掉后桌宠不会一直停在旧状态）。
//
// 本文件是**纯函数 + 常量**（零副作用、零 Qt Widgets），可脱 UI 单测。

#include "contextapi/ISignalSource.h"
#include "core/WorkState.h"

#include <cstdint>

namespace whalepet::contextapi {

// 显式信号的默认覆盖时长（毫秒）：窗口内保持该工作态，过期回到推断。
inline constexpr qint64 kAcpSignalHoldMs = 30000;

struct SignalStateMapping {
    bool mapped = false;                    // false = 该信号不映射为工作态（忽略，不覆盖推断）
    core::WorkState state = core::WorkState::Unknown;
    double confidence = 0.0;                // 0..1
    qint64 holdMs = kAcpSignalHoldMs;       // 覆盖保持时长（> 0）
};

// 把一条外部显式信号映射为工作态。
// 优先级：
//   1) payload.state 显式指定（core::workStateId 可识别的字符串）——最强，允许 IDE 直接定态；
//   2) 否则按 kind 的标准映射表（见 .cpp）；
//   3) 都不命中 → mapped = false（**不猜、不覆盖**）。
// payload 可附带 confidence / holdMs 覆盖默认值。
SignalStateMapping mapSignalToWorkState(const CoreSignal &signal);

} // namespace whalepet::contextapi
