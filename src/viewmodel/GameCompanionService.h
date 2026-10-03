#pragma once

// EX1.4 游戏陪玩服务：采样调度（定时）→ 只读读数（gamestate 适配器）→ 判定（core::GameCompanionRules）
// → 上报（gameStateChanged 信号 → PetController::handleGameState → PetStateMachine）。
// 归属：docs/ROADMAP-ex1.md EX1.4 交付物 3。
//
// 【只读/隐私】默认**关闭**：未 start() 时**不创建适配器、不打开任何进程、不启动定时器**（零开销）。
// 【零回归】未启用时不上报任何 GameStateChanged → PetStateMachine 游戏分支恒跳过，行为同 EX1 前。
// 【不崩溃】attach/read 失败一律 `available=false` + 原因，绝不伪造数据（§4.3）。

#include "core/GameState.h"
#include "gamestate/GameProfile.h"
#include "gamestate/IGameStateAdapter.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QTimer;

namespace whalepet::viewmodel {

class GameCompanionService : public QObject {
    Q_OBJECT
public:
    // 适配器工厂（可注入：测试用假适配器，运行时默认走 gamestate::createGameStateAdapter）
    using AdapterFactory = std::function<std::unique_ptr<gamestate::IGameStateAdapter>(
        const gamestate::GameProfile &profile, QString *error)>;

    explicit GameCompanionService(QObject *parent = nullptr);
    ~GameCompanionService() override;

    void setAdapterFactory(AdapterFactory factory);

    // 启用：按 profile 建适配器并启动定时采样。失败返回 false + *error（已 detach，无副作用）。
    bool start(const gamestate::GameProfile &profile, QString *error);
    void stop();
    bool running() const { return m_running; }

    // 最近一轮读数「是否可用」（供上层显示「已连接 / 无数据」）
    bool available() const { return m_last.available; }

    const core::GameCompanionSample &current() const { return m_stable; }
    const core::GameSample &lastSample() const { return m_last; }
    const gamestate::GameProfile &profile() const { return m_profile; }
    qint64 sampleCount() const { return m_samples; }
    qint64 changeCount() const { return m_changes; }

    // 注入一轮读数（测试 / 桥接外部驱动）：内部跑规则并上报一次。
    void onSample(const core::GameSample &sample, qint64 nowMs);

signals:
    // 判定结果上报：stable 为滞回后的持续态；milestones 为相邻两轮的边沿里程碑（可全 false）。
    void gameStateChanged(const core::GameCompanionSample &stable,
                          const core::GameMilestoneSet &milestones, const core::GameSample &sample,
                          qint64 nowMs);
    // 适配器失效已自动停用（上层可提示用户重连）
    void companionStopped();

private slots:
    void tick();

private:
    static std::unique_ptr<gamestate::IGameStateAdapter> defaultFactory(
        const gamestate::GameProfile &profile, QString *error);

    gamestate::GameProfile m_profile;
    AdapterFactory m_factory;
    std::unique_ptr<gamestate::IGameStateAdapter> m_adapter;
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
