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
// 深夜的「被交互唤醒」：用户点击（或拖拽 / 关键词命中 / 外部播报等任何 touchInput 路径）
// 之后 kLateNightAwakeMs 内改显 night 立绘；无操作到期后自动切回 daily-pajama。
// 唤醒窗口由状态机维护（PetStateMachine::touchInput），本文件只给常量与纯判定。
//
// 零 Qt 依赖：仅整数与字符串字面量，可脱界面单测。

#include <cstdint>

namespace whalepet::core {

// 深夜被交互唤醒后保持「醒着」立绘的时长（1 分钟）
inline constexpr std::int64_t kLateNightAwakeMs = 60000;

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

// 深夜「醒着」时的常驻立绘（唤醒窗口内覆盖 daily-pajama）
inline constexpr const char *kLateNightAwakePose = "night";

} // namespace whalepet::core
