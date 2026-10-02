#pragma once

// 真实 Win32 桌面感知（docs/ROADMAP-P7-Fin.md P7.1）。
//
// 三个子采样器（实现 DesktopObserver.h 的接口，宿主只认接口）：
//   * Win32ForegroundSampler   前台窗口标题 + 进程名
//                              （GetForegroundWindow → GetWindowTextW / GetWindowThreadProcessId
//                               + QueryFullProcessImageNameW）；
//   * Win32ActivitySampler     空闲时长（GetLastInputInfo）+ 键鼠事件计数
//                              （**首选低层钩子** WH_KEYBOARD_LL / WH_MOUSE_LL；
//                               钩子安装失败时**降级**为 GetLastInputInfo 差分 + 定时采样）；
//   * Win32SystemStatusSampler 会话锁定 / 屏保
//                              （OpenInputDesktop 失败 = 锁屏安全桌面；SPI_GETSCREENSAVERRUNNING）。
//
// 聚合（category 归一化 / appSwitches / dwellMs / 滚动窗口）复用 CompositeDesktopObserver，
// 本类只负责「提供真实读数 + 管理生命周期」，不重复实现聚合逻辑（单一职责）。
//
// 隐私边界（docs/CONTEXT-API.md §5，硬约束）：
//   * 输入侧**只计数**：不记录按键、不读文本；鼠标只计按键与滚轮，**不计移动**
//     （WM_MOUSEMOVE 量级极大，计进来会让「输入爆发」失真）；
//   * 只取前台窗口标题与进程名：不做全窗口枚举、不截图；
//   * 采样失败一律返回 false 并保持「无数据」，**绝不伪造**。
//
// 生命周期：低层钩子只在 `setObserving(true)`（宿主开启感知）时安装，
// `setObserving(false)` 或析构时卸载——**默认关闭时不存在任何钩子**。
// 注入替身读数（单测）时**永不**安装系统钩子，测试进程不受影响。

#include "platform/DesktopObserver.h"

#include <cstdint>
#include <functional>
#include <string>

namespace whalepet::platform {

// 「正在活跃」判据：距最近一次输入小于该时长（与 1s 采样周期对齐）
inline constexpr std::int64_t kWin32InputActiveMs = 1500;

// ---------------------------------------------------------------------------
// 原始读数（宽字符；不暴露任何 Win32 句柄，故可在单测中注入替身）
// ---------------------------------------------------------------------------
struct Win32ForegroundRaw {
    bool valid = false;         // false = 读不到（无前台窗口 / 锁屏）→ 上层保持「无数据」
    std::wstring windowTitle;
    std::wstring processPath;   // 进程可执行文件全路径；读不到时为空
};

struct Win32ActivityRaw {
    bool valid = false;
    std::uint32_t lastInputTickMs = 0; // GetLastInputInfo().dwTime
    std::uint32_t nowTickMs = 0;       // GetTickCount()
};

struct Win32SystemRaw {
    bool valid = false;
    bool sessionLocked = false;
    bool screensaverRunning = false;
};

using Win32ForegroundReader = std::function<Win32ForegroundRaw()>;
using Win32ActivityReader = std::function<Win32ActivityRaw()>;
using Win32SystemReader = std::function<Win32SystemRaw()>;

// ---------------------------------------------------------------------------
// 前台窗口采样器
// ---------------------------------------------------------------------------
class Win32ForegroundSampler : public IForegroundSampler {
public:
    Win32ForegroundSampler() = default;
    explicit Win32ForegroundSampler(Win32ForegroundReader reader);

    bool available() const override { return true; }
    bool sampleForeground(core::EnvSample &out) override;

private:
    Win32ForegroundReader m_reader; // 空 = 使用真实 Win32 读数
};

// ---------------------------------------------------------------------------
// 输入活跃度采样器
// ---------------------------------------------------------------------------
class Win32ActivitySampler : public IActivitySampler {
public:
    Win32ActivitySampler();
    explicit Win32ActivitySampler(Win32ActivityReader reader);
    ~Win32ActivitySampler() override;

    bool available() const override { return true; }
    bool sampleActivity(core::EnvSample &out) override;
    void setObserving(bool active) override;

    // 可观测（日志用）：当前输入计数是否来自低层钩子（false = 差分降级）
    bool usingLowLevelHooks() const { return m_hooksActive; }
    // 配置意图：是否**倾向于**低层钩子（真实读数 = true；注入替身 = false）。
    // 与 usingLowLevelHooks() 的区别：本函数不依赖「是否已安装」，故可确定性单测。
    bool hooksPreferred() const { return m_useHooks; }
    const char *sourceName() const;

private:
    Win32ActivityReader m_reader; // 空 = 使用真实 Win32 读数
    bool m_useHooks = true;       // 注入替身时自动关闭（单测不安装系统钩子）
    bool m_hooksActive = false;
    bool m_hasPrevTick = false;
    std::uint32_t m_prevLastInputTickMs = 0;
};

// ---------------------------------------------------------------------------
// 系统状态采样器（会话锁定 / 屏保 → systemPaused）
// ---------------------------------------------------------------------------
class Win32SystemStatusSampler : public ISystemStatusSampler {
public:
    Win32SystemStatusSampler() = default;
    explicit Win32SystemStatusSampler(Win32SystemReader reader);

    bool available() const override { return true; }
    bool sampleSystemStatus(core::EnvSample &out) override;

private:
    Win32SystemReader m_reader; // 空 = 使用真实 Win32 读数
};

// ---------------------------------------------------------------------------
// 统一观察者：宿主（EnvironmentService）唯一依赖的实现
// ---------------------------------------------------------------------------
class Win32DesktopObserver : public IEnvironmentObserver {
public:
    // 默认真实 Win32 读数
    Win32DesktopObserver();
    // 注入替身读数（单测；三者都可为空 = 该维度用真实读数）
    Win32DesktopObserver(Win32ForegroundReader foreground,
                         Win32ActivityReader activity,
                         Win32SystemReader system);
    ~Win32DesktopObserver() override;

    bool available() const override;
    core::EnvSample sample(std::int64_t nowMs) override;
    void setObserving(bool active) override;

    bool usingLowLevelHooks() const { return m_activity.usingLowLevelHooks(); }
    bool hooksPreferred() const { return m_activity.hooksPreferred(); }
    const char *inputSourceName() const { return m_activity.sourceName(); }

private:
    Win32ForegroundSampler m_foreground;
    Win32ActivitySampler m_activity;
    Win32SystemStatusSampler m_system;
    CompositeDesktopObserver m_composite;
};

} // namespace whalepet::platform
