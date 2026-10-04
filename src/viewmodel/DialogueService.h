#pragma once

// DialogueService（P8 问答系统）：**用户提问 → 鲸鱼娘回答** 的编排层 —— docs/DIALOGUE.md。
//
// 职责边界（刻意的窄接口）：
//   * **语料**：读 :/lines/dialogue.txt 交给 core::PresetDialogueTable 解析，
//     并把回答登记进 core::LineTable（`dialogue.<id>.<slot>`），复用既有
//     `speak → LineTable::pick → SpeechBubble` 管线输出**文字**回答；
//   * **选项池**：core/DialogueOptions.h 负责「五选一」的构建（固定天气槽 + 固定敏感槽
//     + 3 个每次刷新的随机题）与「从三个预设回答中随机取一个」；
//   * **配额**：敏感 / 私密题的好感度门槛与**每日 3 次**由本类判定与落库
//     （`model::Database::meta`，跨天用 core::dayKey 清零，与 MiniGameService 同构）；
//   * **节奏**：本类只负责「什么时候可以开口问」，门槛回调由 View 注入
//     （静息 / 非深夜 / 无面板占用 / 桌宠可见）。
//
// 天气：由 WeatherService 提供类型判定；未配置 key / 城市时**天气槽位禁用**（不可点）。

#include "core/DialogueOptions.h"
#include "core/PresetDialogue.h"
#include "core/WeatherRules.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class QTimer;

namespace whalepet::core {
class LineTable;
} // namespace whalepet::core

namespace whalepet::model {
class Database;
} // namespace whalepet::model

namespace whalepet::viewmodel {

class WeatherService;

class DialogueService : public QObject {
    Q_OBJECT
public:
    static constexpr int kDefaultMinIntervalMs = 15 * 60 * 1000; // 提醒可提问的最小间隔
    static constexpr int kDefaultMaxIntervalMs = 30 * 60 * 1000; // 提醒可提问的最大间隔

    // lines 不接管所有权（PetController 持有同一份；为空则只切立绘、不输出文字）
    explicit DialogueService(core::LineTable *lines = nullptr, QObject *parent = nullptr);
    ~DialogueService() override;

    // 读取内置语料（:/lines/dialogue.txt）。返回解析出的问题数；缺失只降级为不可用。
    std::size_t loadBundled();
    // 直接喂文本（单测 / 外部语料）
    std::size_t loadFromText(const std::string &content);

    bool available() const { return !m_table.empty(); }
    std::size_t questionCount() const { return m_table.size(); }
    core::PresetDialogueTable &table() { return m_table; }

    void setLineTable(core::LineTable *lines) { m_lines = lines; }
    void setWeatherService(WeatherService *weather) { m_weather = weather; }
    // 敏感题每日配额（meta 表；为空 = 不限额，仅按好感度门槛判定）
    void setDatabase(model::Database *db);
    // 好感度来源（由 View 注入 GrowthService 的当前 affinity）
    void setAffinityProvider(std::function<int()> provider) { m_affinity = std::move(provider); }
    // 门槛（由 View 注入：静息 / 非深夜 / 无面板占用 / 桌宠可见）。为空视为始终允许。
    void setCanAsk(std::function<bool()> canAsk) { m_canAsk = std::move(canAsk); }
    void setIntervalRange(int minMs, int maxMs);

    void start();
    void stop();
    bool running() const;

    // 打开「主人的问题」面板（整理五个可选问题）。不满足门槛且 !force → false。
    bool offerOptions(bool force = false);
    // 面板被关闭 / 超时：放弃本次选择（不消耗任何配额）
    void cancel();

    bool offering() const { return !m_options.empty(); }
    const std::vector<core::DialogueOption> &options() const { return m_options; }
    QStringList optionTexts() const;    // 五个问题文本（不可用项也占位）
    QList<bool> optionEnabled() const;  // 逐项可用性（UI 据此禁用按钮）
    QStringList optionHints() const;    // 逐项提示（不可用原因；可用项为空）

    // 用户选择第 index 个问题 → 从该问题的三个预设回答中随机取一个并**输出文字**
    // （经 answered → PetController → 气泡）。敏感题在此消耗一次每日配额。
    bool choose(int index);

    // 诊断 / UI
    bool weatherAvailable() const;
    bool sensitiveUnlocked() const;
    int sensitiveUsedToday() const { return m_sensitiveUsedToday; }
    int sensitiveRemaining() const;
    int affinity() const;

signals:
    // 面板应显示：questions 五个问题文本；enabled 逐项可用性；hints 不可用原因
    void optionsOffered(const QStringList &questions, const QList<bool> &enabled,
                        const QStringList &hints);
    // 鲸鱼娘的回答：pose（该问题类别的独立立绘）+ sceneKey（`dialogue.<id>.<slot>`）
    void answered(const QString &pose, const QString &sceneKey);
    // 敏感题配额变化（已用 / 上限）
    void quotaChanged(int usedToday, int limit);

private:
    void scheduleNext();
    void onTimer();
    void refreshDay();
    void persistSensitiveUsage();
    bool consumeSensitiveQuota();
    QString poseForOption(const core::DialogueOption &option);

    core::LineTable *m_lines = nullptr;
    WeatherService *m_weather = nullptr;
    model::Database *m_db = nullptr;

    core::PresetDialogueTable m_table;
    core::SystemRandom m_rng;

    QTimer *m_timer = nullptr;
    int m_minIntervalMs = kDefaultMinIntervalMs;
    int m_maxIntervalMs = kDefaultMaxIntervalMs;

    std::function<bool()> m_canAsk;
    std::function<int()> m_affinity;

    std::vector<core::DialogueOption> m_options;
    std::vector<std::string> m_recentRandomIds; // 上一轮出现的随机题（下次优先避开）
    QString m_lastSensitivePose;
    QString m_lastChoicePose;

    // 敏感题每日配额（跨天清零）
    QString m_sensitiveDay;
    int m_sensitiveUsedToday = 0;
};

} // namespace whalepet::viewmodel
