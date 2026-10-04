#include "viewmodel/DialogueService.h"

#include "core/DialoguePoseRules.h"
#include "core/GrowthRules.h"
#include "core/LineTable.h"
#include "model/Database.h"
#include "viewmodel/WeatherService.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTimer>

namespace whalepet::viewmodel {

namespace {

const QString kKeySensitiveDay = QStringLiteral("dialogue.sensitive_day");
const QString kKeySensitiveUsed = QStringLiteral("dialogue.sensitive_used_today");

const char *reasonOf(const core::DialogueOption &option)
{
    return option.reason;
}

} // namespace

DialogueService::DialogueService(core::LineTable *lines, QObject *parent)
    : QObject(parent)
    , m_lines(lines)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &DialogueService::onTimer);
}

DialogueService::~DialogueService() = default;

void DialogueService::setDatabase(model::Database *db)
{
    m_db = db;
    if (m_db != nullptr && m_db->isOpen()) {
        m_sensitiveDay = m_db->meta(kKeySensitiveDay);
        m_sensitiveUsedToday = m_db->meta(kKeySensitiveUsed, QStringLiteral("0")).toInt();
    }
    refreshDay();
}

void DialogueService::refreshDay()
{
    const QString today = QString::fromStdString(core::dayKey(QDateTime::currentMSecsSinceEpoch()));
    if (m_sensitiveDay == today) {
        return;
    }
    m_sensitiveDay = today;
    m_sensitiveUsedToday = 0;
    persistSensitiveUsage();
}

void DialogueService::persistSensitiveUsage()
{
    if (m_db == nullptr || !m_db->isOpen()) {
        return;
    }
    m_db->setMeta(kKeySensitiveDay, m_sensitiveDay);
    m_db->setMeta(kKeySensitiveUsed, QString::number(m_sensitiveUsedToday));
}

int DialogueService::affinity() const
{
    return (m_affinity != nullptr) ? m_affinity() : 0;
}

bool DialogueService::sensitiveUnlocked() const
{
    return affinity() >= core::kSensitiveUnlockAffinity;
}

int DialogueService::sensitiveRemaining() const
{
    const int left = core::kSensitiveDailyLimit - m_sensitiveUsedToday;
    return left > 0 ? left : 0;
}

bool DialogueService::consumeSensitiveQuota()
{
    if (sensitiveRemaining() <= 0) {
        return false;
    }
    ++m_sensitiveUsedToday;
    persistSensitiveUsage();
    emit quotaChanged(m_sensitiveUsedToday, core::kSensitiveDailyLimit);
    return true;
}

void DialogueService::setIntervalRange(int minMs, int maxMs)
{
    if (minMs <= 0 || maxMs < minMs) {
        return;
    }
    m_minIntervalMs = minMs;
    m_maxIntervalMs = maxMs;
}

std::size_t DialogueService::loadFromText(const std::string &content)
{
    m_table.clear();
    m_options.clear();
    m_recentRandomIds.clear();

    const std::size_t count = m_table.loadFromText(content);

    // 回答文本登记进台词表：`dialogue.<id>.<slot>` → 文本。
    // 同一 slot 的多条候选共享同一个 key，由 LineTable::pick 随机取一条。
    if (m_lines != nullptr) {
        for (const core::DialogueQuestion &question : m_table.questions()) {
            for (const core::DialogueAnswer &answer : question.answers) {
                m_lines->addLine(core::dialogueSceneKey(question.id, answer.slot), answer.text);
            }
        }
    }
    if (count == 0) {
        qWarning() << "[DialogueService] 预设对话语料为空或格式不合法，问答功能降级为不可用";
    }
    return count;
}

std::size_t DialogueService::loadBundled()
{
    QFile file(QStringLiteral(":/lines/dialogue.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[DialogueService] 预设对话语料缺失，问答功能降级为不可用:"
                   << file.fileName();
        return 0;
    }
    return loadFromText(file.readAll().toStdString());
}

void DialogueService::start()
{
    if (m_timer == nullptr || m_timer->isActive()) {
        return;
    }
    scheduleNext();
}

void DialogueService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    cancel();
}

bool DialogueService::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

void DialogueService::scheduleNext()
{
    if (m_timer == nullptr) {
        return;
    }
    const int span = (m_maxIntervalMs > m_minIntervalMs) ? (m_maxIntervalMs - m_minIntervalMs) : 0;
    const int delay = m_minIntervalMs + ((span > 0) ? m_rng.nextInt(span + 1) : 0);
    m_timer->start(delay);
}

void DialogueService::onTimer()
{
    if (!offerOptions(false)) {
        scheduleNext(); // 门槛不满足 / 语料不可用：顺延一轮，不打扰
    }
}

bool DialogueService::weatherAvailable() const
{
    // 未配置 key / 城市 = 完全离线 → 天气问题选项不可用（需求：没有 API 时不可用）
    return m_weather != nullptr && m_weather->configured();
}

QStringList DialogueService::optionTexts() const
{
    QStringList out;
    for (const core::DialogueOption &option : m_options) {
        if (option.question != nullptr) {
            out << QString::fromStdString(option.question->text);
        } else {
            out << QString(); // 占位（UI 显示为空并禁用）
        }
    }
    return out;
}

QList<bool> DialogueService::optionEnabled() const
{
    QList<bool> out;
    for (const core::DialogueOption &option : m_options) {
        out << option.available;
    }
    return out;
}

