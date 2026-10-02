#pragma once

// 感知采样调度（docs/PLUGIN-ARCHITECTURE.md §6.1）：
//   按 core::kWorkSampleIntervalMs（1s 级）驱动 IEnvironmentObserver，并把采样广播出去。
//
// 职责边界：只做「调度 + 广播」，不解释数据——判定在 core::WorkStateRules / WorkStateService。
// 观察者不可用（空实现 / 未启用）时仍按周期产出「无数据」快照，
// 使 Context API 能如实报告 `env.available == false`，而不是让外部误以为「用户一直空闲」。
//
// 生命周期（P7.1）：start()/stop() 会把「开始/停止观察」下发到 IEnvironmentObserver::setObserving()，
// Win32 实现据此安装/卸载低层输入钩子——即**只有真正采样时**才占用系统资源。

#include "core/WorkState.h"
#include "platform/DesktopObserver.h"

#include <QObject>

#include <cstdint>

class QTimer;

namespace whalepet::viewmodel {

class EnvironmentService : public QObject {
    Q_OBJECT
public:
    explicit EnvironmentService(QObject *parent = nullptr);
    ~EnvironmentService() override;

    // 注入观察者（**不接管所有权**；为空 = 该维度无数据）
    void setObserver(platform::IEnvironmentObserver *observer);
    platform::IEnvironmentObserver *observer() const { return m_observer; }

    // 采样周期（<= 0 时回落到 core::kWorkSampleIntervalMs）
    void setIntervalMs(int intervalMs);
    int intervalMs() const;

    bool available() const;
    const core::EnvSample &lastSample() const { return m_last; }
    qint64 sampleCount() const { return m_count; }

    void start();
    void stop();
    bool running() const;

    // 立即采样一次（测试 / 启用感知后立刻出结果）；nowMs <= 0 取系统墙钟
    core::EnvSample sampleNow(qint64 nowMs = 0);

signals:
    // 每次采样后广播（含「无数据」快照；判定侧据此降级为 Unknown）
    void sampleReady(const core::EnvSample &sample);

private:
    void onTimeout();

    platform::IEnvironmentObserver *m_observer = nullptr;
    QTimer *m_timer = nullptr;
    core::EnvSample m_last;
    qint64 m_count = 0;
};

} // namespace whalepet::viewmodel
