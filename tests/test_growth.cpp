#include "core/GrowthRules.h"
#include "model/Database.h"
#include "viewmodel/GrowthService.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace whalepet;
using namespace whalepet::core;

namespace {

constexpr qint64 kDay = 86400000LL;

qint64 baseMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// 在内存库上建一个已载入默认状态的服务
std::unique_ptr<model::Database> makeDb()
{
    auto db = std::make_unique<model::Database>();
    const bool ok = db->openMemory();
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return db;
}

} // namespace

class TestGrowth : public QObject {
    Q_OBJECT

private slots:
    // ---- 纯规则 ----
    void levelCurveDerivedFromExp();
    void expCurveIsMonotonic();
    void deltaTableMatchesWhale();
    void clampingRules();
    void bondUnlocksThresholds();
    void dayKeyFormatAndAdvance();

    // ---- 服务 ----
    void interactionUpdatesStateAndPersists();
    void satietyDecayUsesIntegerPoints();
    void feedingResetsDecayRemainder();
    void companionTimeAccumulates();
    void signInIsIdempotentPerDay();
    void signInStreakBreaksAfterGap();
    void levelUpSignalFiresOnce();
    void stateSurvivesReopen();
    void resetToDefaultsRestoresBaseline();
};

void TestGrowth::levelCurveDerivedFromExp()
{
    QCOMPARE(levelForExp(0), 1);
    QCOMPARE(levelForExp(kLevelStep - 1), 1);
    QCOMPARE(levelForExp(kLevelStep), 2);
    QCOMPARE(levelForExp(kLevelStep * 2), 3);
    QCOMPARE(levelForExp(-5), 1);

    QCOMPARE(expInLevel(0), 0);
    QCOMPARE(expInLevel(750), 250);
    QCOMPARE(expInLevel(1000), 0);
    QCOMPARE(expSpanForLevel(1), kLevelStep);
    QCOMPARE(expSpanForLevel(9), kLevelStep);
}

void TestGrowth::expCurveIsMonotonic()
{
    // docs/GAMEPLAY.md：exp_needed(level) 对 level 单调递增
    for (int lv = 1; lv < 40; ++lv) {
        QVERIFY2(expNeeded(lv + 1) > expNeeded(lv), qPrintable(QString::number(lv)));
    }
    QCOMPARE(expNeeded(0), expNeeded(1)); // 非法等级按 1 处理
    QCOMPARE(expNeeded(2), kLevelStep * 2);
}

void TestGrowth::deltaTableMatchesWhale()
{
    const auto pat = deltaFor(Interaction::Pat);
    QCOMPARE(pat.mood, 4);
    QCOMPARE(pat.affinity, 2);
    QCOMPARE(pat.satiety, 0);

    const auto poke = deltaFor(Interaction::Poke);
    QCOMPARE(poke.mood, -6);
    QCOMPARE(poke.affinity, 0);

    const auto feed = deltaFor(Interaction::Feed);
    QCOMPARE(feed.mood, 3);
    QCOMPARE(feed.affinity, 5);
    QCOMPARE(feed.satiety, 30);

    const auto praise = deltaFor(Interaction::Praise);
    QCOMPARE(praise.mood, 5);
    QCOMPARE(praise.affinity, 8);

    const auto belly = deltaFor(Interaction::Belly);
    QCOMPARE(belly.mood, 3);
    QCOMPARE(belly.affinity, 2);

    const auto tail = deltaFor(Interaction::Tail);
    QCOMPARE(tail.mood, 2);
    QCOMPARE(tail.affinity, 3);

    const auto triple = deltaFor(Interaction::Triple);
    QCOMPARE(triple.mood, 10);
    QCOMPARE(triple.affinity, 10);

    const auto signin = deltaFor(Interaction::Signin);
    QCOMPARE(signin.mood, 5);
    QCOMPARE(signin.affinity, 0);
}

void TestGrowth::clampingRules()
{
    QCOMPARE(clampMood(-1), 0);
    QCOMPARE(clampMood(kMoodMax + 1), kMoodMax);
    QCOMPARE(clampSatiety(-100), 0);
    QCOMPARE(clampSatiety(kSatietyMax + 50), kSatietyMax);
    QCOMPARE(clampAffinity(-1), 0);
    QCOMPARE(clampAffinity(kAffinityMax + 1), kAffinityMax);
}

