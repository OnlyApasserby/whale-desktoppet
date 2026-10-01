// P4 内容层单测：39 项成就判定 / 每日任务抽签与领取 / 周签到里程碑 / 成长日记去重与上限。
// 约定同 test_growth.cpp：内存库（Database::openMemory）跑服务，headless（QTEST_GUILESS_MAIN）。
// 参考：docs/ROADMAP-P4.md、docs/GAMEPLAY.md §3-6、docs/TESTING.md。

#include "core/Achievements.h"
#include "core/Calendar.h"
#include "core/GrowthRules.h"
#include "core/Quests.h"
#include "core/SigninRules.h"
#include "model/Database.h"
#include "model/DiaryRepo.h"
#include "model/QuestRepo.h"
#include "viewmodel/AchievementService.h"
#include "viewmodel/QuestService.h"
#include "viewmodel/SigninService.h"

#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QSignalSpy>
#include <QTime>
#include <QtTest>

#include <memory>

using namespace whalepet;
using namespace whalepet::core;

namespace {

constexpr qint64 kDay = 86400000LL;

std::unique_ptr<model::Database> makeDb()
{
    auto db = std::make_unique<model::Database>();
    const bool ok = db->openMemory();
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return db;
}

// 今天本地 hour 点的毫秒（用于让「深夜」判定可控、结果与运行时刻无关）
qint64 msAtLocalHour(int hour)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime at(now.date(), QTime(hour, 0, 0));
    return at.toMSecsSinceEpoch();
}

Interaction interactionForQuest(QuestMetric metric)
{
    switch (metric) {
    case QuestMetric::Signin: return Interaction::Signin;
    case QuestMetric::Pat: return Interaction::Pat;
    case QuestMetric::Belly: return Interaction::Belly;
    case QuestMetric::Tail: return Interaction::Tail;
    case QuestMetric::Poke: return Interaction::Poke;
    case QuestMetric::Feed: return Interaction::Feed;
    case QuestMetric::Praise: return Interaction::Praise;
    case QuestMetric::Triple: return Interaction::Triple;
    }
    return Interaction::Pat;
}

} // namespace

class TestContent : public QObject {
    Q_OBJECT

private slots:
    // ---- 纯规则 ----
    void achievementDefinitionsAreComplete();
    void achievementStatKeyMapping();
    void achievementSnapshotJudgement();
    void companionDaysConversion();
    void calendarWeekAndNightWindow();
    void dailyQuestPickIsDeterministic();
    void signinMilestoneTable();

    // ---- 成就服务 ----
    void achievementUnlocksInteractionsAndPersists();
    void achievementNightAndComeback();
    void achievementStateMetrics();
    void achievementQuestCounters();
    void achievementMiniGameReport();

    // ---- 每日任务 ----
    void questLoadsFixedAndPickedSlots();
    void questProgressAndClaimIsIdempotent();
    void questDayRollReportsFullAttendance();
    void questRepoConditionalUpdate();

    // ---- 周签到 ----
    void signinBoardTracksTodayAndMilestone();
    void signinResetsOnWeekChange();

    // ---- 成长日记 ----
    void diaryDeduplicatesWithinDayAndTrims();
};

void TestContent::achievementDefinitionsAreComplete()
{
    QCOMPARE(kAchievementCount, 39);
    QCOMPARE(achCategoryCount(AchCategory::Interaction), 10);
    QCOMPARE(achCategoryCount(AchCategory::Companion), 8);
    QCOMPARE(achCategoryCount(AchCategory::Growth), 8);
    QCOMPARE(achCategoryCount(AchCategory::Quest), 6);
    QCOMPARE(achCategoryCount(AchCategory::MiniGame), 7);

    // id 落库主键必须唯一（重复会让成就互相覆盖）
    QSet<QString> ids;
    for (const AchievementDef &d : kAchievements) {
        const QString id = QString::fromLatin1(d.id);
        QVERIFY2(!ids.contains(id), qPrintable(id));
        ids.insert(id);
        QVERIFY(d.name != nullptr && d.desc != nullptr && d.threshold > 0);
    }
    QCOMPARE(ids.size(), kAchievementCount);
}

