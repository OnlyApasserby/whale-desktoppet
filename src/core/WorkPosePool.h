#pragma once

// 工作立绘池（P8）：**含 work 字段的立绘**作为「工作时的池子」。
//
// 需求口径（docs/STATE-MACHINE.md §1.3）：
//   1) 检测到用户在**编程**（Coding / VibeCoding / Debugging）→ 常驻立绘 `running`；
//   2) 其余工作态（Reading / Meeting 等 busy 态）→ 从本池**轮转**取一张；
//   3) 本池与**热词检测**、**ACP** 联动：外部显式指定过的立绘（热词命中的
//      work-* 、ACP 映射的工作态播报）会被记入「最近」，轮转时避开它们，
//      避免「刚出现过的那张马上又出现」。
//
// 轮转（round-robin）而非纯随机：工作期间持续数分钟，纯随机会频繁撞图；
// 轮转保证一轮内不重复，且「最近窗口」保证跨轮也不立刻重复。
//
// 零 Qt 依赖：仅依赖 core::IRandom，可脱界面单测。

#include "core/IRandom.h"
#include "core/WorkState.h"

#include <cstddef>
#include <string>
#include <vector>

namespace whalepet::core {

// 池成员：全部 13 张 work-* 立绘（不含 running —— running 是编程态的常驻立绘）
class WorkPosePool {
public:
    // 池成员表（顺序即轮转顺序）
    static const char *const *poses();
    static std::size_t count();
    static const char *poseAt(std::size_t index); // 越界返回 nullptr
    static bool contains(const char *pose);

    // 当前立绘（尚未取过任何一张时返回池首张，但**不**推进游标）
    const char *current() const;

    // 取下一张：自游标起找到第一张不在「最近窗口」内的；命中后推进游标。
    // 全部成员都在最近窗口内（池小于窗口）时，清空窗口重来。
    const char *next();

    // 外部（热词命中 / ACP 播报）显式用过某张 work 立绘 → 记入最近窗口并对齐游标
    void note(const char *pose);

    std::size_t recentCount() const { return m_recent.size(); }
    std::size_t cursor() const { return m_cursor; }

    void reset(); // 清空游标与最近窗口

private:
    static constexpr std::size_t kRecentKeep = 3; // 最近窗口大小

    void remember(const char *pose);
    bool inRecent(const char *pose) const;

    std::size_t m_cursor = 0;
    const char *m_current = nullptr; // 最近一次 next() / note() 取定的立绘
    std::vector<const char *> m_recent;
};

// 编程族（core::WorkState 中的 Coding / VibeCoding / Debugging）：常驻 `running` 立绘
constexpr const char *kCodingPose = "running";
bool workStateIsCoding(WorkState state);

// 该工作态是否走「工作立绘池」（busy 且**非**编程族）。
// 未知态（Unknown）与 Idle / Browsing / Game / Afk 返回 false ——
// 无感知数据时完全不参与判定（零回归），其余按既有 workStatePose 映射。
bool workStateUsesPool(WorkState state);

// 池的轮转节奏：工作期间每 60s 换一张（避免 200ms tick 抖动，也避免几分钟一张）
inline constexpr long long kWorkPoseDwellMs = 60000;

} // namespace whalepet::core
