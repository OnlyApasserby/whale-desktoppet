#include <QtTest>
#include <QFile>

#include "core/LineTable.h"
#include "core/PetStateMachine.h"

#include <vector>

using namespace whalepet::core;
namespace core = whalepet::core;

namespace {
constexpr std::int64_t kBase = 2'000'000;

// 保证每次交互都跨过台词节流窗口
constexpr std::int64_t kStep = kSpeechGapMs + 1000;
} // namespace

class LineTableTest : public QObject {
    Q_OBJECT

private slots:
    // ---- 解析 ----
    void parsesValidLines();
    void skipsMalformedLines();
    void separatesOnFirstPipeOnly();
    void emptyKeyOrTextIgnored();

    // ---- 取用 ----
    void missingSceneReturnsEmpty();
    void pickFollowsRandomAndAvoidsRepeat();
    void pickWithoutRandomIsStable();

    // ---- 计划/实际一致性：真实语料必须覆盖状态机会说的话 ----
    void bundledLinesCoverStateMachineScenes();
};

void LineTableTest::parsesValidLines()
{
    LineTable table;
    const std::size_t n = table.loadFromText(
        "# 注释行\n"
        "\n"
        "click.head|摸头的话……也不是不行啦。\r\n"
        "  click.head  |  唔……舒服得想打呼噜了。  \n"
        "menu.feed|开动啦！\n");

    QCOMPARE(n, std::size_t(3));
    QCOMPARE(table.size(), std::size_t(3));
    QVERIFY(table.hasScene("click.head"));
    QVERIFY(table.hasScene("menu.feed"));

    // CRLF 与首尾空白都被剥掉（Windows 下编辑语料不会带上脏字符）
    QCOMPARE(QString::fromStdString(table.pick("menu.feed", nullptr)),
             QStringLiteral("开动啦！"));
}

void LineTableTest::skipsMalformedLines()
{
    LineTable table;
    // 缺分隔符 / 空 key / 空文本 → 全部跳过，且不影响同文件里的合法行
    const std::size_t n = table.loadFromText(
        "没有分隔符的一行\n"
        "|只有文本\n"
        "只有关键字|\n"
        "click.body|怎么啦？我一直都在哦。\n");

    QCOMPARE(n, std::size_t(1));
    QCOMPARE(table.size(), std::size_t(1));
    QVERIFY(table.hasScene("click.body"));
}

void LineTableTest::separatesOnFirstPipeOnly()
{
    LineTable table;
    table.loadFromText("evt.quest|前半|后半\n");
    QCOMPARE(QString::fromStdString(table.pick("evt.quest", nullptr)),
             QStringLiteral("前半|后半"));
}

void LineTableTest::emptyKeyOrTextIgnored()
{
    LineTable table;
    table.addLine("", "文本");
    table.addLine("key", "");
    QVERIFY(table.empty());
    QCOMPARE(table.size(), std::size_t(0));
}

void LineTableTest::missingSceneReturnsEmpty()
{
    LineTable table;
    table.loadFromText("click.head|摸头的话……也不是不行啦。\n");

    ScriptedRandom rng({});
    QVERIFY(table.pick("no.such.scene", &rng).empty());
    QVERIFY(!table.hasScene("no.such.scene"));
}

void LineTableTest::pickFollowsRandomAndAvoidsRepeat()
{
    LineTable table;
    table.loadFromText("k|A\nk|B\nk|C\n");

    // ScriptedRandom 空序列 → next01() 恒为 1.0 → nextInt(n) 恒为 0
    // 因此「取第一条候选」在最近窗口去重下会依次推进：A → B → C →（无新候选）回到 A
    ScriptedRandom rng({});
    QCOMPARE(QString::fromStdString(table.pick("k", &rng)), QStringLiteral("A"));
    QCOMPARE(QString::fromStdString(table.pick("k", &rng)), QStringLiteral("B"));
    QCOMPARE(QString::fromStdString(table.pick("k", &rng)), QStringLiteral("C"));
    QCOMPARE(QString::fromStdString(table.pick("k", &rng)), QStringLiteral("A"));

    // 清空去重窗口后回到首条
    table.clearRecent();
    QCOMPARE(QString::fromStdString(table.pick("k", &rng)), QStringLiteral("A"));
}

void LineTableTest::pickWithoutRandomIsStable()
{
    LineTable table;
    table.loadFromText("k|A\nk|B\n");
    // rng == nullptr：不引入随机，可预期地取第一条
    QCOMPARE(QString::fromStdString(table.pick("k", nullptr)), QStringLiteral("A"));
}

void LineTableTest::bundledLinesCoverStateMachineScenes()
{
    LineTable table;
#ifdef WHALEPET_LINES_FILE
    QFile file(QString::fromUtf8(WHALEPET_LINES_FILE));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("无法打开台词文件: %1").arg(file.fileName())));

    const std::size_t count = table.loadFromText(file.readAll().toStdString());
    QVERIFY2(count > 0, "台词资源解析结果为空");
#else
    QSKIP("未定义 WHALEPET_LINES_FILE，跳过语料一致性检查");
#endif

    // 让状态机真实跑一遍 P2 的全部交互，收集它给出的 lineKey，再要求语料里都有候选。
    // 这样「改姿态名 / 改场景 key 但忘了补台词」会被测试挡住。
    ScriptedRandom rng({});
    PetStateMachine sm(&rng);
    sm.reset(kBase);

    struct Case {
        const char *label;
        Event event;
    };
    const std::vector<Case> cases = {
        {"click-head", Event::click(Zone::Head, kBase + kStep * 1)},
        {"click-belly", Event::click(Zone::Belly, kBase + kStep * 2)},
        {"click-tail", Event::click(Zone::Tail, kBase + kStep * 3)},
        {"click-body", Event::click(Zone::Body, kBase + kStep * 4)},
        {"triple-click", Event::simple(EventType::TripleClick, kBase + kStep * 5)},
        {"menu-feed", Event::simple(EventType::Feed, kBase + kStep * 6)},
        {"menu-tease", Event::simple(EventType::Tease, kBase + kStep * 7)},
        {"menu-praise", Event::simple(EventType::Praise, kBase + kStep * 8)},
        {"drag-start", Event::simple(EventType::DragStart, kBase + kStep * 9)},
        {"level-up", Event::simple(EventType::LevelUp, kBase + kStep * 10)},
        {"achievement", Event::simple(EventType::AchievementUnlocked, kBase + kStep * 11)},
        {"quest-done", Event::simple(EventType::QuestDone, kBase + kStep * 12)},
        {"keyword-omg", Event::keywordHit("omg", kBase + kStep * 13)},
    };

    int checked = 0;
    for (const Case &c : cases) {
        if (c.event.type == EventType::DragStart) {
            sm.handle(c.event); // 拖拽中不主动说话，仅驱动状态
            continue;
        }
        const PoseResult r = sm.handle(c.event);
        if (r.lineKey.empty()) {
            continue; // 节流/静默导致的不说话是合法情况
        }
        QVERIFY2(table.hasScene(r.lineKey),
                 qPrintable(QStringLiteral("%1: 状态机给出的场景 key '%2' 在台词资源中没有候选")
                                .arg(QString::fromUtf8(c.label),
                                     QString::fromStdString(r.lineKey))));
        ++checked;
    }

    // 至少大部分交互应该真的说了话，否则这个测试是空转的
    QVERIFY2(checked >= 5, qPrintable(QStringLiteral("实际校验到的台词场景过少: %1").arg(checked)));
}

QTEST_GUILESS_MAIN(LineTableTest)
#include "test_line_table.moc"
