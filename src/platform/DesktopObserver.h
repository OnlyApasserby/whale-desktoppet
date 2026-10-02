#pragma once

// 桌面环境感知层（接口）：把「用户此刻在电脑上做什么」抽象为可注入、可替身的采样接口。
//
// 归属：docs/PLUGIN-ARCHITECTURE.md §3/§6.1、docs/ROADMAP-P7.md P7.0。
//
// 隐私边界（docs/CONTEXT-API.md §5，硬约束）：
//   * **只统计**键鼠事件数与空闲时长——不记录按键、不读文本、不读编辑区；
//   * 只取**前台**窗口标题与进程名——不做全窗口枚举、不截图；
//   * 采样失败或不可用时返回 false 并**不得伪造数据**（宁可 Unknown，也不猜）。
//
// P7.0 提供空实现 EmptyDesktopObserver（恒 unknown，完全不采集）；
// P7.1 新增 Win32DesktopObserver（真实 Win32 采集，见 Win32DesktopObserver.h）。
// 两者都只实现本文件的接口，**core 判定与宿主装配无需改动**。

#include "core/WorkState.h"

#include <cstdint>

namespace whalepet::platform {

// 采样器的可选生命周期（默认空实现）：
//   宿主启用/停用感知时回调（EnvironmentService::start/stop）。
//   需要申请**系统级资源**的实现（如 Win32 低层输入钩子）必须在此获取/释放，
//   以保证「默认关闭时不占用系统资源、退出时可靠回收」；
//   空实现与测试替身无需关心（保持默认 no-op，零回归）。

// 前台窗口采样器：填充 EnvSample 的 appId / windowTitle
class IForegroundSampler {
public:
    virtual ~IForegroundSampler() = default;
    virtual bool available() const = 0;
    // 成功返回 true 并填充 out（只改自己负责的字段）；失败返回 false 且不得改动 out
    virtual bool sampleForeground(core::EnvSample &out) = 0;
    virtual void setObserving(bool active) { (void)active; }
};

// 输入活跃度采样器：填充 EnvSample 的 hasInput / idleMs / inputEvents。
// 约定：`inputEvents` 是**自上次采样以来**的事件数增量（不是窗口累计值），
//      窗口累计由 CompositeDesktopObserver 统一完成（判定规则依赖窗口语义，
//      见 core/WorkState.h 的 kWorkWindowMs）。
class IActivitySampler {
public:
    virtual ~IActivitySampler() = default;
    virtual bool available() const = 0;
    virtual bool sampleActivity(core::EnvSample &out) = 0;
    virtual void setObserving(bool active) { (void)active; }
};

// 系统状态采样器：会话锁定 / 屏保 → systemPaused（P7.1 起由 Win32 实现填充）
class ISystemStatusSampler {
public:
    virtual ~ISystemStatusSampler() = default;
    virtual bool available() const = 0;
    virtual bool sampleSystemStatus(core::EnvSample &out) = 0;
    virtual void setObserving(bool active) { (void)active; }
};

// 统一观察者：宿主（EnvironmentService）只依赖本接口，便于注入真实实现或空实现
class IEnvironmentObserver {
public:
    virtual ~IEnvironmentObserver() = default;
    virtual bool available() const = 0;
    // 产出一份采样快照。任何子采样器不可用都不影响其余字段；
    // 全部不可用时返回 available()==false 且 isEmpty()==true 的「未知」快照（不抛错）。
    virtual core::EnvSample sample(std::int64_t nowMs) = 0;
    virtual void setObserving(bool active) { (void)active; }
};

// 组合观察者：把三个子采样器拼成一份 EnvSample，并负责**只有它能算的两件事**：
//   * category：经 core::classifyApp 归一化（保持判定规则纯函数化的前提）；
//   * appSwitches / dwellMs：需要跨采样记忆，故落在有状态的组合层（core 层保持无状态）。
//
// 子采样器**不接管所有权**（按需注入真实实现或测试替身，可为空 = 该维度不可用）。
class CompositeDesktopObserver : public IEnvironmentObserver {
public:
    CompositeDesktopObserver() = default;
    ~CompositeDesktopObserver() override = default;

    void setForegroundSampler(IForegroundSampler *sampler) { m_foreground = sampler; }
    void setActivitySampler(IActivitySampler *sampler) { m_activity = sampler; }
    void setSystemStatusSampler(ISystemStatusSampler *sampler) { m_systemStatus = sampler; }

    bool available() const override;
    core::EnvSample sample(std::int64_t nowMs) override;
    // 生命周期下发到各子采样器（如 Win32 低层输入钩子的安装/卸载）
    void setObserving(bool active) override;

private:
    IForegroundSampler *m_foreground = nullptr;
    IActivitySampler *m_activity = nullptr;
    ISystemStatusSampler *m_systemStatus = nullptr;

    // 跨采样记忆：滚动窗口累计（切换数 / 事件数）与同应用停留起点
    core::EnvSample m_last;
    bool m_hasLast = false;
    std::int64_t m_windowStartMs = 0;
    int m_windowEvents = 0;
    int m_windowSwitches = 0;
    std::int64_t m_appSinceMs = 0;
};

} // namespace whalepet::platform
