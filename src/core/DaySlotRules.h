#pragma once

// 时段立绘规则（P8）：**常驻立绘**按本地时段切换（docs/STATE-MACHINE.md §1.2）。
//
// 与 P7 的关系：此前时段态只有「深夜 → sleep」一档（PetStateMachine::isNight），
// 其余时段沿用静息链。本文件把时段切成三段，并给出每段的空闲常驻立绘：
//
//   | 时段        | 小时          | 空闲常驻立绘       |
//   |-------------|---------------|--------------------|
//   | 日间 Day    | 07:00–17:59   | idle-cute          |
//   | 傍晚 Evening | 18:00–22:59   | night              |
//   | 深夜 LateNight | 23:00–06:59 | daily-pajama       |
//
// 深夜**没有**「被交互唤醒」态（2026-10-04 重构移除）：深夜常驻立绘恒为 daily-pajama，
// 交互不再改换常驻立绘（点击仅累计，达阈值后转「虚弱」一次性立绘，见 IdleRules.h）。
//
// 零 Qt 依赖：仅整数与字符串字面量，可脱界面单测。

#include <cstdint>

namespace whalepet::core {

enum class DaySlot {
    Day,       // 07:00–17:59
    Evening,   // 18:00–22:59
    LateNight, // 23:00–06:59
};

// 时段划分。非法小时（<0 或 >23）保守判为深夜（不主动换装成日间立绘）。
inline DaySlot daySlotOf(int hour)
{
    if (hour < 0 || hour > 23) {
        return DaySlot::LateNight;
    }
    if (hour >= 7 && hour < 18) {
        return DaySlot::Day;
    }
    if (hour >= 18 && hour < 23) {
        return DaySlot::Evening;
    }
    return DaySlot::LateNight;
}

// 该时段的**空闲常驻立绘**。
// 注意：日间返回 idle-cute 只是一致性取值 —— 日间不接管状态机判定，
// 仍由静息链（waiting / 节日换装 / 挂机态）决定，见 PetStateMachine::contextPose。
inline const char *daySlotPoseOf(DaySlot slot)
{
    switch (slot) {
    case DaySlot::Day:
        return "idle-cute";
    case DaySlot::Evening:
        return "night";
    case DaySlot::LateNight:
        return "daily-pajama";
    }
    return "idle-cute";
}

} // namespace whalepet::core
