#pragma once

// P10「桌宠互动（菜单选项）」通道（docs/ARCHITECTURE.md 附录 D）：
//   - 语义：由**菜单选项触发**的桌宠互动（如「工作台…」开启的工作会话）所产生的**常驻表现**，
//     与「感知用户工作」（core::WorkState）**完全解耦** —— 后者是只读观测，本通道是自驱互动；
//   - 优先级（docs/STATE-MACHINE.md §1.2）：一次性事件 > 深夜 > **菜单互动** > 工作态
//     > 睡眠循环 / 时段态 / 满值 / 挂机 / 游戏陪玩 / 静息；
//   - None（默认）时状态机完全跳过本通道 → 行为与 P10 之前一致（零回归）。
//
// 零 Qt 依赖：仅常量表与纯函数，可脱界面单测。

namespace whalepet::core {

enum class MenuInteraction {
    None, // 无菜单互动（默认）
    Work, // 工作会话（「工作台…」：桌宠自己产出 Python 脚本，见 viewmodel::WorkService）
};

// 稳定字符串 id（日志 / 单测 / Context API 观测）；恒非空
inline const char *menuInteractionId(MenuInteraction kind)
{
    switch (kind) {
    case MenuInteraction::Work:
        return "menu.work";
    case MenuInteraction::None:
        break;
    }
    return "menu.none";
}

// 该互动是否复用 **work-* 工作立绘池**（与感知工作态共享同一份池与 60s 轮转节奏，
// 见 core/WorkPosePool.h）；None 返回 false
inline bool menuInteractionUsesWorkPool(MenuInteraction kind)
{
    return kind == MenuInteraction::Work;
}

// 该互动的台词场景 key（`menu.*`，语料见 assets/lines/lines.txt）；
// None 返回 nullptr（不播报）
inline const char *menuInteractionScene(MenuInteraction kind)
{
    return (kind == MenuInteraction::Work) ? "menu.work" : nullptr;
}

} // namespace whalepet::core