QStringList DialogueService::optionHints() const
{
    QStringList out;
    for (const core::DialogueOption &option : m_options) {
        if (option.available) {
            out << QString();
            continue;
        }
        const char *reason = reasonOf(option);
        QString hint = (reason != nullptr) ? QString::fromUtf8(reason) : QString();
        if (option.kind == core::DialogueOptionKind::Sensitive
            && !sensitiveUnlocked()
            && option.question != nullptr) {
            hint += QStringLiteral("（当前好感度 %1 / 5000）").arg(affinity());
        }
        out << hint;
    }
    return out;
}

bool DialogueService::offerOptions(bool force)
{
    if (!m_options.empty()) {
        return false; // 面板已打开（有一批问题在等选择）
    }
    if (!available()) {
        return false;
    }
    if (!force && m_canAsk && !m_canAsk()) {
        return false;
    }

    refreshDay(); // 跨天则配额清零

    core::DialogueOptionRequest request;
    request.weatherAvailable = weatherAvailable();
    request.sensitiveUnlocked = sensitiveUnlocked();
    request.sensitiveQuotaLeft = sensitiveRemaining() > 0;
    request.randomCount = core::kDialogueRandomOptionCount;

    m_options = core::buildDialogueOptions(m_table, &m_rng, request, m_recentRandomIds);
    if (m_options.empty()) {
        return false;
    }

    // 记录本轮随机题（下一轮优先避开），并保证面板至少有一个可选项
    m_recentRandomIds.clear();
    bool anyAvailable = false;
    for (const core::DialogueOption &option : m_options) {
        if (option.question != nullptr) {
            m_recentRandomIds.push_back(option.question->id);
        }
        anyAvailable = anyAvailable || option.available;
    }
    if (!anyAvailable) {
        m_options.clear();
        m_recentRandomIds.clear();
        return false;
    }

    emit optionsOffered(optionTexts(), optionEnabled(), optionHints());
    return true;
}

void DialogueService::cancel()
{
    m_options.clear();
}

QString DialogueService::poseForOption(const core::DialogueOption &option)
{
    if (option.question == nullptr) {
        return QString::fromLatin1(core::kDialogueNormalPose);
    }
    switch (option.question->category) {
    case core::DialogueCategory::Weather: {
        const core::WeatherKind kind =
            (m_weather != nullptr) ? m_weather->kind() : core::WeatherKind::Unknown;
        // 2026-10-04 立绘激活 22：盛夏晴天立绘为 daily-melt，其余沿用 weatherKindPose
        const int month = QDate::currentDate().month();
        return QString::fromLatin1(core::weatherKindPoseForMonth(kind, month));
    }
    case core::DialogueCategory::Sensitive: {
        std::size_t count = 0;
        const char *const *poses = core::dialogueSensitivePoses(count);
        const QByteArray avoid = m_lastSensitivePose.toLatin1();
        const char *pose = core::pickDialoguePose(
            poses, count, &m_rng, m_lastSensitivePose.isEmpty() ? nullptr : avoid.constData());
        if (pose == nullptr) {
            return m_lastSensitivePose; // 池中只有一张且正是上一张 → 保留
        }
        m_lastSensitivePose = QString::fromLatin1(pose);
        return m_lastSensitivePose;
    }
    case core::DialogueCategory::Choice: {
        std::size_t count = 0;
        const char *const *poses = core::dialogueChoicePoses(count);
        const QByteArray avoid = m_lastChoicePose.toLatin1();
        const char *pose = core::pickDialoguePose(
            poses, count, &m_rng, m_lastChoicePose.isEmpty() ? nullptr : avoid.constData());
        if (pose == nullptr) {
            return m_lastChoicePose;
        }
        m_lastChoicePose = QString::fromLatin1(pose);
        return m_lastChoicePose;
    }
    case core::DialogueCategory::Normal:
        return QString::fromLatin1(core::kDialogueNormalPose);
    }
    return QString::fromLatin1(core::kDialogueNormalPose);
}

bool DialogueService::choose(int index)
{
    if (index < 0 || index >= static_cast<int>(m_options.size())) {
        return false;
    }
    const core::DialogueOption option = m_options[index];
    if (!option.available || option.question == nullptr) {
        return false; // 禁用项不可选择（UI 已禁用，这里再守一道）
    }

    // 敏感题：先消耗每日配额（用尽 → 拒绝，不输出）
    if (option.kind == core::DialogueOptionKind::Sensitive && !consumeSensitiveQuota()) {
        return false;
    }

    const char *weatherSlot = nullptr;
    if (option.kind == core::DialogueOptionKind::Weather) {
        const core::WeatherKind kind =
            (m_weather != nullptr) ? m_weather->kind() : core::WeatherKind::Unknown;
        weatherSlot = core::weatherKindId(kind);
    }

    const std::string slot = core::pickAnswerSlot(*option.question, &m_rng, weatherSlot);
    if (slot.empty()) {
        return false; // 该问题没有任何可用回答：不应发生（构建时已过滤），守一道
    }

    const QString pose = poseForOption(option);
    const std::string sceneKey = core::dialogueSceneKey(option.question->id, slot);

    cancel(); // 本批已选中 → 清空，等待下一次 offerOptions
    emit answered(pose, QString::fromStdString(sceneKey));
    scheduleNext();
    return true;
}

} // namespace whalepet::viewmodel