void TestContent::achievementStatKeyMapping()
{
    QCOMPARE(QString::fromLatin1(achStatKey(AchMetric::PatCount)), QStringLiteral("stat.pat"));
    QCOMPARE(QString::fromLatin1(achStatKey(AchMetric::QuestDoneTotal)),
             QStringLiteral("stat.quest_done"));
    // 状态型指标不是计数器：返回 nullptr
    QVERIFY(achStatKey(AchMetric::Level) == nullptr);
    QVERIFY(achStatKey(AchMetric::StreakDays) == nullptr);
    QVERIFY(achStatKey(AchMetric::CompanionDays) == nullptr);
    // 小游戏（扫雷）为计数器 / 峰值型指标：均落在 meta 表的 stat.* 键
    QCOMPARE(QString::fromLatin1(achStatKey(AchMetric::MiniGamePlays)),
             QStringLiteral("stat.mg_plays"));
    QCOMPARE(QString::fromLatin1(achStatKey(AchMetric::MiniGameMaxChain)),
             QStringLiteral("stat.mg_max_chain"));
    QCOMPARE(QString::fromLatin1(achStatKey(AchMetric::MiniGamePlaysToday)),
             QStringLiteral("stat.mg_plays_today"));
}

void TestContent::achievementSnapshotJudgement()
{
    AchievementSnapshot s;
    const AchievementDef *tenPats = nullptr;
    const AchievementDef *hundredPats = nullptr;
    const AchievementDef *firstGame = nullptr;
    for (const AchievementDef &d : kAchievements) {
        if (QString::fromLatin1(d.id) == QStringLiteral("ten-pats")) {
            tenPats = &d;
        } else if (QString::fromLatin1(d.id) == QStringLiteral("hundred-pats")) {
            hundredPats = &d;
        } else if (QString::fromLatin1(d.id) == QStringLiteral("game-first")) {
            firstGame = &d;
        }
    }
    QVERIFY(tenPats != nullptr && hundredPats != nullptr && firstGame != nullptr);

    s.patCount = 9;
    QVERIFY(!achievementReached(*tenPats, s));
    s.patCount = 10;
    QVERIFY(achievementReached(*tenPats, s));
    QVERIFY(!achievementReached(*hundredPats, s));

    // 小游戏（扫雷）已落地为真实判定：完成一局即解锁「初次开玩」
    QCOMPARE(s.valueFor(AchMetric::MiniGamePlays), 0);
    QVERIFY(!achievementReached(*firstGame, s));
    s.miniGamePlays = 1;
    QVERIFY(achievementReached(*firstGame, s));
}

void TestContent::companionDaysConversion()
{
    QCOMPARE(companionDaysFromMs(0), 0);
    QCOMPARE(companionDaysFromMs(kCompanionMsPerDay - 1), 0);
    QCOMPARE(companionDaysFromMs(kCompanionMsPerDay), 1);
    QCOMPARE(companionDaysFromMs(kCompanionMsPerDay * 7 + 100), 7);
}

