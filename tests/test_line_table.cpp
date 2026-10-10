#include <QtTest>
#include <QFile>

#include "core/DialoguePoseRules.h"
#include "core/LineTable.h"
#include "core/PetStateMachine.h"
#include "core/PoseCatalog.h"
#include "core/PresetDialogue.h"

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

    // ---- 预设对话扩充语料：口径（题数 / 三答）+ 按题立绘池双向一致性 ----
    void bundledDialogueCorpusMatchesDocumentedScale();
    void bundledDialogueQuestionsHavePosePools();
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
#ifdef WHALEPET_LINES_DIR
    // P5 起语料按场景分文件（CHAT.md §1）：一致性检查必须覆盖全部文件，
    // 否则「把某组场景挪到新文件却忘了加载」会被漏掉（正是本次 meme.* 的情况）。
    const char *const kFiles[] = { "lines.txt", "greet.txt", "bond.txt", "meme.txt" };
    std::size_t count = 0;
    for (const char *name : kFiles) {
        QFile file(QString::fromUtf8(WHALEPET_LINES_DIR) + QStringLiteral("/")
                   + QString::fromUtf8(name));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(QStringLiteral("无法打开台词文件: %1").arg(file.fileName())));
        count += table.loadFromText(file.readAll().toStdString());
    }
    QVERIFY2(count > 0, "台词资源解析结果为空");
#else
    QSKIP("未定义 WHALEPET_LINES_DIR，跳过语料一致性检查");
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

void LineTableTest::bundledDialogueCorpusMatchesDocumentedScale()
{
#ifdef WHALEPET_LINES_DIR
    QFile file(QString::fromUtf8(WHALEPET_LINES_DIR) + QStringLiteral("/dialogue.txt"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("无法打开预设对话语料: %1").arg(file.fileName())));

    PresetDialogueTable table;
    const std::size_t questions = table.loadFromText(file.readAll().toStdString());
    // 规模口径（docs/DIALOGUE-CORPUS.md）：11 既有 + 115 扩充 = 126 题
    QCOMPARE(questions, std::size_t(126));
    QCOMPARE(table.countOf(DialogueCategory::Normal), std::size_t(106));
    QCOMPARE(table.countOf(DialogueCategory::Choice), std::size_t(2));
    QCOMPARE(table.countOf(DialogueCategory::Sensitive), std::size_t(17));
    QCOMPARE(table.countOf(DialogueCategory::Weather), std::size_t(1));

    const char *const kSlots[] = { "0", "1", "2" };
    std::size_t answers = 0;
    for (const DialogueQuestion &question : table.questions()) {
        answers += question.answerCount();
        QVERIFY2(question.answerCount() > 0, question.id.c_str());
        if (question.category == DialogueCategory::Weather) {
            continue; // 天气题 slot 是天气类型，由 pickAnswerSlotFollowsWeatherKind 覆盖
        }
        // 非天气题：每题三条回答（slot 0 / 1 / 2 各一条；同 slot 仍可再加候选）
        QCOMPARE(question.answerSlots().size(), std::size_t(3));
        for (const char *slot : kSlots) {
            QCOMPARE(question.answersFor(slot).size(), std::size_t(1));
        }
    }
    QCOMPARE(answers, std::size_t(390)); // 45 既有 + 345 扩充
#else
    QSKIP("未定义 WHALEPET_LINES_DIR，跳过预设对话语料口径检查");
#endif
}

void LineTableTest::bundledDialogueQuestionsHavePosePools()
{
#ifdef WHALEPET_LINES_DIR
    QFile file(QString::fromUtf8(WHALEPET_LINES_DIR) + QStringLiteral("/dialogue.txt"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("无法打开预设对话语料: %1").arg(file.fileName())));

    PresetDialogueTable table;
    table.loadFromText(file.readAll().toStdString());
    QVERIFY(!table.empty());

    // 正向：每道题的立绘都必须可达 —— 按题池命中则池内 key 必须存在；
    // 未命中则所属类别的兜底池必须非空（扩充语料的 8 道操作 / 说明题走这里）。
    for (const DialogueQuestion &question : table.questions()) {
        const char *pool[3] = { nullptr, nullptr, nullptr };
        const std::size_t count = dialoguePosesForQuestion(question.id, pool);
        if (count > 0) {
            for (std::size_t i = 0; i < count; ++i) {
                QVERIFY2(poseExists(pool[i]), pool[i]);
            }
            continue;
        }
        std::size_t fallbackCount = 0;
        const char *const *fallback = nullptr;
        switch (question.category) {
        case DialogueCategory::Normal:
            QVERIFY(poseExists(kDialogueNormalPose));
            break;
        case DialogueCategory::Sensitive:
            fallback = dialogueSensitivePoses(fallbackCount);
            QVERIFY(fallbackCount > 0);
            QVERIFY(poseExists(fallback[0]));
            break;
        case DialogueCategory::Choice:
            fallback = dialogueChoicePoses(fallbackCount);
            QVERIFY(fallbackCount > 0);
            QVERIFY(poseExists(fallback[0]));
            break;
        case DialogueCategory::Weather:
            break; // 由天气类型决定，weatherKindPosesExist 已覆盖
        }
    }

    // 反向：按题池里的 id 必须存在于语料（防止 id 打错后静默回落、永不生效）
    for (const DialoguePoseRule &rule : kDialoguePoseRules) {
        QVERIFY2(table.find(rule.id) != nullptr, rule.id);
    }
#else
    QSKIP("未定义 WHALEPET_LINES_DIR，跳过按题立绘池一致性检查");
#endif
}

QTEST_GUILESS_MAIN(LineTableTest)
#include "test_line_table.moc"
