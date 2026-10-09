#pragma once

// EX1.4 游戏陪玩服务（EX3 起数据源改为中立接口 IGameCompanionSource）：
// 采样调度（定时）→ 只读读数（数据源）→ 判定（core::GameCompanionRules）
// → 上报（gameStateChanged 信号 → PetController::handleGameState → PetStateMachine）。
//
// 【EX3 变更】原 gamestate 适配器（进程外只读读取底座）已移除；本服务改为消费
//   viewmodel::IGameCompanionSource。EX4 将由**进程内小游戏状态源**实现该接口。
// 【只读/隐私】默认**关闭**：未 start() 时**不创建数据源、不打开任何进程、不启动定时器**（零开销）。
// 【零回归】未启用时不上报任何 GameStateChanged → PetStateMachine 游戏分支恒跳过。
// 【不崩溃】source->read() 失败一律 `available=false` + 原因，绝不伪造数据。

#include "core/GameState.h"
#include "viewmodel/IGameCompanionSource.h"

#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace whalepet::viewmodel {

class GameCompanionService : public QObject {
    Q_OBJECT
public:
    // 数据源工厂（可注入：测试用假数据源；EX4 运行时注入进程内小游戏状态源）。
    using SourceFactory = GameCompanionSourceFactory;

    explicit GameCompanionService(QObject *parent = nullptr);
    ~GameCompanionService() override;

    void setSourceFactory(SourceFactory factory);

    // 启用：按工厂建数据源并启动定时采样。失败返回 false + *error（已 detach，无副作用）。
    bool start(QString *error = nullptr);
    void stop();
    bool running() const { return m_running; }

    // 最近一轮读数「是否可用」（供上层显示「已连接 / 无数据」）
    bool available() const { return m_last.available; }

    const core::GameCompanionSample &current() const { return m_stable; }
    const core::GameSample &lastSample() const { return m_last; }
    qint64 sampleCount() const { return m_samples; }
    qint64 changeCount() const { return m_changes; }

    // 注入一轮读数（测试 / 数据源外部驱动）：内部跑规则并上报一次。
    void onSample(const core::GameSample &sample, qint64 nowMs);

signals:
    // 判定结果上报：stable 为滞回后的持续态；milestones 为相邻两轮的边沿里程碑（可全 false）。
    void gameStateChanged(const core::GameCompanionSample &stable,
                          const core::GameMilestoneSet &milestones, const core::GameSample &sample,
                          qint64 nowMs);
    // 数据源失效已自动停用（上层可提示用户）
    void companionStopped();

private slots:
    void tick();

private:
    SourceFactory m_factory;
    std::unique_ptr<IGameCompanionSource> m_source;
    core::GameCompanionRules m_rules;
    QTimer *m_timer = nullptr;

    core::GameSample m_last;   // 最近一轮读数（含 available=false）
    core::GameSample m_prev;   // 上一轮读数（里程碑比较用）
    core::GameCompanionSample m_stable;
    qint64 m_samples = 0;
    qint64 m_changes = 0;
    bool m_haveLast = false;
    bool m_running = false;
};

} // namespace whalepet::viewmodel