void TestGrowth::bondUnlocksThresholds()
{
    QVERIFY(!bondUnlocks(2).action);
    QVERIFY(bondUnlocks(3).action);
    QVERIFY(!bondUnlocks(4).badge);
    QVERIFY(bondUnlocks(5).badge);
    QVERIFY(!bondUnlocks(6).egg);
    QVERIFY(bondUnlocks(7).egg);
    QVERIFY(bondUnlocks(0).action == false);
}

void TestGrowth::dayKeyFormatAndAdvance()
{
    const qint64 now = baseMs();
    const std::string today = dayKey(now);
    const std::string tomorrow = dayKey(now + kDay);
    QVERIFY(!today.empty());
    QVERIFY(today != tomorrow);
    QVERIFY(previousDayKey(now + kDay) == today);

    // 格式 `年-月-日`（月/日不补零），不依赖时区
    const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{1,2}-\\d{1,2}$"));
    QVERIFY(re.match(QString::fromStdString(today)).hasMatch());
}

void TestGrowth::interactionUpdatesStateAndPersists()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    QCOMPARE(growth.state().mood, kDefaultMood);
    QCOMPARE(growth.state().satiety, kDefaultSatiety);
    QCOMPARE(growth.state().affinity, 0);
    QCOMPARE(growth.state().exp, 0);

    const qint64 now = baseMs();
    growth.applyInteraction(Interaction::Pat, now);
    QCOMPARE(growth.state().mood, kDefaultMood + 4);
    QCOMPARE(growth.state().affinity, 2);
    QCOMPARE(growth.state().exp, 2); // exp 与 affinity 同步累加

    growth.applyInteraction(Interaction::Feed, now);
    QCOMPARE(growth.state().mood, kDefaultMood + 7);
    QCOMPARE(growth.state().affinity, 7);
    QCOMPARE(growth.state().satiety, kSatietyMax); // 80 + 30 → 夹到上限

    // 心情掉到下限就不再降低
    for (int i = 0; i < 40; ++i) {
        growth.applyInteraction(Interaction::Poke, now);
    }
    QCOMPARE(growth.state().mood, 0);

    // 即时落盘：重新载入应得到同样数值
    viewmodel::GrowthService reopened(db.get());
    QVERIFY(reopened.load());
    QCOMPARE(reopened.state().affinity, growth.state().affinity);
    QCOMPARE(reopened.state().mood, growth.state().mood);
}

void TestGrowth::satietyDecayUsesIntegerPoints()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    const qint64 base = baseMs();
    QCOMPARE(growth.state().satiety, kDefaultSatiety);

    // 不足 1 点的时长：不掉点
    growth.settle(base + kMsPerSatietyPoint - 1);
    QCOMPARE(growth.state().satiety, kDefaultSatiety);

    // 累计到 1 点：掉 1
    growth.settle(base + kMsPerSatietyPoint);
    QCOMPARE(growth.state().satiety, kDefaultSatiety - 1);

    // 再过 3 点时长：累计掉到 76
    growth.settle(base + kMsPerSatietyPoint + kMsPerSatietyPoint * 3);
    QCOMPARE(growth.state().satiety, kDefaultSatiety - 4);

    // 系统时间回拨：不结算、不崩溃
    growth.settle(base);
    QCOMPARE(growth.state().satiety, kDefaultSatiety - 4);
}

void TestGrowth::feedingResetsDecayRemainder()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    const qint64 base = baseMs();
    // 先积累不足 1 点的余量
    growth.settle(base + kMsPerSatietyPoint / 2);
    QCOMPARE(growth.state().satiety, kDefaultSatiety);

    // 投喂：余量清零，之后必须重新攒满 1 点才掉
    growth.applyInteraction(Interaction::Feed, base + kMsPerSatietyPoint / 2);
    QCOMPARE(growth.state().satiety, kSatietyMax);

    growth.settle(base + kMsPerSatietyPoint / 2 + kMsPerSatietyPoint / 2 + 1000);
    QCOMPARE(growth.state().satiety, kSatietyMax); // 只攒了 0.5 点 + 1s
}

void TestGrowth::companionTimeAccumulates()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    const qint64 base = baseMs();
    growth.settle(base + core::kGrowthTickMs);
    QCOMPARE(growth.state().companionMs, core::kGrowthTickMs);

    growth.settle(base + core::kGrowthTickMs * 3);
    QCOMPARE(growth.state().companionMs, core::kGrowthTickMs * 3);
}

