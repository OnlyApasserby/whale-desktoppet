#pragma once

// Core 层公共类型：零 Qt 依赖（仅 C++17 标准库），可脱离界面单测。

#include "core/GameState.h"

#include <cstdint>
#include <string>

namespace whalepet::core {

// 特效类型（由 Presenter 翻译成 Qt 表现，状态机不关心怎么画）
enum class Fx { None, Heart, Star, Particle };

// 立绘交互分区（点击命中）
enum class Zone { None, Head, Belly, Tail, Body };

// 状态机输出：语义结果，非 UI
struct PoseResult {
    std::string pose;     // pose 名，如 "blush"
    std::string lineKey;  // 台词场景 key，空表示不说话
    Fx fx = Fx::None;     // 特效类型
    int ttlMs = 0;        // 该姿态保持时长；0 表示由规则/事件决定

    // 表现批次序号（P2 增补，见 docs/ROADMAP-P2.md §0）：
    // 状态机每 tick 都会重推同一个缓存结果，Presenter 需要区分
    // 「新的一次表现」与「同一结果的重复重放」，否则特效与台词会被按 tick 反复播放。
    // 约定：只在真正产生新特效 / 新台词时自增；同一结果重放时保持不变；0 表示「无」。
    std::uint32_t fxSerial = 0;
    std::uint32_t lineSerial = 0;
};

enum class EventType {
    Tick,        // 定时推进
    Click,       // 单击分区
    TripleClick, // 三连击
    DragStart,   // 开始拖拽
    DragEnd,     // 结束拖拽
    Feed,        // 投喂
    Tease,       // 戳一下
    Praise,      // 夸夸
    LevelUp,
    AchievementUnlocked,
    QuestDone,
    IdleTimeout, // 挂机超时
    Clock,       // 系统时间推进
    KeywordHit,  // 聊天关键词命中
    // P7：工作状态变化（由 viewmodel::WorkStateService 上报）。
    // 语义：工作态是「时段态」，优先级高于夜/挂机态，但低于一次性事件；
    // 详见 docs/PLUGIN-ARCHITECTURE.md §6.2 与 docs/STATE-MACHINE.md §3。
    WorkStateChanged,
    // EX1.4：游戏陪玩状态变化（由 viewmodel::GameCompanionService 上报）。
    // 语义：游戏陪玩态优先级**最低**（低于工作态 / 时段态 / 挂机态，仅高于静息态）；
    // 永不打断既有一次性表现与工作专注；specialScene != 0 时进入「静默陪伴」。
    // 详见 docs/ROADMAP-ex1.md §2.5。
    GameStateChanged,
    // 工作侧报错（由 viewmodel::AcpSignalService 的显式信号驱动，如 ACP tool.error）。
    // 语义：一次性表现 —— 收到即显示 failure 立绘，ttl 到期回落到上下文常驻。
    WorkError
};

struct Event {
    EventType type = EventType::Tick;
    Zone zone = Zone::None;
    int hour = -1;             // Clock 事件：0..23
    std::string keyword;       // KeywordHit：关键词（不含 meme- 前缀）
    int workState = -1;        // WorkStateChanged：core::WorkState 的整数值（-1 = 未提供）
    // GameStateChanged：core::GameMood 的整数值（-1 = 未提供 → Unknown）
    int gameMood = -1;
    int gameSpecialScene = 0;  // GameStateChanged：GameSpecialScene 整数值（0 = 无 → 正常陪伴）
    GameMilestoneSet gameMilestones; // GameStateChanged：本轮里程碑（默认全 false）
    std::int64_t nowMs = 0;    // 事件时间戳（毫秒，单调递增）

    static Event tick(std::int64_t now) { Event e; e.type = EventType::Tick; e.nowMs = now; return e; }
    static Event click(Zone z, std::int64_t now) { Event e; e.type = EventType::Click; e.zone = z; e.nowMs = now; return e; }
    static Event simple(EventType t, std::int64_t now) { Event e; e.type = t; e.nowMs = now; return e; }
    static Event clock(int h, std::int64_t now) { Event e; e.type = EventType::Clock; e.hour = h; e.nowMs = now; return e; }
    // 注意：工厂函数不能叫 keyword —— 会与数据成员 keyword 同名冲突（C++ 不允许同名成员）
    static Event keywordHit(const std::string &kw, std::int64_t now) { Event e; e.type = EventType::KeywordHit; e.keyword = kw; e.nowMs = now; return e; }
    // 工作状态变化：workState 为 core::WorkState 的整数值（含 Unknown，用于快速降级）
    static Event workStateChanged(int state, std::int64_t now) { Event e; e.type = EventType::WorkStateChanged; e.workState = state; e.nowMs = now; return e; }
    // 游戏陪玩状态变化：mood 为 core::GameMood 整数值（-1 = Unknown）；specialScene 见 GameSpecialScene。
    static Event gameStateChanged(int mood, int specialScene, std::int64_t now)
    {
        Event e;
        e.type = EventType::GameStateChanged;
        e.gameMood = mood;
        e.gameSpecialScene = specialScene;
        e.nowMs = now;
        return e;
    }
    // 含里程碑的重载（高置信度事件才允许主动播报，见 docs/ROADMAP-ex1.md §2.5）
    static Event gameStateChanged(int mood, int specialScene, const GameMilestoneSet &milestones,
                                  std::int64_t now)
    {
        Event e = gameStateChanged(mood, specialScene, now);
        e.gameMilestones = milestones;
        return e;
    }
};

// 时间窗口与概率常量（沿用 whale 取值，见 docs/STATE-MACHINE.md §2）
inline constexpr std::int64_t kAfkMs = 180000;       // 180s 无输入 → afk
inline constexpr std::int64_t kSpeechGapMs = 6000;   // 两条台词最小间隔
inline constexpr std::int64_t kSuccessWindowMs = 2000;
inline constexpr std::int64_t kCuriousWindowMs = 6000;
inline constexpr double kTeaseChance = 0.006;        // 每 tick 触发逗弄概率
inline constexpr std::int64_t kTickMs = 200;         // 待机 tick
inline constexpr int kNightStartHour = 23;           // 23:00–05:59 静默
inline constexpr int kNightEndHour = 6;
inline constexpr std::int64_t kWaitingMs = 15000;    // 15s 无输入 → waiting
inline constexpr std::int64_t kThinkingMs = 60000;   // 60s 无输入 → thinking

} // namespace whalepet::core