void TestContent::calendarWeekAndNightWindow()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int idx = dayIndexMondayFirst(now);
    QVERIFY(idx >= 0 && idx <= 6);

    // 平移 7 天：周内位置（dayIndex）不变，但 weekKey 必须前进一周
    QCOMPARE(dayIndexMondayFirst(now + kDay * 7), idx);
    QVERIFY(QString::fromStdString(weekKey(now))
            != QString::fromStdString(weekKey(now + kDay * 7)));

    // weekKey = 本周周一日期 = 今天 - idx 天
    QCOMPARE(QString::fromStdString(weekKey(now)),
             QString::fromStdString(dayKeyOffset(now, -idx)));
    QCOMPARE(QString::fromStdString(weekKey(now + kDay * 7)),
             QString::fromStdString(dayKeyOffset(now + kDay * 7, -idx)));

    // 深夜窗口 22:00–06:00
    QVERIFY(isLateNight(msAtLocalHour(23)));
    QVERIFY(isLateNight(msAtLocalHour(3)));
    QVERIFY(!isLateNight(msAtLocalHour(12)));
    QVERIFY(!isLateNight(msAtLocalHour(21)));
    QVERIFY(isLateNight(msAtLocalHour(22)));
    QVERIFY(!isLateNight(msAtLocalHour(6)));
}

void TestContent::dailyQuestPickIsDeterministic()
{
    const QuestDef *picked[kQuestSlotCount] = {};
    const int n = pickDailyQuests("2026-9-30", picked, kQuestSlotCount);
    QCOMPARE(n, kQuestSlotCount);
    // slot 0 固定为「今日签到」（always 项）
    QCOMPARE(QString::fromLatin1(picked[0]->id), QString::fromLatin1(alwaysQuest()->id));
    QCOMPARE(QString::fromLatin1(picked[0]->id), QStringLiteral("signin-1"));

    // 同日重复抽签结果一致（同一天重启后槽位不变）
    const QuestDef *again[kQuestSlotCount] = {};
    QCOMPARE(pickDailyQuests("2026-9-30", again, kQuestSlotCount), kQuestSlotCount);
    for (int i = 0; i < n; ++i) {
        QCOMPARE(QString::fromLatin1(again[i]->id), QString::fromLatin1(picked[i]->id));
    }

    // 同一天内不重复抽到同一条
    QSet<QString> ids;
    for (int i = 0; i < n; ++i) {
        ids.insert(QString::fromLatin1(picked[i]->id));
    }
    QCOMPARE(ids.size(), n);
}

void TestContent::signinMilestoneTable()
{
    QCOMPARE(kSigninWeekDays, 7);
    QCOMPARE(kSigninMilestoneCount, 3);
    QCOMPARE(kSigninMilestones[0].days, 1);
    QCOMPARE(kSigninMilestones[1].days, 3);
    QCOMPARE(kSigninMilestones[2].days, 7);
    // 位图三档必须互不相同，否则里程碑会互相覆盖
    QVERIFY(kSigninMilestones[0].bit != kSigninMilestones[1].bit);
    QVERIFY(kSigninMilestones[1].bit != kSigninMilestones[2].bit);
    QVERIFY(kSigninMilestones[0].bit != kSigninMilestones[2].bit);
}

void TestContent::achievementUnlocksInteractionsAndPersists()
{
    auto db = makeDb();
    viewmodel::AchievementService ach(db.get());
    ach.load();
    QCOMPARE(ach.unlockedCount(), 0);

    QSignalSpy spy(&ach, &viewmodel::AchievementService::unlocked);
    QVERIFY(spy.isValid());

    const qint64 noon = msAtLocalHour(12); // 固定白天：不触发「深夜陪伴」
    for (int i = 0; i < 10; ++i) {
        ach.reportInteraction(Interaction::Pat, noon);
    }
    QVERIFY(ach.isUnlocked(QStringLiteral("first-pat")));
    QVERIFY(ach.isUnlocked(QStringLiteral("ten-pats")));
    QVERIFY(!ach.isUnlocked(QStringLiteral("hundred-pats")));

    const int afterTen = ach.unlockedCount();
    // 再摸 10 次不重复解锁
    for (int i = 0; i < 10; ++i) {
        ach.reportInteraction(Interaction::Pat, noon);
    }
    QCOMPARE(ach.unlockedCount(), afterTen);

    // 计数器落库 + 已解锁集合跨重开保留
    viewmodel::AchievementService reopened(db.get());
    reopened.load();
    QVERIFY(reopened.isUnlocked(QStringLiteral("ten-pats")));
    QCOMPARE(reopened.unlockedCount(), afterTen);
    QCOMPARE(reopened.snapshot().patCount, 20);
}

