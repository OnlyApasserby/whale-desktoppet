#pragma once

// 状态机：移植 whale assets/whale-moe-core.js 的纯逻辑部分。
// 零 Qt 依赖；随机源可注入；输入「事件 + 时间戳」，输出 PoseResult。

#include "core/IRandom.h"
#include "core/PetTypes.h"

namespace whalepet::core {

class PetStateMachine {
public:
    explicit PetStateMachine(IRandom *rng = nullptr);

    // 复位到初始上下文态
    void reset(std::int64_t nowMs);

    // 唯一入口：处理事件并返回当前姿态（语义结果）
    PoseResult handle(const Event &event);

    const PoseResult &current() const { return m_current; }
    bool dragging() const { return m_dragging; }

    // 游戏/设置面板打开时抑制主动小剧场与闲聊
    void setSuppressed(bool suppressed) { m_suppressed = suppressed; }
    bool suppressed() const { return m_suppressed; }

    // 台词节流是否允许主动说话（只约束 proactive 台词；用户交互不节流）
    bool canSpeak(std::int64_t nowMs) const;

    static bool isNight(int hour);

private:
    void touchInput(std::int64_t nowMs);
    std::string contextPose(std::int64_t nowMs) const;
    PoseResult fallback(std::int64_t nowMs) const;
    std::string makeLine(const std::string &scene, const Event &event, bool proactive);
    // 组装结果并分配「表现批次序号」（fx/serial 只在真有新表现时自增）
    PoseResult compose(const std::string &pose, const std::string &scene, Fx fx,
                       int ttlMs, const Event &event, bool proactive);
    void applyOneShot(const std::string &pose, const std::string &scene, Fx fx,
                      int ttlMs, const Event &event, bool proactive);

    IRandom *m_rng = nullptr;
    PoseResult m_current;
    std::int64_t m_oneShotUntilMs = 0;   // 0 表示当前无一次性姿态
    std::int64_t m_lastSpeechMs = -1000000;
    std::int64_t m_lastInputMs = 0;
    int m_hour = 12;
    bool m_dragging = false;
    bool m_suppressed = false;

    // 表现批次序号：单调递增，reset() 刻意**不清零**，
    // 避免复位后与 Presenter 记录的旧序号相同而导致「新表现被误判为重复」。
    std::uint32_t m_fxSerial = 0;
    std::uint32_t m_lineSerial = 0;
};

} // namespace whalepet::core
