#include <QtTest>

#include "core/DaySlotRules.h"
#include "core/DialogueOptions.h"
#include "core/DialoguePoseRules.h"
#include "core/PoseCatalog.h"
#include "core/PresetDialogue.h"
#include "core/WeatherRules.h"
#include "core/WorkPosePool.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace whalepet::core;
namespace core = whalepet::core;

namespace {

constexpr std::int64_t kBase = 1'000'000;

// 语料片段：四类问题各若干（**主人提问**的口吻；天气题同一类型写三条候选）
const char *const kCorpus = R"(# 注释行忽略
q|q-weather|weather|外面天气怎么样呀？
a|q-weather|sunny|今天是晴天呢
a|q-weather|sunny|阳光很好，适合出门
a|q-weather|sunny|晴得让人想散步
a|q-weather|rain|下雨了记得带伞
a|q-weather|rain|外面在下雨哦
a|q-weather|rain|雨天路滑，慢一点
q|q-secret|sensitive|你的存款有多少呀？
a|q-secret|0|这是我的小秘密
a|q-secret|1|够买很多小鱼干
a|q-secret|2|（捂住钱包）想都别想
q|q-mood|normal|今天心情怎么样呀？
a|q-mood|0|还不错！
a|q-mood|1|有一点点累
a|q-mood|2|好得想转三圈
q|q-food|normal|你喜欢吃什么呀？
a|q-food|0|小鱼干！
a|q-food|1|甜甜的东西
a|q-food|2|你喂的都很香
q|q-hotpot|choice|今天吃火锅好不好？
a|q-hotpot|0|好！必须好
a|q-hotpot|1|不了，胃在抗议
a|q-hotpot|2|你请客我就去
q|q-plan|normal|周末想做什么呀？
a|q-plan|0|睡到自然醒
a|q-plan|1|去有风的地方
a|q-plan|2|列一长串然后躺平
q|q-wish|normal|有什么小愿望吗？
a|q-wish|0|希望你顺利
a|q-wish|1|想你多摸摸头
a|q-wish|2|说出来就不灵了
)";

// 只含天气题的**最小**语料（用于「无 unknown 槽位」的回落验证）
const char *const kWeatherOnlyCorpus = R"(
q|q-weather|weather|外面天气怎么样呀？
a|q-weather|sunny|晴1
a|q-weather|rain|雨1
)";

} // namespace

// ---------------------------------------------------------------------------
// P8：时段规则 / 天气判定 / 工作立绘池 / 问答系统（五选一）——全部零 Qt 纯逻辑
// 归属：docs/DIALOGUE.md、docs/STATE-MACHINE.md §1.2–§1.3
// ---------------------------------------------------------------------------
class PresetDialogueTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 时段 ----
    void daySlotCoversWholeClock();
    void daySlotPosesExist();

    // ---- 天气 ----
    void caiyunWeatherMapsToKinds();
    void weatherKindPosesExist();
    void weatherKindIdRoundTrip();

    // ---- 工作立绘池 ----
    void workPoolRotatesWithoutRepeat();
    void workPoolAvoidsNotedPoses();
    void codingStatesAreSeparated();

    // ---- 语料 ----
    void corpusParsesQuestionsAndAnswers();
    void corpusKeepsMultipleCandidatesPerSlot();
    void corpusIgnoresDirtyLines();
    void sceneKeysAreStable();

    // ---- 五选一选项池 ----
    void optionsAreAlwaysFiveWithFixedSlots();
    void weatherSlotDisabledWithoutApi();
    void sensitiveSlotLockedByAffinity();
    void sensitiveSlotBlockedByDailyQuota();
    void randomOptionsAvoidRecentIds();

    // ---- 回答选取 ----
    void pickAnswerSlotRandomForQuestion();
    void pickAnswerSlotFollowsWeatherKind();

    // ---- 独立立绘池 ----
    void dialoguePosesExist();
};