void TestGrowth::signInIsIdempotentPerDay()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    const qint64 base = baseMs();
    QCOMPARE(growth.state().streakDays, 0);

    QVERIFY(growth.signIn(base));
    QCOMPARE(growth.state().streakDays, 1);
    const int moodAfterFirst = growth.state().mood;

    QVERIFY(!growth.signIn(base)); // 同日重复签到：不生效
    QCOMPARE(growth.state().streakDays, 1);
    QCOMPARE(growth.state().mood, moodAfterFirst); // 心情不再叠加

    // 同日但换个时刻，仍算同一天
    QVERIFY(!growth.signIn(base + 3600000));
    QCOMPARE(growth.state().streakDays, 1);
}

void TestGrowth::signInStreakBreaksAfterGap()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    const qint64 base = baseMs();
    QVERIFY(growth.signIn(base));
    QCOMPARE(growth.state().streakDays, 1);

    // 连续第二天 → 2
    QVERIFY(growth.signIn(base + kDay));
    QCOMPARE(growth.state().streakDays, 2);

    // 断档 2 天 → 重置为 1
    QVERIFY(growth.signIn(base + kDay * 4));
    QCOMPARE(growth.state().streakDays, 1);

    // 签到状态跨重开保留
    viewmodel::GrowthService reopened(db.get());
    QVERIFY(reopened.load());
    QCOMPARE(reopened.state().streakDays, 1);
    QVERIFY(!reopened.signIn(base + kDay * 4)); // 同一天仍然幂等
}

void TestGrowth::levelUpSignalFiresOnce()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    QSignalSpy levelSpy(&growth, &viewmodel::GrowthService::levelUp);
    QSignalSpy bondSpy(&growth, &viewmodel::GrowthService::bondUp);
    QVERIFY(levelSpy.isValid());

    const qint64 now = baseMs();
    // 夸夸 +8 好感：500 / 8 = 62.5 → 第 63 次跨过 Lv2
    for (int i = 0; i < 62; ++i) {
        growth.applyInteraction(Interaction::Praise, now);
    }
    QCOMPARE(growth.state().level, 1);
    QCOMPARE(levelSpy.count(), 0);

    growth.applyInteraction(Interaction::Praise, now);
    QCOMPARE(growth.state().exp, 504);
    QCOMPARE(growth.state().level, 2);
    QCOMPARE(growth.state().bondLevel, 2);
    QCOMPARE(levelSpy.count(), 1);
    QCOMPARE(levelSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(bondSpy.count(), 1);
    QCOMPARE(bondSpy.at(0).at(0).toInt(), 2);

    // 同一等级内继续互动不再重复发升级信号
    growth.applyInteraction(Interaction::Praise, now);
    QCOMPARE(levelSpy.count(), 1);
}

void TestGrowth::stateSurvivesReopen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("whalepet.db"));

    {
        model::Database db;
        QVERIFY(db.openPath(path, model::StorageMode::InstallDir));
        viewmodel::GrowthService growth(&db);
        growth.load();
        const qint64 now = baseMs();
        growth.applyInteraction(Interaction::Tail, now);
        growth.signIn(now);
        QVERIFY(growth.flush());
    }
    {
        model::Database db;
        QVERIFY(db.openPath(path, model::StorageMode::InstallDir));
        viewmodel::GrowthService growth(&db);
        QVERIFY(growth.load());
        QCOMPARE(growth.state().affinity, 3); // Tail: +3
        QCOMPARE(growth.state().exp, 3);
        QCOMPARE(growth.state().streakDays, 1);
    }
}

void TestGrowth::resetToDefaultsRestoresBaseline()
{
    auto db = makeDb();
    viewmodel::GrowthService growth(db.get());
    growth.load();

    const qint64 now = baseMs();
    for (int i = 0; i < 5; ++i) {
        growth.applyInteraction(Interaction::Praise, now);
    }
    growth.signIn(now);
    QVERIFY(growth.state().affinity > 0);

    growth.resetToDefaults(now);
    QCOMPARE(growth.state().level, kDefaultLevel);
    QCOMPARE(growth.state().exp, 0);
    QCOMPARE(growth.state().affinity, 0);
    QCOMPARE(growth.state().mood, kDefaultMood);
    QCOMPARE(growth.state().satiety, kDefaultSatiety);
    QCOMPARE(growth.state().streakDays, 0);
    QCOMPARE(growth.state().companionMs, 0);

    // 重置后同日仍可重新签到（last_signin_day 已清空）
    QVERIFY(growth.signIn(now));
    QCOMPARE(growth.state().streakDays, 1);
}

QTEST_GUILESS_MAIN(TestGrowth)
#include "test_growth.moc"
