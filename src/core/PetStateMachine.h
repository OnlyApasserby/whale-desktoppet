#pragma once

// 状态机：移植 whale assets/whale-moe-core.js 的纯逻辑部分。
// 零 Qt 依赖；随机源可注入；输入「事件 + 时间戳」，输出 PoseResult。

#include "core/IRandom.h"
#include "core/PetTypes.h"
#include "core/WorkState.h"

namespace whalepet::core {

class PetStateMachine {
public:
    explicit PetStateMachine(IRandom *rng = nullptr);

    // 复位到初始上下文态
    void reset(std::int64_t nowMs);

    // 唯一入口：处理事件并返回当前姿态（语义结果）
    PoseResult handle(const Event &event);

    // 外部编排层（ChatService）专用入口：产出一次「指定姿态 + 场景 key」的表现。
    //   - pose 为空 → 不改立绘，沿用当前上下文姿态（分时问候 / 心情 / 羁绊播报）；
    //   - pose 非空 → 按一次性姿态处理并保持 ttlMs（关键词表情感知）；
    //   - proactive=true 走与内部一致的深夜静默 / 面板抑制 / ≥6s 节流规则；
    //     用户主动交互（关键词命中）传 false，永不节流。
    // 序号由状态机统一分配，避免与内部表现批次冲突（见 PoseResult 的序号注释）。
    PoseResult speak(const std::string &pose, const std::string &scene, int ttlMs,
                     const Event &event, bool proactive = true);

    const PoseResult &current() const { return m_current; }
    bool dragging() const { return m_dragging; }

    // 游戏/设置面板打开时抑制主动小剧场与闲聊
    void setSuppressed(bool suppressed) { m_suppressed = suppressed; }
    bool suppressed() const { return m_suppressed; }

    // 台词节流是否允许主动说话（只约束 proactive 台词；用户交互不节流）
    bool canSpeak(std::int64_t nowMs) const;

    // P6 设置项 night_quiet：深夜（23:00–05:59）是否静默。
    // 关闭后深夜也会主动发言；只影响 proactive 台词，用户交互始终豁免。
    // 属外部配置，reset() 不会把它复位。
    void setNightQuiet(bool quiet) { m_nightQuiet = quiet; }
    bool nightQuiet() const { return m_nightQuiet; }

    // P7 工作状态（docs/PLUGIN-ARCHITECTURE.md §6.2）：
    //   - 由 EventType::WorkStateChanged 驱动，本类不自行采集；
    //   - Unknown（无感知数据）时**完全跳过**工作态分支，行为与 P6 一致；
    //   - 优先级：一次性事件 > 工作态 > 时段态（夜/睡）> 挂机态 > 默认；
    //   - 专注态（Coding/VibeCoding/Debugging/Meeting）下主动台词静默，
    //     唯一豁免是 `work.*` 场景自身的状态播报。
    WorkState workState() const { return m_workState; }
    // 复位工作态到 Unknown（reset() 会调用；供「感知被关闭」时显式降级）
    void clearWorkState() { m_workState = WorkState::Unknown; }

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
    bool m_nightQuiet = true;   // P6 设置项 night_quiet（默认开）
    // P7 工作态：默认 Unknown（无感知数据），此时不参与姿态判定（零回归）
    WorkState m_workState = WorkState::Unknown;

    // 表现批次序号：单调递增，reset() 刻意**不清零**，
    // 避免复位后与 Presenter 记录的旧序号相同而导致「新表现被误判为重复」。
    std::uint32_t m_fxSerial = 0;
    std::uint32_t m_lineSerial = 0;
};

} // namespace whalepet::core
