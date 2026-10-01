#include "viewmodel/GrowthService.h"

#include "model/Database.h"
#include "model/PetStateRepo.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace whalepet::viewmodel {

namespace {

const QString kLastSigninDayKey = QStringLiteral("last_signin_day");

model::PetStateData makeDefaultState(qint64 nowMs)
{
    model::PetStateData s;
    s.level = core::kDefaultLevel;
    s.exp = core::kDefaultExp;
    s.coins = 0;
    s.mood = core::kDefaultMood;
    s.affinity = core::kDefaultAffinity;
    s.satiety = core::kDefaultSatiety;
    s.bondLevel = core::kDefaultBondLevel;
    s.companionMs = 0;
    s.streakDays = core::kDefaultStreakDays;
    s.lastActiveMs = nowMs;
    s.updatedMs = nowMs;
    return s;
}

} // namespace

GrowthService::GrowthService(model::Database *db, QObject *parent)
    : QObject(parent), m_db(db)
{
    if (m_db != nullptr) {
        m_repo = std::make_unique<model::PetStateRepo>(m_db);
    }
}

GrowthService::~GrowthService() = default;

qint64 GrowthService::currentMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool GrowthService::load()
{
    const qint64 nowMs = currentMs();
    bool existed = false;

    if (m_repo != nullptr) {
        existed = m_repo->load(m_state);
    }

    if (!existed) {
        m_state = makeDefaultState(nowMs);
        m_lastSigninDay.clear();
        m_satietyAccumMs = 0;
        m_lastSettleMs = nowMs;
        store();
        qInfo() << "[Growth] 无历史记录，已写入默认养成状态";
        m_loaded = true;
        emit stateChanged();
        return false;
    }

    m_lastSigninDay = (m_db != nullptr) ? m_db->meta(kLastSigninDayKey) : QString();
    m_satietyAccumMs = 0;
    m_lastSettleMs = nowMs;

    // 离线衰减：程序未运行期间饱食仍会下降（whale 按时间戳推算式）
    const qint64 offlineMs = nowMs - m_state.lastActiveMs;
    if (offlineMs > 0) {
        const int before = m_state.satiety;
        decaySatiety(offlineMs);
        if (m_state.satiety != before) {
            m_state.updatedMs = nowMs;
            store();
        }
    }

    // 夹取历史脏数据（人工改库 / 旧版本遗留）
    m_state.level = core::levelForExp(m_state.exp);
    m_state.mood = core::clampMood(m_state.mood);
    m_state.satiety = core::clampSatiety(m_state.satiety);
    m_state.affinity = core::clampAffinity(m_state.affinity);
    m_state.bondLevel = core::levelForExp(m_state.affinity);

    m_loaded = true;
    emit stateChanged();
    return true;
}

bool GrowthService::flush()
{
    m_state.lastActiveMs = currentMs();
    m_state.updatedMs = m_state.lastActiveMs;
    return store();
}

bool GrowthService::store()
{
    if (m_repo == nullptr) {
        return false;
    }
    return m_repo->save(m_state);
}

void GrowthService::startTicking(int intervalMs)
{
    if (m_db == nullptr) {
        qWarning() << "[Growth] 无数据库连接，定时结算仍可用但不落盘";
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::CoarseTimer);
        connect(m_timer, &QTimer::timeout, this, [this]() { settle(0); });
    }
    m_lastSettleMs = currentMs();
    m_timer->start(intervalMs > 0 ? intervalMs : static_cast<int>(core::kGrowthTickMs));
}

void GrowthService::stopTicking()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

void GrowthService::decaySatiety(qint64 elapsedMs)
{
    if (elapsedMs <= 0) {
        return;
    }
    m_satietyAccumMs += elapsedMs;
    const qint64 points = m_satietyAccumMs / core::kMsPerSatietyPoint;
    if (points <= 0) {
        return;
    }
    m_satietyAccumMs -= points * core::kMsPerSatietyPoint;

    qint64 remain = points;
    int satiety = m_state.satiety;
    while (remain > 0 && satiety > 0) {
        --satiety;
        --remain;
    }
    m_state.satiety = core::clampSatiety(satiety);
    if (remain > 0) {
        // 已经掉到底：丢弃余量，避免长时间离线后一次性掉穿
        m_satietyAccumMs = 0;
    }
}