void PresetDialogueTest::daySlotCoversWholeClock()
{
    QCOMPARE(static_cast<int>(core::daySlotOf(0)), static_cast<int>(core::DaySlot::LateNight));
    QCOMPARE(static_cast<int>(core::daySlotOf(6)), static_cast<int>(core::DaySlot::LateNight));
    QCOMPARE(static_cast<int>(core::daySlotOf(7)), static_cast<int>(core::DaySlot::Day));
    QCOMPARE(static_cast<int>(core::daySlotOf(17)), static_cast<int>(core::DaySlot::Day));
    QCOMPARE(static_cast<int>(core::daySlotOf(18)), static_cast<int>(core::DaySlot::Evening));
    QCOMPARE(static_cast<int>(core::daySlotOf(22)), static_cast<int>(core::DaySlot::Evening));
    QCOMPARE(static_cast<int>(core::daySlotOf(23)), static_cast<int>(core::DaySlot::LateNight));
    // 非法小时保守判深夜（不主动换成日间立绘）
    QCOMPARE(static_cast<int>(core::daySlotOf(24)), static_cast<int>(core::DaySlot::LateNight));
    QCOMPARE(static_cast<int>(core::daySlotOf(-1)), static_cast<int>(core::DaySlot::LateNight));

    QCOMPARE(QString::fromLatin1(core::daySlotPoseOf(core::DaySlot::Day)),
             QStringLiteral("idle-cute"));
    QCOMPARE(QString::fromLatin1(core::daySlotPoseOf(core::DaySlot::Evening)),
             QStringLiteral("night"));
    QCOMPARE(QString::fromLatin1(core::daySlotPoseOf(core::DaySlot::LateNight)),
             QStringLiteral("daily-pajama"));
    QCOMPARE(core::kLateNightAwakeMs, std::int64_t(60000));
}

void PresetDialogueTest::daySlotPosesExist()
{
    for (int hour = 0; hour <= 23; ++hour) {
        const char *pose = core::daySlotPoseOf(core::daySlotOf(hour));
        QVERIFY2(core::poseExists(pose), pose);
    }
    QVERIFY(core::poseExists(core::kLateNightAwakePose));
}

void PresetDialogueTest::caiyunWeatherMapsToKinds()
{
    struct Case {
        const char *zh;
        WeatherKind kind;
    };
    // 顺序即优先级：雷 > 雹 > 雪 > 雨 > 雾霾尘 > 阴 > 云 > 晴
    const Case cases[] = {
        { "晴", WeatherKind::Sunny },
        { "多云", WeatherKind::Cloudy },
        { "晴间多云", WeatherKind::Cloudy },
        { "阴", WeatherKind::Overcast },
        // 「阴转多云」以**首个**状态为准（阴），故仍是阴天
        { "阴转多云", WeatherKind::Overcast },
        { "阵雨", WeatherKind::Rain },
        { "小雨", WeatherKind::Rain },
        { "雷阵雨", WeatherKind::Thunder },
        { "小雨夹雪", WeatherKind::Snow },
        { "小雪", WeatherKind::Snow },
        { "雾", WeatherKind::Fog },
        { "霾", WeatherKind::Fog },
        { "浮尘", WeatherKind::Fog },
        { "冰雹", WeatherKind::Hail },
        { "", WeatherKind::Unknown },
        { "外星天气", WeatherKind::Unknown },
    };
    for (const Case &c : cases) {
        QCOMPARE(static_cast<int>(core::weatherKindFromCaiyun(c.zh)),
                 static_cast<int>(c.kind));
    }
}

void PresetDialogueTest::weatherKindPosesExist()
{
    for (int i = 0; i <= static_cast<int>(WeatherKind::Hail); ++i) {
        const auto kind = static_cast<WeatherKind>(i);
        QVERIFY2(core::poseExists(core::weatherKindPose(kind)),
                 core::weatherKindPose(kind));
    }
    QCOMPARE(QString::fromLatin1(core::weatherKindPose(WeatherKind::Rain)),
             QStringLiteral("weather-umbrella"));
    QCOMPARE(QString::fromLatin1(core::weatherKindPose(WeatherKind::Snow)),
             QStringLiteral("weather-snow"));
    QCOMPARE(QString::fromLatin1(core::weatherKindPose(WeatherKind::Thunder)),
             QStringLiteral("weather-thunder"));
    // 未判定时退回通用好奇，不硬聊天气
    QCOMPARE(QString::fromLatin1(core::weatherKindPose(WeatherKind::Unknown)),
             QStringLiteral("curious"));

    // 2026-10-04 立绘激活 22：盛夏（7/8/9 月）晴天 → daily-melt，其余月份沿用 weatherKindPose
    for (int month : { 7, 8, 9 }) {
        QCOMPARE(QString::fromLatin1(core::weatherKindPoseForMonth(WeatherKind::Sunny, month)),
                 QStringLiteral("daily-melt"));
    }
    QCOMPARE(QString::fromLatin1(core::weatherKindPoseForMonth(WeatherKind::Sunny, 1)),
             QString::fromLatin1(core::weatherKindPose(WeatherKind::Sunny)));
    QCOMPARE(QString::fromLatin1(core::weatherKindPoseForMonth(WeatherKind::Rain, 7)),
             QStringLiteral("weather-umbrella"));
    QVERIFY(core::poseExists(core::weatherKindPoseForMonth(WeatherKind::Sunny, 8)));
}

