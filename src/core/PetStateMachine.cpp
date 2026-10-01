#include "core/PetStateMachine.h"

#include "core/PoseCatalog.h"

namespace whalepet::core {

namespace {
// 逗弄小剧场姿态保持时长
constexpr int kTeaseTtlMs = 1500;
// 长时间无输入的上下文姿态阈值以外的兜底
constexpr const char *kDefaultPose = "idle-cute";
} // namespace

PetStateMachine::PetStateMachine(IRandom *rng)
    : m_rng(rng)
{
    m_current.pose = kDefaultPose;
}

bool PetStateMachine::isNight(int hour)
{
    return hour >= kNightStartHour || hour < kNightEndHour;
}

void PetStateMachine::reset(std::int64_t nowMs)
{
    m_oneShotUntilMs = 0;
    m_lastSpeechMs = -1000000;
    m_lastInputMs = nowMs;
    m_dragging = false;
    m_suppressed = false;
    m_current = PoseResult{kDefaultPose, "", Fx::None, 0};
}

bool PetStateMachine::canSpeak(std::int64_t nowMs) const
{
    return (nowMs - m_lastSpeechMs) >= kSpeechGapMs;
}

void PetStateMachine::touchInput(std::int64_t nowMs)
{
    m_lastInputMs = nowMs;
}

// 优先级：时段态（夜/睡）> 挂机态（afk/thinking/waiting）> 默认（idle）
std::string PetStateMachine::contextPose(std::int64_t nowMs) const
{
    if (isNight(m_hour)) {
        return "sleep";
    }
    const std::int64_t idle = nowMs - m_lastInputMs;
    if (idle >= kAfkMs) {
        return "afk";
    }
    if (idle >= kThinkingMs) {
        return "thinking";
    }
    if (idle >= kWaitingMs) {
        return "waiting";
    }
    return kDefaultPose;
}

PoseResult PetStateMachine::fallback(std::int64_t nowMs) const
{
    const std::string pose = contextPose(nowMs);
    return PoseResult{pose, "", Fx::None, 0};
}

std::string PetStateMachine::makeLine(const std::string &scene, const Event &event, bool proactive)
{
    if (scene.empty()) {
        return {};
    }
    // 用户交互（proactive=false）永不节流：一次操作必须有一次回应。
    // 「连点不刷屏」由 Presenter 的「序号去重 + 流式打断（最后一次为准）」保证，
    // 而不是靠吞掉用户的后续操作（见 docs/ROADMAP-P2.md 增补 §B）。
    if (!proactive) {
        return scene;
    }
    // 深夜静默（可经设置项 night_quiet 关闭）：仅抑制「主动」发言，用户主动交互仍可回应
    if (m_nightQuiet && isNight(m_hour)) {
        return {};
    }
    // 面板打开时不主动打断
    if (m_suppressed) {
        return {};
    }
    // 台词节流：只约束主动说话，且只有主动说话消耗额度
    if (!canSpeak(event.nowMs)) {
        return {};
    }
    m_lastSpeechMs = event.nowMs;
    return scene;
}

PoseResult PetStateMachine::compose(const std::string &pose, const std::string &scene, Fx fx,
                                    int ttlMs, const Event &event, bool proactive)
{
    // 立绘不存在时退化为通用好奇表情，避免出现空白
    const std::string safePose = poseExists(pose.c_str()) ? pose : std::string("curious");

    PoseResult result;
    result.pose = safePose;
    result.lineKey = makeLine(scene, event, proactive);
    result.fx = fx;
    result.ttlMs = ttlMs;
    // 序号只在「真有新表现」时自增：Presenter 据此去重，
    // 使「每 tick 重放缓存态」不再重复播特效与台词。
    if (fx != Fx::None) {
        result.fxSerial = ++m_fxSerial;
    }
    if (!result.lineKey.empty()) {
        result.lineSerial = ++m_lineSerial;
    }
    return result;
}

void PetStateMachine::applyOneShot(const std::string &pose, const std::string &scene, Fx fx,
                                   int ttlMs, const Event &event, bool proactive)
{
    m_current = compose(pose, scene, fx, ttlMs, event, proactive);
    m_oneShotUntilMs = (ttlMs > 0) ? (event.nowMs + ttlMs) : 0;
}

PoseResult PetStateMachine::handle(const Event &event)
{
    switch (event.type) {
    case EventType::Tick: {
        // 一次性姿态到期 → 回落到上下文态
        if (m_oneShotUntilMs > 0 && event.nowMs >= m_oneShotUntilMs) {
            m_oneShotUntilMs = 0;
            m_current = fallback(event.nowMs);
        }

        // 无一次性姿态时按上下文刷新（时段/挂机变化会在此反映）
        if (m_oneShotUntilMs == 0 && !m_dragging) {
            m_current = fallback(event.nowMs);

            // 待机小剧场：低概率逗弄
            if (!m_suppressed && m_rng != nullptr && m_rng->next01() < kTeaseChance) {
                m_current = compose("teasing", "idle.tease", Fx::None, kTeaseTtlMs, event, true);
                m_oneShotUntilMs = event.nowMs + kTeaseTtlMs;
            }
        }
        return m_current;
    }

    case EventType::Clock: {
        if (event.hour >= 0) {
            m_hour = event.hour;
        }
        if (m_oneShotUntilMs == 0 && !m_dragging) {
            m_current = fallback(event.nowMs);
        }
        return m_current;
    }

    case EventType::IdleTimeout: {
        touchInput(event.nowMs - kAfkMs); // 直接置为已挂机
        if (m_oneShotUntilMs == 0) {
            m_current = fallback(event.nowMs);
        }
        return m_current;
    }

    case EventType::DragStart: {
        touchInput(event.nowMs);
        m_dragging = true;
        m_oneShotUntilMs = 0;
        m_current = compose("pick-up", "drag.pickup", Fx::None, 0, event, false);
        return m_current;
    }

    case EventType::DragEnd: {
        touchInput(event.nowMs);
        m_dragging = false;
        m_oneShotUntilMs = 0;
        m_current = fallback(event.nowMs);
        return m_current;
    }

    case EventType::Click: {
        touchInput(event.nowMs);
        std::string pose = "curious";
        std::string scene = "click.body";
        switch (event.zone) {
        case Zone::Head:
            pose = "react-head";
            scene = "click.head";
            break;
        case Zone::Belly:
            pose = "react-belly";
            scene = "click.belly";
            break;
        case Zone::Tail:
            pose = "react-tail";
            scene = "click.tail";
            break;
        case Zone::Body:
        case Zone::None:
        default:
            break;
        }
        applyOneShot(pose, scene, Fx::None, static_cast<int>(kCuriousWindowMs), event, false);
        return m_current;
    }

    case EventType::TripleClick:
        touchInput(event.nowMs);
        applyOneShot("star", "click.triple", Fx::Particle,
                     static_cast<int>(kSuccessWindowMs), event, false);
        return m_current;

    case EventType::Feed:
        touchInput(event.nowMs);
        applyOneShot("eat", "menu.feed", Fx::None,
                     static_cast<int>(kSuccessWindowMs), event, false);
        return m_current;

    case EventType::Tease:
        touchInput(event.nowMs);
        applyOneShot("angry", "menu.tease", Fx::None,
                     static_cast<int>(kSuccessWindowMs), event, false);
        return m_current;

    case EventType::Praise:
        touchInput(event.nowMs);
        applyOneShot("blush", "menu.praise", Fx::Heart,
                     static_cast<int>(kCuriousWindowMs), event, false);
        return m_current;

    case EventType::LevelUp:
        applyOneShot("levelup", "evt.levelup", Fx::Star,
                     static_cast<int>(kSuccessWindowMs) + 1000, event, true);
        return m_current;

    case EventType::AchievementUnlocked:
        applyOneShot("achievement", "evt.achievement", Fx::Star, 3000, event, true);
        return m_current;

    case EventType::QuestDone:
        applyOneShot("success", "evt.quest", Fx::None,
                     static_cast<int>(kSuccessWindowMs), event, true);
        return m_current;

    case EventType::KeywordHit: {
        touchInput(event.nowMs);
        std::string pose = "meme-omg";
        if (!event.keyword.empty()) {
            const std::string candidate = "meme-" + event.keyword;
            if (poseExists(candidate.c_str())) {
                pose = candidate;
            }
        }
        applyOneShot(pose, "meme." + event.keyword, Fx::None,
                     static_cast<int>(kCuriousWindowMs), event, true);
        return m_current;
    }
    }

    return m_current;
}

PoseResult PetStateMachine::speak(const std::string &pose, const std::string &scene, int ttlMs,
                                  const Event &event, bool proactive)
{
    touchInput(event.nowMs);
    if (pose.empty()) {
        // 不改立绘：仅借 compose 走一遍节流/静默规则并分配台词序号，姿态保持上下文态。
        m_current = compose(contextPose(event.nowMs), scene, Fx::None, 0, event, proactive);
    } else {
        applyOneShot(pose, scene, Fx::None, ttlMs, event, proactive);
    }
    return m_current;
}

} // namespace whalepet::core