void GrowthService::settle(qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : currentMs();
    qint64 elapsed = now - m_lastSettleMs;
    if (elapsed < 0) {
        // 系统时间回拨：只重置基准，不做结算
        m_lastSettleMs = now;
        return;
    }
    m_lastSettleMs = now;

    if (elapsed <= 0) {
        return;
    }

    const int prevSatiety = m_state.satiety;
    decaySatiety(elapsed);

    // 陪伴时长只在程序运行期间累计
    m_state.companionMs += elapsed;
    m_state.lastActiveMs = now;

    if (m_state.satiety != prevSatiety) {
        m_state.updatedMs = now;
    }
    store();

    if (m_state.satiety != prevSatiety) {
        emit stateChanged();
    }
}

void GrowthService::applyDelta(const core::GrowthDelta &delta, qint64 nowMs)
{
    const int prevLevel = m_state.level;
    const int prevBond = core::levelForExp(m_state.affinity);

    m_state.mood = core::clampMood(m_state.mood + delta.mood);

    // 好感夹取后的**实际增量**同时计入 exp，保证 level 与 affinity 数值一致
    const int newAffinity = core::clampAffinity(m_state.affinity + delta.affinity);
    const int gained = newAffinity - m_state.affinity;
    m_state.affinity = newAffinity;
    m_state.exp += gained;

    if (delta.satiety != 0) {
        m_state.satiety = core::clampSatiety(m_state.satiety + delta.satiety);
        // 投喂补满后，衰减余量清零，避免「刚喂饱立刻掉点」
        if (delta.satiety > 0) {
            m_satietyAccumMs = 0;
        }
    }

    m_state.lastActiveMs = nowMs;
    m_state.updatedMs = nowMs;

    refreshDerived(nowMs, prevLevel, prevBond);
    store();
    emit stateChanged();
}

void GrowthService::refreshDerived(qint64 nowMs, int prevLevel, int prevBond)
{
    Q_UNUSED(nowMs);

    const int level = core::levelForExp(m_state.exp);
    const int bond = core::levelForExp(m_state.affinity);
    m_state.level = level;
    m_state.bondLevel = bond;

    if (level > prevLevel) {
        qInfo() << "[Growth] 升级:" << prevLevel << "->" << level;
        emit levelUp(level);
    }
    if (bond > prevBond) {
        qInfo() << "[Growth] 羁绊提升:" << prevBond << "->" << bond;
        emit bondUp(bond);
    }
}

void GrowthService::applyInteraction(core::Interaction type, qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : currentMs();
    applyDelta(core::deltaFor(type), now);
}

void GrowthService::grantReward(int mood, int affinity, qint64 nowMs)
{
    if (mood == 0 && affinity == 0) {
        return;
    }
    const qint64 now = (nowMs > 0) ? nowMs : currentMs();
    core::GrowthDelta delta;
    delta.mood = mood;
    delta.affinity = affinity;
    applyDelta(delta, now);
}

bool GrowthService::signIn(qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : currentMs();

    bool sameDay = false;
    const int newStreak =
        core::nextStreakDays(m_state.streakDays, m_lastSigninDay.toStdString(), now, &sameDay);
    if (sameDay) {
        return false;
    }

    m_state.streakDays = newStreak;
    m_lastSigninDay = QString::fromStdString(core::dayKey(now));
    if (m_db != nullptr) {
        m_db->setMeta(kLastSigninDayKey, m_lastSigninDay);
    }

    applyDelta(core::deltaFor(core::Interaction::Signin), now);
    emit signedIn(m_state.streakDays);
    qInfo() << "[Growth] 签到成功，连续天数 =" << m_state.streakDays;
    return true;
}

void GrowthService::resetToDefaults(qint64 nowMs)
{
    const qint64 now = (nowMs > 0) ? nowMs : currentMs();
    m_state = makeDefaultState(now);
    m_lastSigninDay.clear();
    m_satietyAccumMs = 0;
    m_lastSettleMs = now;
    if (m_db != nullptr) {
        m_db->setMeta(kLastSigninDayKey, QString());
    }
    store();
    emit stateChanged();
}

} // namespace whalepet::viewmodel