void PresetDialogueTest::weatherKindIdRoundTrip()
{
    for (int i = 0; i <= static_cast<int>(WeatherKind::Hail); ++i) {
        const auto kind = static_cast<WeatherKind>(i);
        QCOMPARE(static_cast<int>(core::weatherKindFromId(core::weatherKindId(kind))),
                 static_cast<int>(kind));
    }
    QCOMPARE(static_cast<int>(core::weatherKindFromId("no-such")),
             static_cast<int>(WeatherKind::Unknown));
}

void PresetDialogueTest::workPoolRotatesWithoutRepeat()
{
    WorkPosePool pool;
    QCOMPARE(WorkPosePool::count(), std::size_t(13));

    // 一轮轮转：每张都取到，且不重复
    std::vector<std::string> seen;
    for (std::size_t i = 0; i < WorkPosePool::count(); ++i) {
        const char *pose = pool.next();
        QVERIFY(pose != nullptr);
        seen.push_back(pose);
    }
    std::sort(seen.begin(), seen.end());
    QCOMPARE(std::adjacent_find(seen.begin(), seen.end()), seen.end());

    // 第二轮同样取满 13 张（最近窗口 3 < 13，不会卡住）
    for (std::size_t i = 0; i < WorkPosePool::count(); ++i) {
        QVERIFY(pool.next() != nullptr);
    }

    pool.reset();
    QCOMPARE(pool.recentCount(), std::size_t(0));
    QCOMPARE(pool.cursor(), std::size_t(0));
}

void PresetDialogueTest::workPoolAvoidsNotedPoses()
{
    WorkPosePool pool;
    // 热词命中的立绘记入「最近」→ 随后的轮转必须避开
    pool.note("work-deadline");
    QCOMPARE(QString::fromLatin1(pool.current()), QStringLiteral("work-deadline"));
    QVERIFY(WorkPosePool::contains("work-deadline"));
    QVERIFY(!WorkPosePool::contains("running"));
    QVERIFY(!WorkPosePool::contains("meme-wakuwaku"));

    for (std::size_t i = 0; i < WorkPosePool::count() - 1; ++i) {
        const char *pose = pool.next();
        QVERIFY2(std::string(pose) != "work-deadline", "刚出现过的立绘不应立刻轮回来");
    }

    // 非池成员不参与联动：不改变「最近」窗口，也不改变当前立绘
    const std::size_t before = pool.recentCount();
    const QString currentBefore = QString::fromLatin1(pool.current());
    pool.note("meme-wakuwaku");
    QCOMPARE(pool.recentCount(), before);
    QCOMPARE(QString::fromLatin1(pool.current()), currentBefore);
}

void PresetDialogueTest::codingStatesAreSeparated()
{
    QVERIFY(core::workStateIsCoding(WorkState::Coding));
    QVERIFY(core::workStateIsCoding(WorkState::VibeCoding));
    QVERIFY(core::workStateIsCoding(WorkState::Debugging));
    QVERIFY(!core::workStateIsCoding(WorkState::Reading));
    QVERIFY(!core::workStateIsCoding(WorkState::Meeting));
    QVERIFY(!core::workStateIsCoding(WorkState::Idle));
    QVERIFY(!core::workStateIsCoding(WorkState::Unknown));

    // 池态 = busy 且非编程族
    QVERIFY(core::workStateUsesPool(WorkState::Reading));
    QVERIFY(core::workStateUsesPool(WorkState::Meeting));
    QVERIFY(!core::workStateUsesPool(WorkState::Coding));
    // 无感知数据（Unknown）与未工作态不参与池（零回归）
    QVERIFY(!core::workStateUsesPool(WorkState::Unknown));
    QVERIFY(!core::workStateUsesPool(WorkState::Idle));

    QVERIFY(core::poseExists(core::kCodingPose));
}