void TestContent::achievementNightAndComeback()
{
    auto db = makeDb();
    viewmodel::AchievementService ach(db.get());
    ach.load();

    ach.reportInteraction(Interaction::Belly, msAtLocalHour(23));
    QVERIFY(ach.isUnlocked(QStringLiteral("night-owl")));

    ach.reportComeback(msAtLocalHour(12));
    QVERIFY(ach.isUnlocked(QStringLiteral("comeback")));
    // 幂等
    ach.reportComeback(msAtLocalHour(12));
    QCOMPARE(ach.snapshot().comeback, 1);
}

void TestContent::achievementStateMetrics()
{
    auto db = makeDb();
    viewmodel::AchievementService ach(db.get());
    ach.load();

    const qint64 noon = msAtLocalHour(12);
    ach.setProgress(5, 3, kCompanionMsPerDay * 7, 3, 7, 100, 100, noon);

    QVERIFY(ach.isUnlocked(QStringLiteral("lv5")));
    QVERIFY(!ach.isUnlocked(QStringLiteral("lv10")));
    QVERIFY(ach.isUnlocked(QStringLiteral("bond-action")));
    QVERIFY(!ach.isUnlocked(QStringLiteral("bond-badge")));
    QVERIFY(ach.isUnlocked(QStringLiteral("day7")));
    QVERIFY(!ach.isUnlocked(QStringLiteral("day30")));
    QVERIFY(ach.isUnlocked(QStringLiteral("signin3")));
    QVERIFY(ach.isUnlocked(QStringLiteral("week-signin7")));
    QVERIFY(ach.isUnlocked(QStringLiteral("mood-full")));
    QVERIFY(ach.isUnlocked(QStringLiteral("satiety-full")));

    // 峰值只保留最大值：回落到低值不回退、也不重复解锁
    ach.setProgress(5, 3, kCompanionMsPerDay * 7, 3, 7, 10, 10, noon);
    QCOMPARE(ach.snapshot().moodPeak, 100);
    QCOMPARE(ach.snapshot().satietyPeak, 100);
}

void TestContent::achievementQuestCounters()
{
    auto db = makeDb();
    viewmodel::AchievementService ach(db.get());
    ach.load();

    const qint64 noon = msAtLocalHour(12);
    ach.reportQuestCompleted(noon);
    QVERIFY(ach.isUnlocked(QStringLiteral("quest-first")));
    QCOMPARE(ach.snapshot().questDoneTotal, 1);

    for (int i = 0; i < 9; ++i) {
        ach.reportQuestCompleted(noon);
    }
    QVERIFY(ach.isUnlocked(QStringLiteral("quest-10")));
    QCOMPARE(ach.snapshot().questDoneTotal, 10);

    // 连续 7 天全勤 → quest-all-7；中断一天 → 连续计数归零
    for (int i = 0; i < 7; ++i) {
        ach.reportQuestFullDay(true, noon);
    }
    QVERIFY(ach.isUnlocked(QStringLiteral("quest-all")));
    QVERIFY(ach.isUnlocked(QStringLiteral("quest-all-7")));

    ach.reportQuestFullDay(false, noon);
    QCOMPARE(ach.snapshot().questFullStreak, 0);
}

