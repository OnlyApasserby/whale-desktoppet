#include "viewmodel/GameCompanionService.h"

#include "core/MiniGameCompanion.h"

#include <QDateTime>
#include <QTimer>

namespace whalepet::viewmodel {

GameCompanionService::GameCompanionService(QObject *parent)
    : QObject(parent), m_rules(core::GameCompanionRules())
{
}

GameCompanionService::~GameCompanionService()
{
    // 析构即停用：确保不遗留采样定时器与数据源资源。
    stop();
}

void GameCompanionService::setSourceFactory(SourceFactory factory)
{
    m_factory = std::move(factory);
}

bool GameCompanionService::start(QString *error)
{
    stop(); // 幂等：重复启用先释放上一轮资源

    if (m_factory == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("未配置陪玩数据源（EX3 起外部游戏陪玩已移除；"
                                   "小游戏陪玩数据源将在 EX4 接入）");
        }
        return false;
    }

    std::unique_ptr<IGameCompanionSource> source = m_factory(error);
    if (source == nullptr) {
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("无法为当前配置创建陪玩数据源");
        }
        return false;
    }
    if (!source->attach(error)) {
        source->detach();
        return false;
    }

    m_source = std::move(source);
    m_rules = core::GameCompanionRules();
    m_last = core::GameSample();
    m_prev = core::GameSample();
    m_snapPrev = core::GameSnapshot();
    m_stable = core::GameCompanionSample();
    m_haveLast = false;
    m_haveSnap = false;
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
    if (m_source != nullptr) {
        m_source->detach();
        m_source.reset();
    }
    m_running = false;
    // 复位判定，避免下次启用沿用旧状态（上报方需重新上报，见 PetStateMachine::reset）
    m_last = core::GameSample();
    m_prev = core::GameSample();
    m_snapPrev = core::GameSnapshot();
    m_stable = core::GameCompanionSample();
    m_haveLast = false;
    m_haveSnap = false;
}

void GameCompanionService::tick()
{
    if (!m_running || m_source == nullptr) {
        return;
    }

    // 【EX4】优先走中立快照通道（小游戏陪玩）：数据源提供时按 core::MiniGameCompanion 判定。
    // 不提供（默认）则回落到既有 RPG 采样路径（read），既有数据源与用例零回归。
    core::GameSnapshot snapshot;
    if (m_source->readSnapshot(&snapshot, nullptr)) {
        onSnapshot(snapshot, QDateTime::currentMSecsSinceEpoch());
        return;
    }

    core::GameSample sample;
    QString error;
    const bool ok = m_source->read(&sample, &error);
    if (!ok) {
        // 读取失败：照实回报「不可用」（不伪造数据）；数据源失效则停用并告知上层
        sample.available = false;
        onSample(sample, QDateTime::currentMSecsSinceEpoch());
        if (m_source != nullptr && m_source->invalidated()) {
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

void GameCompanionService::onSnapshot(const core::GameSnapshot &snapshot, qint64 nowMs)
{
    core::GameSnapshot current = snapshot;
    current.nowMs = nowMs;

    // 中立判定（EX4）：与 RPG 路径共用同一套置信度阈值 / 最短驻留 / Unknown 立即生效。
    const core::GameCompanionSample stable = core::miniGameEvaluate(current, m_stable);
    // 边沿里程碑：仅相邻两轮比较；首轮无上一轮 → 不产生（避免「启动即播报」误报）
    const core::GameMilestoneSet milestones =
        m_haveSnap ? core::miniGameMilestones(current, m_snapPrev) : core::GameMilestoneSet();

    if (stable.mood != m_stable.mood) {
        ++m_changes;
    }
    m_stable = stable;
    m_snapPrev = current;
    // 折算为管线载体：downstream（信号 / Context 投影）按 GameSample 读取 available / specialScene。
    m_last = core::gameSampleFromSnapshot(current);
    m_haveSnap = true;
    ++m_samples;

    emit gameStateChanged(m_stable, milestones, m_last, nowMs);
}

} // namespace whalepet::viewmodel