void PresetDialogueTest::corpusParsesQuestionsAndAnswers()
{
    PresetDialogueTable table;
    QCOMPARE(table.loadFromText(kCorpus), std::size_t(7));
    QCOMPARE(table.countOf(DialogueCategory::Normal), std::size_t(4));
    QCOMPARE(table.countOf(DialogueCategory::Sensitive), std::size_t(1));
    QCOMPARE(table.countOf(DialogueCategory::Choice), std::size_t(1));
    QCOMPARE(table.countOf(DialogueCategory::Weather), std::size_t(1));

    const DialogueQuestion *mood = table.find("q-mood");
    QVERIFY(mood != nullptr);
    QCOMPARE(QString::fromStdString(mood->text), QString::fromUtf8("今天心情怎么样呀？"));
    // 每个问题三个预设回答
    QCOMPARE(mood->answerCount(), std::size_t(3));
    QCOMPARE(mood->answerSlots().size(), std::size_t(3));
    QCOMPARE(mood->answersFor("0").size(), std::size_t(1));
    QCOMPARE(mood->answersFor("9").size(), std::size_t(0));

    const std::string *slot1 = mood->answerFor("1");
    QVERIFY(slot1 != nullptr);
    QCOMPARE(QString::fromStdString(*slot1), QString::fromUtf8("有一点点累"));

    // 天气题：同一类型可有多条候选（都落在同一个台词场景 key 下）
    const DialogueQuestion *weather = table.find("q-weather");
    QVERIFY(weather != nullptr);
    QCOMPARE(weather->answersFor("sunny").size(), std::size_t(3));
    QCOMPARE(weather->answersFor("rain").size(), std::size_t(3));
    QCOMPARE(weather->answersFor("snow").size(), std::size_t(0));
}

void PresetDialogueTest::corpusKeepsMultipleCandidatesPerSlot()
{
    PresetDialogueTable table;
    table.loadFromText(kWeatherOnlyCorpus);
    const DialogueQuestion *weather = table.find("q-weather");
    QVERIFY(weather != nullptr);
    // 同一 slot 的两条（不同 slot 各一条）都被保留，不再被去重丢弃
    QCOMPARE(weather->answerCount(), std::size_t(2));
    QCOMPARE(weather->answersFor("sunny").size(), std::size_t(1));
    QCOMPARE(weather->answersFor("rain").size(), std::size_t(1));
}

void PresetDialogueTest::corpusIgnoresDirtyLines()
{
    PresetDialogueTable table;
    const char *dirty = "# 注释\n"
                        "\n"
                        "q|ok|normal|正常问题\n"
                        "a|ok|0|回答零\n"
                        "q||normal|缺 id\n"
                        "q|bad-cat|no-such-category|类别非法\n"
                        "a|orphan|0|孤儿回答\n"
                        "a|ok|1|\n"; // 缺文本
    QCOMPARE(table.loadFromText(dirty), std::size_t(1));
    const DialogueQuestion *ok = table.find("ok");
    QVERIFY(ok != nullptr);
    QCOMPARE(ok->answerCount(), std::size_t(1));
    QVERIFY(table.find("orphan") == nullptr);
}

void PresetDialogueTest::sceneKeysAreStable()
{
    QCOMPARE(QString::fromStdString(core::dialogueSceneKey("q-mood", "0")),
             QStringLiteral("dialogue.q-mood.0"));
    QCOMPARE(QString::fromStdString(core::dialogueSceneKey("q-weather", "rain")),
             QStringLiteral("dialogue.q-weather.rain"));
}