void TestContent::achievementMiniGameReport()
{
    auto db = makeDb();
    viewmodel::AchievementService ach(db.get());
    ach.load();
    const qint64 noon = msAtLocalHour(12);

    // 第 1 局：胜利 + 全对插旗 + 峰值连翻 10（非高级）
    ach.reportMiniGame(true, false, true, 10, noon);
    QVERIFY(ach.isUnlocked(QStringLiteral("game-first")));
    QVERIFY(ach.isUnlocked(QStringLiteral("game-win")));
    QVERIFY(ach.isUnlocked(QStringLiteral("game-combo10")));
    QVERIFY(ach.isUnlocked(QStringLiteral("game-perfect")));
    QVERIFY(!ach.isUnlocked(QStringLiteral("game-highscore")));
    QVERIFY(!ach.isUnlocked(QStringLiteral("game-play10")));
    QCOMPARE(ach.snapshot().miniGamePlays, 1);
    QCOMPARE(ach.snapshot().miniGamePlaysToday, 1);
    QCOMPARE(ach.snapshot().miniGameMaxChain, 10);

    // 高级难度通关 → 「高手认证」
    ach.reportMiniGame(true, true, false, 3, noon);
    QVERIFY(ach.isUnlocked(QStringLiteral("game-highscore")));

    // 峰值只保留最大：低值不回退
    ach.reportMiniGame(false, false, false, 2, noon);
    QCOMPARE(ach.snapshot().miniGameMaxChain, 10);

    // 至此已 3 局（2 胜 1 负）；再补 8 局 → 累计 11 局 → 「十局纪念」；
    // 单日计数 11 ≥ 3 → 「三局全清」
    for (int i = 0; i < 8; ++i) {
        ach.reportMiniGame(false, false, false, 1, noon);
    }
    QVERIFY(ach.isUnlocked(QStringLiteral("game-play10")));
    QVERIFY(ach.isUnlocked(QStringLiteral("game-daily3")));
    QCOMPARE(ach.snapshot().miniGamePlays, 11);

    // 跨天：单日局数清零，累计局数保留
    const qint64 nextDay = noon + kDay;
    ach.reportMiniGame(false, false, false, 1, nextDay);
    QCOMPARE(ach.snapshot().miniGamePlaysToday, 1);
    QCOMPARE(ach.snapshot().miniGamePlays, 12);

    // 已解锁集合与计数器跨重开保留
    viewmodel::AchievementService reopened(db.get());
    reopened.load();
    QVERIFY(reopened.isUnlocked(QStringLiteral("game-win")));
    QVERIFY(reopened.isUnlocked(QStringLiteral("game-perfect")));
    QCOMPARE(reopened.snapshot().miniGamePlays, 12);
    QCOMPARE(reopened.snapshot().miniGameWins, 2);
}

void TestContent::questLoadsFixedAndPickedSlots()
{
    auto db = makeDb();
    viewmodel::QuestService quest(db.get());
    const qint64 base = msAtLocalHour(12);
    QVERIFY(quest.load(base));

    QCOMPARE(quest.slotList().size(), kQuestSlotCount);
    QCOMPARE(quest.slotList().first().id, QStringLiteral("signin-1"));
    QCOMPARE(quest.dayKey(), QString::fromStdString(dayKey(base)));
    QCOMPARE(quest.claimedCount(), 0);
    QVERIFY(!quest.allClaimed());

    // 同日重开：槽位与进度保持一致（dayKey 未变 → 直接复用落库内容）
    viewmodel::QuestService reopened(db.get());
    QVERIFY(reopened.load(base));
    QCOMPARE(reopened.slotList().size(), quest.slotList().size());
    for (int i = 0; i < quest.slotList().size(); ++i) {
        QCOMPARE(reopened.slotList()[i].id, quest.slotList()[i].id);
        QCOMPARE(reopened.slotList()[i].target, quest.slotList()[i].target);
    }
}

