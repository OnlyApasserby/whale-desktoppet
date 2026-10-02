#pragma once

// 工作状态（work state）与桌面环境采样：**零 Qt 依赖**（仅 C++17 标准库），可脱 UI 单测。
//
// 归属：docs/PLUGIN-ARCHITECTURE.md §6.1、docs/ROADMAP-P7.md P7.0。
//
// 分层约定：
//   * 本文件只放「纯数据类型 + 判定常量 + 无副作用查表函数」；
//   * 判定算法在 core/WorkStateRules.h（参数可注入、时钟由采样携带，故完全可测）。
//
// 隐私边界（docs/CONTEXT-API.md §5）：
//   EnvSample 只承载「前台应用标识 / 窗口标题 / 键鼠事件计数 / 空闲时长」，
//   **不承载任何输入内容**（不记录按键、不读文本、不读编辑区）。
//
// 零回归关键：WorkState::Unknown 表示「无感知数据」，此时桌宠行为与 P6 完全一致。

#include <cstddef>
#include <cstdint>
#include <string>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 采样与判定常量（禁止在别处再写字面量）
// ---------------------------------------------------------------------------
inline constexpr std::int64_t kWorkSampleIntervalMs = 1000;  // 采样周期（1s 级）
inline constexpr std::int64_t kWorkWindowMs = 10000;         // 统计窗口（切换数 / 事件数按此窗口累计）
inline constexpr std::int64_t kWorkStateMinDwellMs = 5000;   // 滞回：状态最短驻留时长
inline constexpr double kWorkStateMinConfidence = 0.6;       // 低于此置信度不改变现状
inline constexpr std::int64_t kWorkIdleMs = 20000;           // 20s 无输入 → 空闲
inline constexpr std::int64_t kWorkAfkMs = 180000;           // 180s 无输入 → 离开（与 kAfkMs 对齐）
inline constexpr std::int64_t kWorkReadingMaxIdleMs = 15000; // 阅读：有输入但稀疏
inline constexpr int kWorkReadingMaxEvents = 3;              // 阅读：窗口内事件数上限
inline constexpr std::int64_t kWorkCodingMinDwellMs = 60000; // 编码：同应用连续停留下限（专注）
inline constexpr int kWorkCodingMaxSwitches = 2;             // 编码：窗口内切换上限
inline constexpr int kVibeBurstMinEvents = 20;               // Vibe：窗口内事件数下限（≈2 次/秒）
inline constexpr int kVibeMinSwitches = 3;                   // Vibe：窗口内切换下限（跳跃）
inline constexpr std::int64_t kVibeMaxDwellMs = 45000;       // Vibe：同应用停留上限

// ---------------------------------------------------------------------------
// 前台应用类别
// ---------------------------------------------------------------------------
enum class AppCategory { Unknown, Editor, Terminal, Browser, Meeting, Game, Office, Other };

const char *appCategoryId(AppCategory category);
AppCategory appCategoryFromId(const std::string &id);

// 把「进程名 / 窗口标题」归一化为应用类别。
//   优先级：进程名精确匹配（去路径、去 .exe、忽略大小写） → 标题关键词 → Other。
//   两者都为空 → Unknown（表示「无数据」，而不是「未知应用」）。
AppCategory classifyApp(const std::string &appId, const std::string &windowTitle);

// 窗口标题是否命中任一关键词（忽略大小写；关键词表为小写）
bool windowTitleHasAny(const std::string &title, const char *const *keywords, std::size_t count);

// 调试语义关键词表（供 WorkStateRules 判定 Debugging；长度见 kDebugKeywordCount）
extern const char *const kDebugKeywords[];
extern const std::size_t kDebugKeywordCount;

// ---------------------------------------------------------------------------
// 工作状态
// ---------------------------------------------------------------------------
enum class WorkState {
    Unknown,    // 无感知数据（未启用 / 采样不可用）——**不改变任何既有行为**
    Idle,       // 在电脑前但未产出（短时空闲）
    Reading,    // 阅读（输入稀疏）
    Coding,     // 专注编码（长时间连续编辑、切换少）
    VibeCoding, // 与 AI 快速迭代（高频爆发输入 + 频繁切换 + 停留短）
    Debugging,  // 调试 / 构建 / 跑测试
    Browsing,   // 浏览（输入适中）
    Meeting,    // 会议 / 沟通
    Game,       // 游戏
    Afk         // 离开（长时间无输入 / 会话锁定）
};

const char *workStateId(WorkState state);
WorkState workStateFromId(const std::string &id); // 未识别 → Unknown

// 专注态：主动台词静默（唯一豁免是 work.* 场景自身的状态播报）
bool workStateIsFocus(WorkState state);

// 工作态对应的立绘 pose：**复用既有 93 张立绘**，不新增美术资源。
// Unknown 返回 nullptr（调用方应保持当前立绘不变）。
const char *workStatePose(WorkState state);

// 工作态对应的台词场景 key（`work.*`）；Unknown 返回 nullptr（不播报）
const char *workStateScene(WorkState state);

// ---------------------------------------------------------------------------
// 采样数据
// ---------------------------------------------------------------------------

// 桌面环境采样（由 platform 层填充；可自由拷贝）
struct EnvSample {
    std::string appId;        // 前台进程名（如 "Code.exe"）；空 = 无数据
    std::string windowTitle;  // 前台窗口标题；空 = 无数据
    AppCategory category = AppCategory::Unknown; // 由 classifyApp 填充（无数据时保持 Unknown）
    bool hasInput = false;    // 统计窗口内是否有键鼠输入
    std::int64_t idleMs = 0;  // 距最近一次输入的空闲时长
    int inputEvents = 0;      // 统计窗口（kWorkWindowMs）内的键鼠事件数（**只计数，不含内容**）
    int appSwitches = 0;      // 统计窗口内的前台应用切换次数
    std::int64_t dwellMs = 0; // 当前前台应用已连续停留时长
    bool systemPaused = false; // 会话锁定 / 屏保 / 全屏独占（P7.1 起由系统状态采样器置位）
    std::int64_t nowMs = 0;   // 采样时刻（墙钟毫秒）

    // 「无任何数据」：未启用感知或采样不可用
    bool isEmpty() const { return appId.empty() && windowTitle.empty() && !hasInput; }
};

// 工作状态判定结果
struct WorkStateSample {
    WorkState state = WorkState::Unknown;
    double confidence = 0.0;  // 0..1
    std::int64_t sinceMs = 0; // 进入该状态的时刻（滞回用）
};

} // namespace whalepet::core
