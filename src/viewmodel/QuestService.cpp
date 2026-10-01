#include "viewmodel/QuestService.h"

#include "core/Quests.h"
#include "model/Database.h"
#include "model/DiaryRepo.h"

#include <QDateTime>

namespace whalepet::viewmodel {

namespace {

qint64 nowOrCurrent(qint64 nowMs)
{
    return nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
}

QString dayKeyOf(qint64 nowMs)
{
    return QString::fromStdString(core::dayKey(nowMs));
}

} // namespace

QuestService::QuestService(model::Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_repo(std::make_unique<model::QuestRepo>(db))
    , m_diary(std::make_unique<model::DiaryRepo>(db))
{
}

QuestService::~QuestService() = default;

bool QuestService::load(qint64 nowMs)
{
    m_slots.clear();
    m_dayKey.clear();
    if (m_db == nullptr || !m_db->isOpen() || m_repo == nullptr) {
        return false;
    }

    const qint64 now = nowOrCurrent(nowMs);
    const QString today = dayKeyOf(now);
    const QList<model::QuestSlot> stored = m_repo->loadAll();

    if (!stored.isEmpty() && stored.first().dayKey == today) {
        m_slots = stored;
        m_dayKey = today;
        emit slotsChanged();
        return true;
    }

    // 跨天（或首次）：判断上一天是否 3 槽全部领取，用于「全勤」成就的连续计数
    bool previousDayFull = !stored.isEmpty();
    for (const model::QuestSlot &s : stored) {
        if (!s.claimed) {
            previousDayFull = false;
            break;
        }
    }

    buildTodaySlots(now);
    if (!stored.isEmpty()) {
        emit dayRolled(previousDayFull);
    }
    return true;
}

void QuestService::refreshForToday(qint64 nowMs)
{
    const qint64 now = nowOrCurrent(nowMs);
    const QString today = dayKeyOf(now);
    if (!m_slots.isEmpty() && m_dayKey == today) {
        return;
    }

    bool previousDayFull = false;
    if (!m_slots.isEmpty() && m_dayKey != today) {
        previousDayFull = allClaimed();
    }

    buildTodaySlots(now);
    if (!m_slots.isEmpty() && previousDayFull) {
        emit dayRolled(true);
    }
}

void QuestService::buildTodaySlots(qint64 nowMs)
{
    const std::string today = core::dayKey(nowMs);
    const core::QuestDef *picked[core::kQuestSlotCount] = {nullptr, nullptr, nullptr};
    const int count = core::pickDailyQuests(today.c_str(), picked, core::kQuestSlotCount);

    m_slots.clear();
    for (int i = 0; i < count; ++i) {
        if (picked[i] == nullptr) {
            continue;
        }
        model::QuestSlot slot;
        slot.id = QString::fromLatin1(picked[i]->id);
        slot.slot = i;
        slot.progress = 0;
        slot.target = picked[i]->target;
        slot.done = false;
        slot.claimed = false;
        slot.dayKey = QString::fromStdString(today);
        m_slots.append(slot);
    }
    m_dayKey = QString::fromStdString(today);

    if (m_repo != nullptr) {
        m_repo->replaceAll(m_slots);
    }
    emit slotsChanged();
}

bool QuestService::interactionMatches(core::Interaction type, core::QuestMetric metric)
{
    switch (type) {
    case core::Interaction::Pat:
        return metric == core::QuestMetric::Pat;
    case core::Interaction::Belly:
        return metric == core::QuestMetric::Belly;
    case core::Interaction::Tail:
        return metric == core::QuestMetric::Tail;
    case core::Interaction::Poke:
        return metric == core::QuestMetric::Poke;
    case core::Interaction::Feed:
        return metric == core::QuestMetric::Feed;
    case core::Interaction::Praise:
        return metric == core::QuestMetric::Praise;
    case core::Interaction::Triple:
        return metric == core::QuestMetric::Triple;
    case core::Interaction::Signin:
        return metric == core::QuestMetric::Signin;
    }
    return false;
}

bool QuestService::reportInteraction(core::Interaction type, qint64 nowMs)
{
    if (m_slots.isEmpty()) {
        return false;
    }

    const qint64 now = nowOrCurrent(nowMs);
    if (m_dayKey != dayKeyOf(now)) {
        refreshForToday(now);
        if (m_slots.isEmpty()) {
            return false;
        }
    }

    bool changed = false;
    for (model::QuestSlot &slot : m_slots) {
        if (slot.claimed || slot.progress >= slot.target) {
            continue;
        }
        const core::QuestDef *def = core::findQuest(slot.id.toUtf8().constData());
        if (def == nullptr || !interactionMatches(type, def->metric)) {
            continue;
        }

        ++slot.progress;
        const bool doneNow = slot.progress >= slot.target;
        slot.done = doneNow;
        if (m_repo != nullptr) {
            m_repo->updateProgress(slot.id, slot.progress, slot.done);
        }
        changed = true;

        if (doneNow) {
            const QString name = QString::fromUtf8(def->name);
            // 「今日签到」任务的完成 = 签到本身，而签到已由 SigninService 单独记入日记
            // （kind = "signin"）。日记 UI 只展示 detail，若此处再按任务记一条，
            // 同一次签到会出现两条「今日签到」。故签到类任务不再重复记日记（见 traps-P4 TRAP-P4-006）。
            if (m_diary != nullptr && def->metric != core::QuestMetric::Signin) {
                m_diary->appendDaily(QStringLiteral("quest"), name, now);
            }
            emit questDone(slot.id, name);
        }
    }

    if (changed) {
        emit slotsChanged();
    }
    return changed;
}

bool QuestService::claim(int slotIndex, qint64 nowMs)
{
    if (slotIndex < 0 || slotIndex >= m_slots.size()) {
        return false;
    }

    model::QuestSlot &slot = m_slots[slotIndex];
    if (!slot.done || slot.claimed) {
        return false;
    }
    if (m_repo == nullptr || !m_repo->markClaimed(slot.id)) {
        return false; // DB 条件更新失败 = 已经领过
    }

    const qint64 now = nowOrCurrent(nowMs);
    slot.claimed = true;

    const core::QuestDef *def = core::findQuest(slot.id.toUtf8().constData());
    const int mood = def != nullptr ? def->rewardMood : 0;
    const int affinity = def != nullptr ? def->rewardAffinity : 0;
    const QString name = def != nullptr ? QString::fromUtf8(def->name) : slot.id;

    if (m_diary != nullptr) {
        m_diary->appendDaily(QStringLiteral("quest"), name, now);
    }
    emit rewardGranted(mood, affinity, name);
    emit slotsChanged();
    return true;
}

int QuestService::claimedCount() const
{
    int n = 0;
    for (const model::QuestSlot &s : m_slots) {
        if (s.claimed) {
            ++n;
        }
    }
    return n;
}

bool QuestService::allClaimed() const
{
    if (m_slots.isEmpty()) {
        return false;
    }
    for (const model::QuestSlot &s : m_slots) {
        if (!s.claimed) {
            return false;
        }
    }
    return true;
}

} // namespace whalepet::viewmodel