void TestContent::questProgressAndClaimIsIdempotent()
{
    auto db = makeDb();
    viewmodel::QuestService quest(db.get());
    const qint64 base = msAtLocalHour(12);
    QVERIFY(quest.load(base));

    QSignalSpy doneSpy(&quest, &viewmodel::QuestService::questDone);
    QSignalSpy rewardSpy(&quest, &viewmodel::QuestService::rewardGranted);

    // slot 0 = 今日签到，一次签到交互即完成
    QVERIFY(quest.reportInteraction(Interaction::Signin, base));
    QVERIFY(quest.slotList()[0].done);
    QCOMPARE(doneSpy.count(), 1);

    // 「今日签到」任务的完成不再重复记日记：签到本身已由 SigninService 记 kind=signin，
    // 此处若再记 kind=quest 会让同一次签到在日记里出现两条（见 TRAP-P4-006）。
    {
        model::DiaryRepo diary(db.get());
        QCOMPARE(diary.count(), 0);
    }

    // 未完成时不可领取
    QCOMPARE(quest.slotList()[0].claimed, false);

    QVERIFY(quest.claim(0, base));
    QCOMPARE(quest.slotList()[0].claimed, true);
    QCOMPARE(rewardSpy.count(), 1);
    QCOMPARE(quest.claimedCount(), 1);

    // 重复领取：DB 条件更新影响 0 行 → 返回 false，不重复发奖
    QVERIFY(!quest.claim(0, base));
    QCOMPARE(rewardSpy.count(), 1);

    // 越界领取安全
    QVERIFY(!quest.claim(-1, base));
    QVERIFY(!quest.claim(999, base));
}

void TestContent::questDayRollReportsFullAttendance()
{
    auto db = makeDb();
    viewmodel::QuestService quest(db.get());
    const qint64 base = msAtLocalHour(12);
    QVERIFY(quest.load(base));

    // 用匹配的交互把 3 个槽都推到 done，再全部领取
    for (int i = 0; i < quest.slotList().size(); ++i) {
        const int target = quest.slotList()[i].target;
        const QuestDef *def = findQuest(quest.slotList()[i].id.toUtf8().constData());
        QVERIFY(def != nullptr);
        const Interaction it = interactionForQuest(def->metric);
        for (int t = 0; t < target; ++t) {
            quest.reportInteraction(it, base);
        }
    }
    QVERIFY(quest.allClaimed() == false);
    for (int i = 0; i < quest.slotList().size(); ++i) {
        QVERIFY(quest.claim(i, base));
    }
    QVERIFY(quest.allClaimed());
    QCOMPARE(quest.claimedCount(), kQuestSlotCount);

    // 跨天：上一天 3 槽全领 → dayRolled(true)
    QSignalSpy rollSpy(&quest, &viewmodel::QuestService::dayRolled);
    QVERIFY(quest.load(base + kDay));
    QCOMPARE(rollSpy.count(), 1);
    QVERIFY(rollSpy.at(0).at(0).toBool());
    QCOMPARE(quest.claimedCount(), 0); // 新的一天重新开始
    QCOMPARE(quest.dayKey(), QString::fromStdString(dayKey(base + kDay)));
}

void TestContent::questRepoConditionalUpdate()
{
    auto db = makeDb();
    model::QuestRepo repo(db.get());

    model::QuestSlot slot;
    slot.id = QStringLiteral("pat-3");
    slot.slot = 0;
    slot.progress = 0;
    slot.target = 3;
    slot.done = false;
    slot.claimed = false;
    slot.dayKey = QStringLiteral("2026-9-30");
    QVERIFY(repo.replaceAll({slot}));
    QCOMPARE(repo.loadAll().size(), 1);

    // 未 done 时不可领取（条件更新影响 0 行）
    QVERIFY(!repo.markClaimed(QStringLiteral("pat-3")));

    QVERIFY(repo.updateProgress(QStringLiteral("pat-3"), 3, true));
    QVERIFY(repo.markClaimed(QStringLiteral("pat-3")));
    QVERIFY(!repo.markClaimed(QStringLiteral("pat-3"))); // 幂等

    const QList<model::QuestSlot> loaded = repo.loadAll();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().progress, 3);
    QVERIFY(loaded.first().done);
    QVERIFY(loaded.first().claimed);
}

