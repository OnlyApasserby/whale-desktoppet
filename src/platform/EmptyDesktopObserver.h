#pragma once

// 空感知实现（P7.0）：**完全不采集**任何信息，恒返回「无数据」快照。
//
// 用途：
//   * 默认装配（感知开关关闭 / 未实现真实采集时），保证行为与 P6 完全一致；
//   * 单测替身：验证「无数据 → WorkState::Unknown → 桌宠行为不变」这条零回归路径。
//
// 真实 Win32 采集（GetForegroundWindow / GetLastInputInfo / 低层钩子计数）见
// docs/ROADMAP-P7-Fin.md P7.1；届时新增 IEnvironmentObserver 实现即可，无需改动本类。

#include "platform/DesktopObserver.h"

namespace whalepet::platform {

class EmptyDesktopObserver : public IEnvironmentObserver {
public:
    EmptyDesktopObserver() = default;
    ~EmptyDesktopObserver() override = default;

    // 恒不可用：宿主据此跳过采样并保持 Unknown
    bool available() const override { return false; }

    // 只填 nowMs，其余保持「无数据」（不伪造 appId / 不猜测类别）
    core::EnvSample sample(std::int64_t nowMs) override;
};

} // namespace whalepet::platform
