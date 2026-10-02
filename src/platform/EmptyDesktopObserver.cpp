#include "platform/EmptyDesktopObserver.h"

namespace whalepet::platform {

core::EnvSample EmptyDesktopObserver::sample(std::int64_t nowMs)
{
    core::EnvSample out;
    out.nowMs = nowMs;
    // 其余字段保持默认（空 / 0 / Unknown）——「无数据」在 core::WorkStateRules 中
    // 会被判定为 WorkState::Unknown，从而不改变任何既有桌宠行为。
    return out;
}

} // namespace whalepet::platform