void TestContent::signinBoardTracksTodayAndMilestone()
{
    auto db = makeDb();
    viewmodel::SigninService signin(db.get());
    const qint64 base = msAtLocalHour(12);
    QVERIFY(signin.load(base));

    QCOMPARE(signin.rows().size(), kSigninWeekDays);
    QCOMPARE(signin.signedCount(), 0);
    QVERIFY(!signin.isTodaySigned());
    QCOMPARE(signin.weekKey(), QString::fromStdString(weekKey(base)));

    QSignalSpy rewardSpy(&signin, &viewmodel::SigninService::rewardGranted);
    QVERIFY(signin.markToday(base));
    QCOMPARE(signin.signedCount(), 1);
    QVERIFY(signin.isTodaySigned());
    QCOMPARE(rewardSpy.count(), 1); // 1 天里程碑
    QCOMPARE(rewardSpy.at(0).at(2).toInt(), 1);

    // 同日重复签到：不生效、不重复发奖
    QVERIFY(!signin.markToday(base + 3600000));
    QCOMPARE(signin.signedCount(), 1);
    QCOMPARE(rewardSpy.count(), 1);

    // 签到板跨重开保留
    viewmodel::SigninService reopened(db.get());
    QVERIFY(reopened.load(base));
    QCOMPARE(reopened.signedCount(), 1);
    QVERIFY(reopened.isTodaySigned());
}

void TestContent::signinResetsOnWeekChange()
{
    auto db = makeDb();
    viewmodel::SigninService signin(db.get());
    const qint64 base = msAtLocalHour(12);
    QVERIFY(signin.load(base));
    QVERIFY(signin.markToday(base));
    QCOMPARE(signin.signedCount(), 1);

    const QString before = signin.weekKey();
    QSignalSpy weekSpy(&signin, &viewmodel::SigninService::weekChanged);

    // 下一周：整表重置（回到 7 行、0 已签），随后点亮新一周的今天
    const qint64 nextWeek = base + kDay * 7;
    QVERIFY(signin.markToday(nextWeek));
    QCOMPARE(weekSpy.count(), 1);
    QVERIFY(signin.weekKey() != before);
    QCOMPARE(signin.rows().size(), kSigninWeekDays);
    QCOMPARE(signin.signedCount(), 1);
}

void TestContent::diaryDeduplicatesWithinDayAndTrims()
{
    auto db = makeDb();
    model::DiaryRepo diary(db.get());
    const qint64 base = msAtLocalHour(12);

    QVERIFY(diary.appendDaily(QStringLiteral("quest"), QStringLiteral("摸摸头"), base));
    QVERIFY(!diary.appendDaily(QStringLiteral("quest"), QStringLiteral("摸摸头"), base + 3600000));
    QVERIFY(diary.appendDaily(QStringLiteral("quest"), QStringLiteral("多夸夸我"), base));
    QVERIFY(diary.appendDaily(QStringLiteral("signin"), QStringLiteral("今日签到"), base));
    // 换一天：同文案可再次记录
    QVERIFY(diary.appendDaily(QStringLiteral("quest"), QStringLiteral("摸摸头"), base + kDay));
    QCOMPARE(diary.count(), 4);

    // 上限裁剪：只保留最近 maxEntries 条
    for (int i = 0; i < 100; ++i) {
        diary.append(QStringLiteral("test"), QStringLiteral("entry-%1").arg(i), base + i);
    }
    QCOMPARE(diary.count(), 104);
    QVERIFY(diary.trimTo(80));
    QCOMPARE(diary.count(), 80);

    // 最近记录按时间倒序（最新的 entry-99 在最前）
    const QList<model::DiaryEntry> recent = diary.loadRecent(12);
    QCOMPARE(recent.size(), 12);
    QCOMPARE(recent.first().detail, QStringLiteral("entry-99"));
}

QTEST_GUILESS_MAIN(TestContent)
#include "test_content.moc"