void PresetDialogueTest::optionsAreAlwaysFiveWithFixedSlots()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);

    DialogueOptionRequest request;
    request.weatherAvailable = true;
    request.sensitiveUnlocked = true;
    request.sensitiveQuotaLeft = true;

    const std::vector<DialogueOption> options = core::buildDialogueOptions(table, nullptr, request);
    QCOMPARE(options.size(), std::size_t(5));

    // 槽位固定：天气、敏感、随机 ×3（顺序恒定，界面形状稳定）
    QCOMPARE(static_cast<int>(options[0].kind), static_cast<int>(DialogueOptionKind::Weather));
    QCOMPARE(static_cast<int>(options[1].kind), static_cast<int>(DialogueOptionKind::Sensitive));
    for (std::size_t i = 2; i < options.size(); ++i) {
        QCOMPARE(static_cast<int>(options[i].kind), static_cast<int>(DialogueOptionKind::Random));
        QVERIFY(options[i].question != nullptr);
        QVERIFY(options[i].available);
    }
    // 两类固定槽在可用时也必须有题目
    QVERIFY(options[0].question != nullptr);
    QVERIFY(options[0].available);
    QVERIFY(options[1].question != nullptr);
    QVERIFY(options[1].available);

    // 三个随机题互不重复
    QVERIFY(options[2].question->id != options[3].question->id);
    QVERIFY(options[3].question->id != options[4].question->id);
    QVERIFY(options[2].question->id != options[4].question->id);

    // 可用项不带原因文案
    QVERIFY(options[0].reason == nullptr);
    QVERIFY(options[1].reason == nullptr);
}

void PresetDialogueTest::weatherSlotDisabledWithoutApi()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);

    DialogueOptionRequest request;
    request.weatherAvailable = false; // 未配置彩云 key + 城市
    request.sensitiveUnlocked = true;
    request.sensitiveQuotaLeft = true;

    const std::vector<DialogueOption> options = core::buildDialogueOptions(table, nullptr, request);
    QCOMPARE(options.size(), std::size_t(5));
    QVERIFY(!options[0].available); // 天气槽不可用
    QVERIFY(options[0].reason != nullptr);
    QCOMPARE(QString::fromUtf8(options[0].reason),
             QString::fromUtf8(core::kDialogueReasonWeather));
    // 其余槽位不受影响
    QVERIFY(options[1].available);
    QVERIFY(options[2].available);
}

void PresetDialogueTest::sensitiveSlotLockedByAffinity()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);

    DialogueOptionRequest request;
    request.weatherAvailable = true;
    request.sensitiveUnlocked = false; // 好感度 < 5000
    request.sensitiveQuotaLeft = true;

    const std::vector<DialogueOption> options = core::buildDialogueOptions(table, nullptr, request);
    QVERIFY(!options[1].available);
    QCOMPARE(QString::fromUtf8(options[1].reason),
             QString::fromUtf8(core::kDialogueReasonSensitiveLocked));
    QVERIFY(options[0].available);

    // 门槛常量与文档一致
    QCOMPARE(core::kSensitiveUnlockAffinity, 5000);
    QCOMPARE(core::kSensitiveDailyLimit, 3);
}

void PresetDialogueTest::sensitiveSlotBlockedByDailyQuota()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);

    DialogueOptionRequest request;
    request.weatherAvailable = true;
    request.sensitiveUnlocked = true;
    request.sensitiveQuotaLeft = false; // 今日 3 次已用完

    const std::vector<DialogueOption> options = core::buildDialogueOptions(table, nullptr, request);
    QVERIFY(!options[1].available);
    QCOMPARE(QString::fromUtf8(options[1].reason),
             QString::fromUtf8(core::kDialogueReasonSensitiveQuota));
}

void PresetDialogueTest::randomOptionsAvoidRecentIds()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);

    DialogueOptionRequest request;
    request.weatherAvailable = true;
    request.sensitiveUnlocked = true;
    request.sensitiveQuotaLeft = true;

    // 上一轮出现过 q-mood / q-food（rng=nullptr → 保持登记顺序）
    const std::vector<std::string> recent{ "q-mood", "q-food" };
    const std::vector<DialogueOption> options =
        core::buildDialogueOptions(table, nullptr, request, recent);

    // 随机槽优先取没出现过的（q-hotpot / q-plan / q-wish）
    for (std::size_t i = 2; i < options.size(); ++i) {
        const std::string &id = options[i].question->id;
        QVERIFY2(id != "q-mood" && id != "q-food", "最近出现过的随机题应被避开（语料充足时）");
    }

    // 语料不足时允许重复：只给一道普通题也能填满槽位（允许 question == nullptr 的占位）
    PresetDialogueTable small;
    small.loadFromText("q|only|normal|只有一个问题\n"
                       "a|only|0|回答\n");
    DialogueOptionRequest smallRequest;
    smallRequest.randomCount = core::kDialogueRandomOptionCount;
    const std::vector<DialogueOption> smallOptions =
        core::buildDialogueOptions(small, nullptr, smallRequest);
    QCOMPARE(smallOptions.size(), std::size_t(5));
    QVERIFY(smallOptions[2].available);   // 唯一一道题放在第一个随机槽
    QVERIFY(!smallOptions[3].available);  // 其余槽位占位且禁用
    QVERIFY(!smallOptions[4].available);
    QVERIFY(!smallOptions[0].available);  // 无天气题 → 天气槽占位禁用
    QVERIFY(!smallOptions[1].available);  // 无敏感题 → 敏感槽占位禁用
}

