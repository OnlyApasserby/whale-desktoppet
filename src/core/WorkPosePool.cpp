#include "core/WorkPosePool.h"

#include <cstring>

namespace whalepet::core {

namespace {

// 13 张 work-* 立绘：全部来自既有 93 张资产（assets/poses/），无新增美术资源。
// 顺序即轮转顺序；语义分组见 docs/POSE-ASSETS.md §3.2。
const char *const kWorkPoses[] = {
    "work-ram",       // 肝代码
    "work-idea",      // 构思
    "work-review",    // 评审 / 看别人的代码
    "work-debug",     // 调不出来的 bug
    "work-deploy",    // 部署 / 上线
    "work-deadline",  // 赶 DDL
    "work-boss",      // 画饼的老板
    "work-slack",     // 摸鱼中
    "work-slack-phone", // 边开会边划水
    "work-meeting",   // 会议中
    "work-sleep",     // 工位上睡过去
    "work-celebrate", // 交付 / 合并成功
    "work-pat",       // 被拍肩膀（协作）
};

constexpr std::size_t kWorkPoseCount = sizeof(kWorkPoses) / sizeof(kWorkPoses[0]);

} // namespace

const char *const *WorkPosePool::poses()
{
    return kWorkPoses;
}

std::size_t WorkPosePool::count()
{
    return kWorkPoseCount;
}

const char *WorkPosePool::poseAt(std::size_t index)
{
    return (index < kWorkPoseCount) ? kWorkPoses[index] : nullptr;
}

bool WorkPosePool::contains(const char *pose)
{
    if (pose == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < kWorkPoseCount; ++i) {
        if (std::strcmp(kWorkPoses[i], pose) == 0) {
            return true;
        }
    }
    return false;
}

bool workStateIsCoding(WorkState state)
{
    switch (state) {
    case WorkState::Coding:
    case WorkState::VibeCoding:
    case WorkState::Debugging:
        return true;
    default:
        return false;
    }
}

bool workStateUsesPool(WorkState state)
{
    return workStateIsBusy(state) && !workStateIsCoding(state);
}

const char *WorkPosePool::current() const
{
    return m_current != nullptr ? m_current : kWorkPoses[0];
}

bool WorkPosePool::inRecent(const char *pose) const
{
    for (const char *recent : m_recent) {
        if (std::strcmp(recent, pose) == 0) {
            return true;
        }
    }
    return false;
}

void WorkPosePool::remember(const char *pose)
{
    // 去重后插入，超出窗口丢最旧
    for (std::size_t i = 0; i < m_recent.size(); ++i) {
        if (std::strcmp(m_recent[i], pose) == 0) {
            m_recent.erase(m_recent.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    m_recent.push_back(pose);
    while (m_recent.size() > kRecentKeep) {
        m_recent.erase(m_recent.begin());
    }
}

const char *WorkPosePool::next()
{
    if (m_recent.size() >= kWorkPoseCount) {
        // 池被「最近窗口」占满（仅当窗口 ≥ 池大小时可能发生）：清空重来
        m_recent.clear();
    }
    for (std::size_t step = 0; step < kWorkPoseCount; ++step) {
        const std::size_t index = (m_cursor + step) % kWorkPoseCount;
        const char *pose = kWorkPoses[index];
        if (inRecent(pose)) {
            continue;
        }
        remember(pose);
        m_cursor = (index + 1) % kWorkPoseCount;
        m_current = pose;
        return pose;
    }
    // 理论不可达（上面已处理池被占满的情况）：仍需推进游标避免死循环
    m_cursor = (m_cursor + 1) % kWorkPoseCount;
    m_current = kWorkPoses[m_cursor];
    return m_current;
}

void WorkPosePool::note(const char *pose)
{
    if (!contains(pose)) {
        return; // 非池成员（如 meme-wakuwaku）不参与联动
    }
    remember(pose);
    m_current = pose;
    // 对齐游标：下一张从该立绘的**后一张**开始，保证「刚出现过的」不会被立刻轮回来
    for (std::size_t i = 0; i < kWorkPoseCount; ++i) {
        if (std::strcmp(kWorkPoses[i], pose) == 0) {
            m_cursor = (i + 1) % kWorkPoseCount;
            return;
        }
    }
}

void WorkPosePool::reset()
{
    m_cursor = 0;
    m_current = nullptr;
    m_recent.clear();
}

} // namespace whalepet::core
