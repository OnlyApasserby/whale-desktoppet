#pragma once

// 养成数值编排服务 —— 落实 docs/GAMEPLAY.md §1 与 docs/ROADMAP-P3.md「P3 设计补充」。
//
// 职责：
//   - 内存中的养成状态（level/exp/mood/affinity/satiety/bond/companion/streak）
//   - 状态变更即时落盘（DATA-MODEL §4 写策略）
//   - 升级 / 羁绊提升 / 签到 事件广播（供 PetController 驱动状态机）
//
// 纯数值规则全部委托 core::GrowthRules（零 Qt，可脱 UI 单测）；
// 本类只做「编排 + 持久化」，不含任何界面依赖。

#include "core/GrowthRules.h"
#include "model/PetStateData.h"

#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace whalepet::model {
class Database;
class PetStateRepo;
} // namespace whalepet::model

namespace whalepet::viewmodel {

class GrowthService : public QObject {
    Q_OBJECT

public:
    explicit GrowthService(model::Database *db, QObject *parent = nullptr);
    ~GrowthService() override;

    // 从库中载入；无记录时写入默认值。返回 true 表示读到了已有记录。
    bool load();

    // 强制落盘（退出前调用）
    bool flush();

    const model::PetStateData &state() const { return m_state; }
    core::BondUnlocks bondUnlocks() const { return core::bondUnlocks(m_state.level); }

    // 定时结算：饱食衰减 + 陪伴时长累计（默认 kGrowthTickMs）
    void startTicking(int intervalMs = static_cast<int>(core::kGrowthTickMs));
    void stopTicking();

    // 应用一次交互；nowMs <= 0 表示取当前时间
    void applyInteraction(core::Interaction type, qint64 nowMs = 0);

    // 每日签到（跨天幂等）：返回 true 表示本次真的签到成功
    bool signIn(qint64 nowMs = 0);

    // 时间结算（公开以便单测直接注入时间）
    void settle(qint64 nowMs);

    // 重置养成数据（默认值 + 落库）
    void resetToDefaults(qint64 nowMs = 0);

signals:
    void stateChanged();
    void levelUp(int level);
    void bondUp(int level);
    void signedIn(int streakDays);

private:
    static qint64 currentMs();
    // 返回实际生效的（夹取后的）变化量，用于决定是否落盘/广播
    void applyDelta(const core::GrowthDelta &delta, qint64 nowMs);
    void refreshDerived(qint64 nowMs, int prevLevel, int prevBond);
    void decaySatiety(qint64 elapsedMs);
    bool store();

    model::Database *m_db = nullptr;
    std::unique_ptr<model::PetStateRepo> m_repo;
    QTimer *m_timer = nullptr;

    model::PetStateData m_state;

    // 不足 1 点的饱食衰减余量（毫秒）；不落库，重启最多丢失 1 点衰减
    qint64 m_satietyAccumMs = 0;
    qint64 m_lastSettleMs = 0;

    // meta.last_signin_day（YYYY-M-D）
    QString m_lastSigninDay;

    bool m_loaded = false;
};

} // namespace whalepet::viewmodel
