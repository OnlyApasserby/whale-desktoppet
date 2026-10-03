#include "viewmodel/GameCompanionService.h"

#include <QDateTime>
#include <QTimer>

namespace whalepet::viewmodel {

GameCompanionService::GameCompanionService(QObject *parent)
    : QObject(parent), m_rules(core::GameCompanionRules())
{
}

GameCompanionService::~GameCompanionService()
{
    // 析构即停用：确保不遗留采样定时器与（只读）进程句柄。
    stop();
}

void GameCompanionService::setAdapterFactory(AdapterFactory factory)
{
    m_factory = std::move(factory);
}

std::unique_ptr<gamestate::IGameStateAdapter> GameCompanionService::defaultFactory(
    const gamestate::GameProfile &profile, QString *error)
{
    return gamestate::createGameStateAdapter(profile, error);
}

bool GameCompanionService::start(const gamestate::GameProfile &profile, QString *error)
{
    stop(); // 幂等：重复启用先释放上一轮资源
    m_profile = profile;

    if (m_factory == nullptr) {
        m_factory = &GameCompanionService::defaultFactory;
    }
    std::unique_ptr<gamestate::IGameStateAdapter> adapter = m_factory(m_profile, error);
    if (adapter == nullptr) {
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("无法为该游戏档案创建状态适配器（引擎/配置不受支持）");
        }
        return false;
    }
    if (!adapter->attach(m_profile, error)) {
        adapter->detach();
        return false;
    }

    m_adapter = std::move(adapter);
    m_rules = core::GameCompanionRules();
    m_last = core::GameSample();
    m_prev = core::GameSample();
    m_stable = core::GameCompanionSample();
    m_haveLast = false;
    m_samples = 0;
    m_changes = 0;

    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::CoarseTimer); // 200ms 档，不需要精确定时
        connect(m_timer, &QTimer::timeout, this, &GameCompanionService::tick);
    }
    int intervalMs = static_cast<int>(core::kGameSampleIntervalMs);
    if (intervalMs <= 0) {
        intervalMs = 200;
    }
    m_timer->start(intervalMs);
    m_running = true;
    return true;
}

void GameCompanionService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    if (m_adapter != nullptr) {
        m_adapter->detach();
        m_adapter.reset();
    }
    m_running = false;
    // 复位判定，避免下次启用沿用旧状态（上报方需重新上报，见 PetStateMachine::reset）
    m_last = core::GameSample();
    m_prev = core::GameSample();
    m_stable = core::GameCompanionSample();
    m_haveLast = false;
}

void GameCompanionService::tick()
{
    if (!m_running || m_adapter == nullptr) {
        return;
    }
    core::GameSample sample;
    QString error;
    const bool ok = m_adapter->read(&sample, &error);
    if (!ok) {
        // 读取失败：照实回报「不可用」（不伪造数据）；适配器失效则停用并告知上层
        sample.available = false;
        onSample(sample, QDateTime::currentMSecsSinceEpoch());
        if (m_adapter != nullptr && m_adapter->invalidated()) {
            stop();
            emit companionStopped();
        }
        return;
    }
    onSample(sample, QDateTime::currentMSecsSinceEpoch());
}

void GameCompanionService::onSample(const core::GameSample &sample, qint64 nowMs)
{
    core::GameSample current = sample;
    current.nowMs = nowMs;

    // 滞回判定（Unknown 立即生效：失联时尽快退回既有行为）
    const core::GameCompanionSample stable = m_rules.evaluate(current, m_stable);
    // 边沿里程碑：仅相邻两轮比较；首轮无上一轮 → 不产生（避免「启动即播报」误报）
    const core::GameMilestoneSet milestones =
        m_haveLast ? m_rules.milestones(current, m_prev) : core::GameMilestoneSet();

    if (stable.mood != m_stable.mood) {
        ++m_changes;
    }
    m_stable = stable;
    m_prev = current;
    m_last = current;
    m_haveLast = true;
    ++m_samples;

    emit gameStateChanged(m_stable, milestones, m_last, nowMs);
}

} // namespace whalepet::viewmodel
