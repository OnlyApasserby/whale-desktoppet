// P5 聊天层单测：分时问候 / 深夜静默 / 心情分层 / 羁绊专属 / 关键词映射与开关 /
// 主动发言节流与序号 / 真实语料覆盖。见 docs/CHAT.md、docs/ROADMAP-P5.md。
//
// 覆盖两类断言：
//   1) ChatService（场景决策 + 关键词门控）—— 用程序内构造的小语料，边界可控；
//   2) 真实语料一致性 —— 直读 assets/lines/*.txt，保证「代码里引用的场景 key
//      在语料里真有候选」，避免出现「切了场景但一句话都说不出来」。

#include "core/ChatRules.h"
#include "core/LineTable.h"
#include "core/PetStateMachine.h"
#include "core/PoseCatalog.h"
#include "viewmodel/ChatService.h"

#include <QFile>
#include <QString>
#include <QtTest>

namespace {

using whalepet::core::LineTable;
using whalepet::viewmodel::ChatService;

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

// 加载 assets/lines/ 下全部语料；返回是否至少加载到一条
bool loadCorpus(LineTable &table)
{
    const char *const kFiles[] = { "lines.txt", "greet.txt", "bond.txt", "meme.txt" };
    std::size_t total = 0;
    for (const char *name : kFiles) {
        QFile file(QString::fromLatin1(WHALEPET_LINES_DIR) + QStringLiteral("/")
                   + QString::fromLatin1(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        total += table.loadFromText(file.readAll().toStdString());
    }
    return total > 0;
}

} // namespace

class TestChat : public QObject {
    Q_OBJECT
private slots:
    // ---- 关键词：开关门控 + 规则顺序（core/ChatRules.h kKeywordRules 顺序即优先级）----
    void keywordSwitchGatesMatching();
    void keywordRuleOrderAndSubstring();

    // ---- 分时问候 + 深夜静默 ----
    void greetByHourAndNightSilence();

    // ---- 心情分层 / 羁绊专属：只在跨档时播报 ----
    void moodSceneOnTierChange();
    void bondSceneOnThresholdCrossing();

    // ---- 缺语料优雅降级 ----
    void missingCorpusDegradesToSilence();

    // ---- 状态机 speak()：proactive 节流/静默 vs 用户交互不节流 + 序号唯一 ----
    void speakRespectsThrottleAndNightSilence();

    // ---- 真实语料覆盖：21 项关键词立绘与场景、问候/心情/羁绊场景齐全 ----
    void bundledCorpusCoversAllScenes();
    void keywordPosesAllExist();
};

void TestChat::keywordSwitchGatesMatching()
{
    LineTable lines;
    lines.addLine("meme.omg", "x");
    ChatService chat(&lines);

    // 默认关闭：任何文本都不匹配（CHAT.md §4/§7）
    QVERIFY(!chat.keywordAware());
    QCOMPARE(qs(chat.matchText(QStringLiteral("我的天，太离谱了"))), QString());

    chat.setKeywordAware(true);
    QCOMPARE(qs(chat.matchText(QStringLiteral("我的天，太离谱了"))), QStringLiteral("omg"));
    QCOMPARE(qs(chat.matchText(QStringLiteral("完全没有命中词的句子"))), QString());
    QCOMPARE(qs(chat.matchText(QString())), QString());
}

void TestChat::keywordRuleOrderAndSubstring()
{
    LineTable lines;
    ChatService chat(&lines);
    chat.setKeywordAware(true);

    QCOMPARE(qs(chat.matchText(QStringLiteral("谢谢你啦"))), QStringLiteral("thanks"));
    QCOMPARE(qs(chat.matchText(QStringLiteral("今天好累啊"))), QStringLiteral("tired"));
    // 注意规则顺序即优先级：含「帮我」的句子会被 help 先命中，
    // 故 deploy 需用不含更靠前触发词的文本
    QCOMPARE(qs(chat.matchText(QStringLiteral("准备部署上线"))), QStringLiteral("deploy"));
    QCOMPARE(qs(chat.matchText(QStringLiteral("无语了"))), QStringLiteral("smilepain"));
    QCOMPARE(qs(chat.matchText(QStringLiteral("aww"))), QStringLiteral("kyun"));
    QCOMPARE(qs(chat.matchText(QStringLiteral("抱抱"))), QStringLiteral("hug"));
    // 大小写不敏感（DEADLINE → ddl 组 "deadline"）
    QCOMPARE(qs(chat.matchText(QStringLiteral("DEADLINE 快到了"))), QStringLiteral("ddl"));
}

void TestChat::greetByHourAndNightSilence()
{
    LineTable lines;
    lines.addLine("greet.morning", "早上好");
    lines.addLine("greet.evening", "晚上好");
    ChatService chat(&lines);

    QCOMPARE(qs(chat.greetScene(8)), QStringLiteral("greet.morning"));
    // 同一时段（9/10/11 点同属 Forenoon / Morning）不重复问候
    QCOMPARE(qs(chat.greetScene(9)), QString());
    // 深夜静默：23:00–05:59
    QCOMPARE(qs(chat.greetScene(23)), QString());
    QCOMPARE(qs(chat.greetScene(3)), QString());
    // 跨到傍晚时段 → 恢复问候
    QCOMPARE(qs(chat.greetScene(19)), QStringLiteral("greet.evening"));
    QCOMPARE(qs(chat.greetScene(20)), QString());
}

void TestChat::moodSceneOnTierChange()
{
    LineTable lines;
    lines.addLine("bond.low-mood", "低");
    lines.addLine("bond.high-mood", "高");
    ChatService chat(&lines);

    // 首次观察只建立基线，不发言
    QCOMPARE(qs(chat.moodSceneFor(60)), QString());
    // 掉到低落档 → 播报一次
    QCOMPARE(qs(chat.moodSceneFor(30)), QStringLiteral("bond.low-mood"));
    // 同档位内波动不重复
    QCOMPARE(qs(chat.moodSceneFor(35)), QString());
    // 回到中性 → 无专属台词
    QCOMPARE(qs(chat.moodSceneFor(55)), QString());
    // 升到高涨档 → 播报
    QCOMPARE(qs(chat.moodSceneFor(80)), QStringLiteral("bond.high-mood"));
}

void TestChat::bondSceneOnThresholdCrossing()
{
    LineTable lines;
    lines.addLine("bond.l3", "l3");
    lines.addLine("bond.l5", "l5");
    lines.addLine("bond.l7", "l7");
    ChatService chat(&lines);

    QCOMPARE(qs(chat.bondSceneFor(1)), QString()); // 基线
    QCOMPARE(qs(chat.bondSceneFor(3)), QStringLiteral("bond.l3"));
    QCOMPARE(qs(chat.bondSceneFor(4)), QString()); // 同档不重复
    QCOMPARE(qs(chat.bondSceneFor(5)), QStringLiteral("bond.l5"));
    QCOMPARE(qs(chat.bondSceneFor(6)), QString());
    QCOMPARE(qs(chat.bondSceneFor(7)), QStringLiteral("bond.l7"));
    QCOMPARE(qs(chat.bondSceneFor(9)), QString());
}

void TestChat::missingCorpusDegradesToSilence()
{
    LineTable empty;
    ChatService chat(&empty);

    // 语料缺失 → 场景无候选，主动发言全部降级为「不说话」
    QCOMPARE(qs(chat.greetScene(8)), QString());
    QCOMPARE(qs(chat.moodSceneFor(30)), QString());
    QCOMPARE(qs(chat.bondSceneFor(3)), QString());

    // 关键词文本匹配与语料无关（是否切表情由立绘映射决定，无台词不阻塞表情）
    chat.setKeywordAware(true);
    QCOMPARE(qs(chat.matchText(QStringLiteral("我的天"))), QStringLiteral("omg"));
}

void TestChat::speakRespectsThrottleAndNightSilence()
{
    using whalepet::core::Event;
    using whalepet::core::EventType;
    using whalepet::core::PetStateMachine;

    {
        PetStateMachine sm;
        sm.reset(0);
        const whalepet::core::PoseResult first =
            sm.speak({}, "greet.morning", 0, Event::simple(EventType::Tick, 1000), true);
        QCOMPARE(qs(first.lineKey), QStringLiteral("greet.morning"));
        QVERIFY(!first.pose.empty()); // 分时问候不改立绘，沿用上下文姿态

        // ≥6s 节流（core::kSpeechGapMs）
        const whalepet::core::PoseResult throttled =
            sm.speak({}, "greet.morning", 0, Event::simple(EventType::Tick, 2000), true);
        QVERIFY(throttled.lineKey.empty());

        const whalepet::core::PoseResult later =
            sm.speak({}, "greet.morning", 0, Event::simple(EventType::Tick, 8000), true);
        QCOMPARE(qs(later.lineKey), QStringLiteral("greet.morning"));
    }

    {
        // 深夜静默：proactive 台词被抑制
        PetStateMachine sm;
        sm.reset(0);
        sm.handle(Event::clock(23, 1)); // m_hour = 23
        const whalepet::core::PoseResult r =
            sm.speak({}, "greet.morning", 0, Event::simple(EventType::Tick, 100000), true);
        QVERIFY(r.lineKey.empty());
    }

    {
        // 用户交互（proactive=false）永不节流，且序号每次唯一
        PetStateMachine sm;
        sm.reset(0);
        const whalepet::core::PoseResult a =
            sm.speak("meme-kyun", "meme.kyun", 6000, Event::simple(EventType::Tick, 1000), false);
        const whalepet::core::PoseResult b =
            sm.speak("meme-doge", "meme.doge", 6000, Event::simple(EventType::Tick, 1100), false);
        QCOMPARE(qs(a.lineKey), QStringLiteral("meme.kyun"));
        QCOMPARE(qs(b.lineKey), QStringLiteral("meme.doge"));
        QCOMPARE(qs(a.pose), QStringLiteral("meme-kyun"));
        QVERIFY(a.lineSerial != b.lineSerial);
    }

    {
        // 面板打开时的抑制只作用于 proactive
        PetStateMachine sm;
        sm.reset(0);
        sm.setSuppressed(true);
        QVERIFY(sm
                    .speak({}, "greet.morning", 0, Event::simple(EventType::Tick, 100000), true)
                    .lineKey
                    .empty());
        QCOMPARE(qs(sm.speak("meme-omg", "meme.omg", 6000,
                             Event::simple(EventType::Tick, 100000), false)
                        .lineKey),
                 QStringLiteral("meme.omg"));
    }
}

void TestChat::bundledCorpusCoversAllScenes()
{
    LineTable lines;
    if (!loadCorpus(lines)) {
        QSKIP("assets/lines/ 语料不可用");
    }

    // 分时问候 5 个时段全部有候选（Night 无场景，深夜静默）
    const char *const kGreetScenes[] = { "greet.morning", "greet.forenoon", "greet.noon",
                                        "greet.afternoon", "greet.evening" };
    for (const char *scene : kGreetScenes) {
        QVERIFY2(lines.hasScene(scene), scene);
    }

    // 心情分层 + 羁绊专属
    const char *const kBondScenes[] = { "bond.low-mood", "bond.high-mood", "bond.l3", "bond.l5",
                                       "bond.l7" };
    for (const char *scene : kBondScenes) {
        QVERIFY2(lines.hasScene(scene), scene);
    }

    // 21 项关键词立绘对应的 meme.<id> 场景全部有候选
    for (std::size_t i = 0; i < whalepet::core::kKeywordPoseCount; ++i) {
        const std::string scene =
            whalepet::core::keywordSceneKey(whalepet::core::kKeywordPoses[i].id);
        QVERIFY2(lines.hasScene(scene), scene.c_str());
    }
}

void TestChat::keywordPosesAllExist()
{
    // 关键词 → 立绘映射的目标立绘必须在 PoseNames.h 中存在，
    // 否则关键词命中会退化成通用 curious 表情（静默的错误）。
    for (std::size_t i = 0; i < whalepet::core::kKeywordPoseCount; ++i) {
        const char *pose = whalepet::core::kKeywordPoses[i].pose;
        QVERIFY2(whalepet::core::poseExists(pose), pose);
    }
}

QTEST_GUILESS_MAIN(TestChat)
#include "test_chat.moc"
