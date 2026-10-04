#include "core/PetStateMachine.h"

#include "core/FestivalRules.h"
#include "core/GrowthRules.h"
#include "core/IdleRules.h"
#include "core/PoseCatalog.h"

#include <cstring>

namespace whalepet::core {

namespace {
// 逗弄小剧场姿态保持时长
constexpr int kTeaseTtlMs = 1500;
// 长时间无输入的上下文姿态阈值以外的兜底
constexpr const char *kDefaultPose = "idle-cute";
// 工作状态播报的场景前缀（专注态静默的唯一豁免，见 makeLine）
constexpr const char *kWorkScenePrefix = "work.";
// 静息态的第二档（默认待机 → 等待），两档都参与节日换装
constexpr const char *kWaitingPose = "waiting";
// EX1.4 游戏里程碑主动播报的立绘保持时长（与既有一次性表现同量级）
constexpr int kGameMilestoneTtlMs = 2500;
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
    m_lateNightAwakeUntilMs = 0; // 深夜唤醒窗口随复位清空
    m_nextIdlePoolMs = 0;        // 日间待机小剧场重新武装
    m_sleepStartMs = 0;          // 睡眠循环复位
    m_workPool.reset();
    m_workPoolNextMs = 0;
    m_workState = WorkState::Unknown; // 感知状态随复位清空（上报方需重新上报，见 WorkStateService::reset）
    clearGameState();                 // 游戏陪玩态同理（见 GameCompanionService::reset）
    m_current = PoseResult{kDefaultPose, "", Fx::None, 0};
}

bool PetStateMachine::canSpeak(std::int64_t nowMs) const
{
    return (nowMs - m_lastSpeechMs) >= kSpeechGapMs;
}

void PetStateMachine::touchInput(std::int64_t nowMs)
{
    m_lastInputMs = nowMs;
    // P8：深夜（23:00–06:59）被交互后进入「醒着」窗口 —— 显 night 立绘，
    // 窗口内无操作则自动切回睡衣。窗口刻意覆盖任何 touchInput 路径
    // （点击 / 拖拽 / 关键词命中 / 外部播报），口径统一为「用户有操作」。
    m_lateNightAwakeUntilMs = nowMs + kLateNightAwakeMs;
}

bool PetStateMachine::lateNightAwake(std::int64_t nowMs) const
{
    if (daySlotOf(m_hour) != DaySlot::LateNight) {
        return false; // 只有深夜才有「睡衣 / 醒着」之分
    }
    return m_lateNightAwakeUntilMs > nowMs;
}

// 优先级：工作态 > 时段态（傍晚 night / 深夜 pajama）> 挂机态（afk/thinking/waiting）> 默认（idle）
// 工作态为 Unknown（无感知数据）时完全跳过 → 行为与 P6 一致（零回归）。
//
// 静息换装（docs/STATE-MACHINE.md §5.1）：走到最后一档待机链（默认待机 / 等待）时，
// 若当日命中节日则换成节日立绘。工作态（busy）、浏览 / 游戏 / 离开、深夜与思考都有
// 各自的立绘，不会进入这一档 —— 与参考项目「忙时情绪（含节日）一律让位」一致。
std::string PetStateMachine::contextPose(std::int64_t nowMs) const
{
    // 1) 工作态立绘（选图逻辑集中在 contextWorkPose，与工作态播报共用同一口径）：
    //    - 编程族（Coding / VibeCoding / Debugging）→ 常驻 `running`（P8）；
    //    - 其余工作态（Reading / Meeting）→ 从 work-* 立绘池取一张（P8，池见 WorkPosePool.h）；
    //    - 唯独 WorkState::Idle（在电脑前但未产出，对齐参考 idle）映射为默认待机立绘，
    //      放进下面的静息链，使节日换装得以生效。
    if (const char *pose = contextWorkPose(); pose != nullptr && poseExists(pose)) {
        return pose;
    }

    // 1.5) 满值特殊常驻（2026-10-04）：心情与饱腹同时满 → 摇尾巴，持续到任一不满。
    if (m_mood >= kMoodMax && m_satiety >= kSatietyMax) {
        return kVitalsFullPose;
    }

    // 1.6) 长时间待机睡眠循环（日间/傍晚）：待机 >= 20min 时接管；
    //      进入深夜时段自动停止，由下方时段链接管（daily-pajama / night 唤醒）。
    {
        const DaySlot idleSlot = daySlotOf(m_hour);
        if (idleSlot != DaySlot::LateNight && (nowMs - m_lastInputMs) >= kSleepIdleMs) {
            return sleepLoopPose(nowMs);
        }
    }

    // 2) 时段态（P8，docs/STATE-MACHINE.md §1.2）：
    //    日间（07:00–17:59）不接管，继续走挂机态 / 静息链（idle-cute / waiting / 节日换装）；
    //    傍晚（18:00–22:59）→ night；深夜（23:00–06:59）→ daily-pajama，
    //    交互唤醒窗口（kLateNightAwakeMs）内改显 night。
    const DaySlot slot = daySlotOf(m_hour);
    if (slot != DaySlot::Day) {
        return lateNightAwake(nowMs) ? kLateNightAwakePose : daySlotPoseOf(slot);
    }

    // 3) 挂机态（离开 / 思考）
    const std::int64_t idle = nowMs - m_lastInputMs;
    if (idle >= kAfkMs) {
        return "afk";
    }
    if (idle >= kThinkingMs) {
        return "thinking";
    }

    // 4) 游戏陪玩态（EX1.4）：优先级低于工作态/时段/挂机态，仅高于静息态。
    //    Unknown 时 gameMoodPose 返回 nullptr → 完全跳过（零回归）。
    if (const char *gamePose = gameMoodPose(m_gameMood);
        gamePose != nullptr && poseExists(gamePose)) {
        return gamePose;
    }

    // 5) 静息态（默认待机 / 等待）：命中节日则换装
    const char *festival = festivalPoseOf(nowMs);
    if (festival != nullptr && poseExists(festival)) {
        return festival;
    }
    return (idle >= kWaitingMs) ? kWaitingPose : kDefaultPose;
}

void PetStateMachine::advanceWorkPool(std::int64_t nowMs)
{
    if (!workStateUsesPool(m_workState)) {
        m_workPoolNextMs = 0; // 退出池态：下次进入时重新从第一张起轮
        return;
    }
    if (m_workPoolNextMs == 0) {
        m_workPool.next();
        m_workPoolNextMs = nowMs + kWorkPoseDwellMs;
        return;
    }
    if (nowMs >= m_workPoolNextMs) {
        m_workPool.next();
        m_workPoolNextMs = nowMs + kWorkPoseDwellMs;
    }
}

const char *PetStateMachine::contextWorkPose() const
{
    if (m_workState == WorkState::Unknown) {
        return nullptr; // 无感知数据：完全跳过（零回归）
    }
    if (workStateIsCoding(m_workState)) {
        return kCodingPose; // 编程族 → 常驻 running
    }
    if (workStateUsesPool(m_workState)) {
        return m_workPool.current(); // busy 非编程族 → work-* 池
    }
    const char *pose = workStatePose(m_workState);
    if (pose == nullptr) {
        return nullptr;
    }
    // Idle（在电脑前但未产出）映射为默认待机立绘 → 交回静息链，使节日换装生效
    if (!workStateIsBusy(m_workState) && std::strcmp(pose, kDefaultPose) == 0) {
        return nullptr;
    }
    return pose;
}

void PetStateMachine::advanceIdle(std::int64_t nowMs)
{
    const DaySlot slot = daySlotOf(m_hour);
    const std::int64_t idle = nowMs - m_lastInputMs;

    // 睡眠循环计时：日间 / 傍晚 + 待机 >= 20min 才生效；其余情况复位（含深夜停止循环）
    const bool sleepActive = (slot == DaySlot::Day || slot == DaySlot::Evening)
                             && idle >= kSleepIdleMs;
    if (!sleepActive) {
        m_sleepStartMs = 0;
    } else if (m_sleepStartMs == 0) {
        m_sleepStartMs = nowMs;
    }

    // 日间待机小剧场池：仅日间、静息（尚未升级为 thinking/afk）、非拖拽 / 面板抑制 /
    // 睡眠 / 工作忙态 / 游戏陪玩。与「当前是否正处于一次性表现」解耦：后者只暂停触发，
    // 但**不重置 15s 周期**，保证节奏严格为「每 15s 播一张，维持 3s」。
    // 注：一旦待机进入 thinking（60s）/ afk（180s），立绘表达其"心境"，小剧场让位。
    const bool contextEligible = (slot == DaySlot::Day) && idle < kThinkingMs && !m_dragging
                                 && !m_suppressed && !sleepActive
                                 && !workStateIsBusy(m_workState)
                                 && m_gameMood == GameMood::Unknown;
    if (!contextEligible) {
        m_nextIdlePoolMs = 0; // 离开日间静息上下文：重新武装（下次满足时重新计时）
        return;
    }
    if (m_oneShotUntilMs != 0) {
        return; // 正在表现中：保持周期计时不变
    }
    if (m_nextIdlePoolMs == 0) {
        m_nextIdlePoolMs = nowMs + kIdlePoolIntervalMs;
        return;
    }
    if (nowMs >= m_nextIdlePoolMs) {
        const char *pose = pickIdlePoolPose();
        if (pose != nullptr) {
            // 纯立绘表现：scene 留空，避免产生无对应语料的台词批次
            Event e = Event::simple(EventType::Tick, nowMs);
            applyOneShot(pose, "", Fx::None, static_cast<int>(kIdlePoolHoldMs), e, false);
        }
        m_nextIdlePoolMs = nowMs + kIdlePoolIntervalMs;
    }
}

const char *PetStateMachine::sleepLoopPose(std::int64_t nowMs) const
{
    if (m_sleepStartMs <= 0) {
        return kSleepPose;
    }
    const std::int64_t cycle = kSleepHoldMs + kSleepStretchHoldMs;
    const std::int64_t phase = (nowMs - m_sleepStartMs) % cycle;
    return (phase < kSleepHoldMs) ? kSleepPose : kSleepStretchPose;
}

const char *PetStateMachine::pickIdlePoolPose()
{
    const std::size_t count = idlePoolEligibleCount(m_affinity);
    if (count == 0) {
        return nullptr;
    }
    std::size_t index = 0;
    if (m_rng != nullptr) {
        const int raw = m_rng->nextInt(static_cast<int>(count));
        if (raw > 0) {
            index = static_cast<std::size_t>(raw) % count;
        }
    }
    return kIdlePoolPoses[index];
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
    // 特殊场景「静默陪伴」（EX1.4，docs/ROADMAP-ex1.md §2.5）：CG / 影片 / 专用场景 / 对话演出中
    // 一律不主动播报（含里程碑），仅保留立绘与既有点击交互；离开后自动恢复。
    if (gameCompanionSilent()) {
        return {};
    }
    // 工作态专注期主动静默（P7，docs/PLUGIN-ARCHITECTURE.md §6.2）：
    // 唯一豁免是工作状态自身的播报（work.*），否则「状态显著变化时出现」会被自己静默掉。
    if (workStateIsFocus(m_workState) && scene.compare(0, 5, kWorkScenePrefix) != 0) {
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

        // 闲置行为推进（日间待机小剧场池 / 睡眠循环计时）；可能产生一次性表现，
        // 若产生则下方 `m_oneShotUntilMs == 0` 判定为假，不再被上下文覆盖。
        advanceIdle(event.nowMs);

        // 无一次性姿态时按上下文刷新（时段/挂机变化会在此反映）
        if (m_oneShotUntilMs == 0 && !m_dragging) {
            // 工作立绘池的轮转节奏在**刷新上下文之前**推进，
            // 保证「到点换一张」与本 tick 的立绘输出是同一张（不多延迟一个 tick）。
            advanceWorkPool(event.nowMs);
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
        // 每日任务完成（2026-10-04）：显示 daily-done，维持 5s 后回落常驻立绘
        applyOneShot(kDailyDonePose, "evt.quest", Fx::None,
                     static_cast<int>(kDailyDoneTtlMs), event, true);
        return m_current;

    case EventType::WorkError:
        // 工作侧报错（ACP/宿主显式信号）：显示 failure，一次性表现后回落上下文常驻
        applyOneShot(kWorkErrorPose, "work.error", Fx::None,
                     static_cast<int>(kWorkErrorTtlMs), event, true);
        return m_current;

    case EventType::WorkStateChanged: {
        // P7 工作状态：优先级位于一次性事件之下、时段态与挂机态之上（见 contextPose）。
        // event.workState < 0 视为 Unknown（无数据）→ 立即退出工作态分支，行为回到 P6。
        const WorkState next = (event.workState < 0) ? WorkState::Unknown
                                                   : static_cast<WorkState>(event.workState);
        const bool changed = (next != m_workState);
        m_workState = next;

        // P8：工作态切换时重置立绘池节奏 —— 新状态立即取一张（编程族直接 running）。
        // 这一条同时是 **ACP 联动的落点**：ACP 显式信号经 WorkStateService::applyExternalState
        // 覆盖工作态后，同样走本事件（见 docs/STATE-MACHINE.md §1.3）。
        if (changed) {
            m_workPoolNextMs = 0;
        }

        // 「不打断」：一次性姿态未过期或正在拖拽时，不覆盖当前立绘、也不播报
        // （工作态仍然被记下，待一次性姿态到期后由 fallback 自然生效）
        const bool busy = (m_oneShotUntilMs > 0) || m_dragging;
        if (!busy) {
            advanceWorkPool(event.nowMs);
            m_current = fallback(event.nowMs);
        }

        // 状态显著变化 → 播报一句：走 proactive 的深夜静默 / 面板抑制 / ≥6s 节流规则，
        // 但**不受专注态静默限制**（work.* 场景是唯一豁免，见 makeLine）。
        // P8：播报立绘与常驻立绘同源（contextWorkPose），避免「编程时闪一下 work-ram」。
        if (changed && !busy && next != WorkState::Unknown) {
            const char *pose = contextWorkPose();
            const char *scene = workStateScene(next);
            if (pose != nullptr && scene != nullptr) {
                applyOneShot(pose, scene, Fx::None, 0, event, true);
            }
        }
        return m_current;
    }

    case EventType::GameStateChanged: {
        // EX1.4 游戏陪玩：优先级**最低**（contextPose 第 4 档，仅高于静息态）。
        // event.gameMood < 0 视为 Unknown（无陪玩数据）→ 立即退出游戏分支，行为回到 EX1 前。
        m_gameMood = (event.gameMood < 0) ? GameMood::Unknown
                                         : static_cast<GameMood>(event.gameMood);
        m_gameSpecialScene = event.gameSpecialScene;

        // 「不打断」：一次性姿态未过期或正在拖拽时，不覆盖当前立绘、也不播报
        // （陪玩态仍被记下，待一次性姿态到期后由 fallback 自然生效）
        const bool busy = (m_oneShotUntilMs > 0) || m_dragging;
        if (!busy) {
            m_current = fallback(event.nowMs);
        }

        // 仅**高置信度里程碑**（升级/BOSS/濒死/恢复/通关）主动播报；静默陪伴期间一律不播报。
        // 其余状态变化只由立绘体现，等用户交互时再说话——遵循「不打扰」（§2.5）。
        if (!busy && event.gameMilestones.any() && !gameCompanionSilent()) {
            const char *pose = gameMilestonePose(event.gameMilestones);
            const char *scene = gameMilestoneScene(event.gameMilestones);
            if (pose != nullptr && scene != nullptr) {
                const Fx fx = event.gameMilestones.clear ? Fx::Star : Fx::None;
                applyOneShot(pose, scene, fx, kGameMilestoneTtlMs, event, true);
            }
        }
        return m_current;
    }

    case EventType::KeywordHit: {
        touchInput(event.nowMs);
        std::string pose = "meme-omg";
        if (!event.keyword.empty()) {
            const std::string candidate = "meme-" + event.keyword;
            if (poseExists(candidate.c_str())) {
                pose = candidate;
            }
        }
        // P8：热词命中的工作向立绘（work-deadline / work-boss / work-deploy / work-review …）
        // 记入工作立绘池的「最近」，使随后的池轮转避开刚出现过的这一张。
        m_workPool.note(pose.c_str());
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
    // P8：热词 / 外部播报指定的立绘若属工作池成员，记入「最近」，
    // 使随后的工作池轮转避开它（热词与工作池的联动落点，见 docs/STATE-MACHINE.md §1.3）。
    m_workPool.note(pose.c_str());
    if (pose.empty()) {
        // 不改立绘：仅借 compose 走一遍节流/静默规则并分配台词序号，姿态保持上下文态。
        m_current = compose(contextPose(event.nowMs), scene, Fx::None, 0, event, proactive);
    } else {
        applyOneShot(pose, scene, Fx::None, ttlMs, event, proactive);
    }
    return m_current;
}

} // namespace whalepet::core