void PresetDialogueTest::pickAnswerSlotRandomForQuestion()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);
    const DialogueQuestion *mood = table.find("q-mood");
    QVERIFY(mood != nullptr);

    // 三个预设回答里随机取一个（返回的必然是已登记的 slot）
    ScriptedRandom rng({0.99});
    const std::string slot = core::pickAnswerSlot(*mood, &rng, nullptr);
    QVERIFY2(slot == "0" || slot == "1" || slot == "2", slot.c_str());
    QVERIFY(mood->answerFor(slot) != nullptr);

    // 无随机源 → 稳定取第一个 slot（确定性退化）
    QCOMPARE(QString::fromStdString(core::pickAnswerSlot(*mood, nullptr, nullptr)),
             QStringLiteral("0"));

    // 没有任何回答的问题 → 空 slot（调用方应跳过，不输出文字）
    PresetDialogueTable empty;
    empty.loadFromText("q|q-none|normal|没有问题回答\n");
    const DialogueQuestion *none = empty.find("q-none");
    QVERIFY(none != nullptr);
    QVERIFY(core::pickAnswerSlot(*none, nullptr, nullptr).empty());
}

void PresetDialogueTest::pickAnswerSlotFollowsWeatherKind()
{
    PresetDialogueTable table;
    table.loadFromText(kCorpus);
    const DialogueQuestion *weather = table.find("q-weather");
    QVERIFY(weather != nullptr);

    // 有该天气类型的槽位 → 用该槽位（该 key 下的多条候选由 LineTable 随机）
    QCOMPARE(QString::fromStdString(core::pickAnswerSlot(*weather, nullptr, "rain")),
             QStringLiteral("rain"));
    QVERIFY(weather->answersFor("rain").size() == 3);

    // 该类型没有槽位 → 回落到 unknown
    PresetDialogueTable fallback;
    fallback.loadFromText("q|q-weather|weather|天气？\n"
                          "a|q-weather|sunny|晴\n"
                          "a|q-weather|unknown|不知道\n");
    const DialogueQuestion *fb = fallback.find("q-weather");
    QVERIFY(fb != nullptr);
    QCOMPARE(QString::fromStdString(core::pickAnswerSlot(*fb, nullptr, "snow")),
             QStringLiteral("unknown"));

    // 连 unknown 都没有 → 回落首条回答的槽位（不输出空回答）
    PresetDialogueTable last;
    last.loadFromText(kWeatherOnlyCorpus);
    const DialogueQuestion *only = last.find("q-weather");
    QVERIFY(only != nullptr);
    QCOMPARE(QString::fromStdString(core::pickAnswerSlot(*only, nullptr, "snow")),
             QStringLiteral("sunny"));
}

void PresetDialogueTest::dialoguePosesExist()
{
    std::size_t count = 0;
    const char *const *sensitive = core::dialogueSensitivePoses(count);
    QCOMPARE(count, std::size_t(3));
    for (std::size_t i = 0; i < count; ++i) {
        QVERIFY2(core::poseExists(sensitive[i]), sensitive[i]);
    }
    const char *const *choice = core::dialogueChoicePoses(count);
    QCOMPARE(count, std::size_t(2));
    for (std::size_t i = 0; i < count; ++i) {
        QVERIFY2(core::poseExists(choice[i]), choice[i]);
    }
    QVERIFY(core::poseExists(core::kDialogueNormalPose));

    // 取池立绘：避开上一张（0.99 → 索引 1 = meme-yes）
    ScriptedRandom rng({0.99});
    QCOMPARE(QString::fromLatin1(core::pickDialoguePose(choice, count, &rng, "meme-no")),
             QStringLiteral("meme-yes"));
    // 单张池且正是 avoid → nullptr（调用方保留上一张，不伪装）
    QCOMPARE(core::pickDialoguePose(sensitive, 1, nullptr, "meme-broke"), nullptr);
}

QTEST_GUILESS_MAIN(PresetDialogueTest)
#include "test_preset_dialogue.moc"
