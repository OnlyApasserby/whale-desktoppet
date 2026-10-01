// P6 热词单测：自定义热词优先匹配 / 归一化与去重 / hotwords 表 CRUD /
// 显式录入不受 keyword_aware 门控。
//
// 约定同 test_content.cpp：内存库，headless（QTEST_GUILESS_MAIN）。
// 参考：docs/CHAT.md §4、docs/DATA-MODEL.md §3.9、docs/SETTINGS.md §2。
//
// 用例用词说明（刻意避开内置触发词，避免断言被内置规则"接住"而失去意义）：
//   - "开服" 不在任何 kKeywordRules 分组里 → 只能由自定义热词命中；
//   - "上线" 是内置 deploy 词 → 正好用来证明"自定义优先于内置"。

#include "core/ChatRules.h"
#include "core/LineTable.h"
#include "model/Database.h"
#include "model/HotwordRepo.h"
#include "model/Schema.h"
#include "viewmodel/ChatService.h"

#include <QString>
#include <QSqlQuery>
#include <QtTest>

#include <memory>
#include <vector>

using namespace whalepet;
using namespace whalepet::core;

namespace {

std::unique_ptr<model::Database> makeDb()
{
    auto db = std::make_unique<model::Database>();
    const bool ok = db->openMemory();
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return db;
}

// matchKeyword() 无命中时返回 nullptr（core/ChatRules.h 的 API 约定）。若只提供
// qs(const std::string&)，裸指针会先经「const char* → std::string」隐式转换，
// 而 basic_string(const char*) 的契约要求非空 → strlen(nullptr) 访问违例，
// 且崩在 qs 函数体之前（正是 TRAP-P6-001）。故为裸指针单独提供重载并显式判空。
QString qs(const char *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

} // namespace

class TestHotword : public QObject {
    Q_OBJECT
private slots:
    // ---- 纯逻辑：匹配优先级 ----
    void customHotwordBeatsBuiltin();
    void customHotwordOrderIsPriority();
    void dirtyHotwordIsSkipped();
    void keywordIdValidation();

    // ---- 持久化：归一化 / 覆盖 / 删除 / 顺序 / 建表 ----
    void repoNormalizesAndDeduplicates();
    void repoRejectsInvalidInput();
    void repoKeepsInsertOrderAndRemoves();
    void repoTableRebuiltByMigrate();

    // ---- 集成：显式录入 vs 被动监听 ----
    void explicitInputIgnoresKeywordSwitch();
    void passiveMatchingStillNeedsSwitch();
};

void TestHotword::customHotwordBeatsBuiltin()
{
    // "上线" 是内置 deploy 词（kDeployWords）→ 无自定义时命中 deploy
    QCOMPARE(qs(matchKeyword(QStringLiteral("准备上线").toStdString())), QStringLiteral("deploy"));

    // 自定义把 "上线" 指到 sike → 自定义优先，内置被覆盖
    const std::vector<CustomHotword> custom = { { "上线", "sike" } };
    QCOMPARE(qs(matchKeyword(QStringLiteral("准备上线").toStdString(), custom)),
             QStringLiteral("sike"));

    // 自定义未命中时仍回落到内置规则
    QCOMPARE(qs(matchKeyword(QStringLiteral("明天就是 deadline").toStdString(), custom)),
             QStringLiteral("ddl"));

    // 兼容重载（无自定义）与显式传空表等价
    QCOMPARE(qs(matchKeyword(QStringLiteral("明天就是 deadline").toStdString())),
             QStringLiteral("ddl"));
    QCOMPARE(qs(matchKeyword(QStringLiteral("明天就是 deadline").toStdString(),
                             std::vector<CustomHotword>{})),
             QStringLiteral("ddl"));
}

void TestHotword::customHotwordOrderIsPriority()
{
    // 两条热词都能命中同一文本 → 靠前的赢（列表顺序 = 录入顺序 = 优先级）
    const std::vector<CustomHotword> forward = {
        { "开服", "sike" },
        { "服", "doge" },
    };
    QCOMPARE(qs(matchKeyword(QStringLiteral("今晚开服").toStdString(), forward)),
             QStringLiteral("sike"));

    const std::vector<CustomHotword> reversed = {
        { "服", "doge" },
        { "开服", "sike" },
    };
    QCOMPARE(qs(matchKeyword(QStringLiteral("今晚开服").toStdString(), reversed)),
             QStringLiteral("doge"));
}

void TestHotword::dirtyHotwordIsSkipped()
{
    // 空词 / 空 id / 非法 id 的脏数据必须跳过，不能把后面的合法热词一起挡掉
    const std::vector<CustomHotword> custom = {
        { "", "omg" },               // 空词
        { "开服", "" },              // 空 id
        { "开服", "not-a-keyword" }, // 非法 id
        { "开服", "sike" },          // 合法（唯一有效项）
    };
    QCOMPARE(qs(matchKeyword(QStringLiteral("今晚开服").toStdString(), custom)),
             QStringLiteral("sike"));

    // 脏数据不会被"误命中"：只有自定义、且全部无效时结果为 nullptr
    const std::vector<CustomHotword> allDirty = { { "", "omg" }, { "开服", "nope" } };
    QCOMPARE(qs(matchKeyword(QStringLiteral("今晚开服").toStdString(), allDirty)), QString());
}

void TestHotword::keywordIdValidation()
{
    QVERIFY(keywordIdValid("omg"));
    QVERIFY(keywordIdValid("hug")); // 无立绘但仍是合法 id（仍在 kKeywordRules 中）
    QVERIFY(!keywordIdValid(""));
    QVERIFY(!keywordIdValid("meme-omg")); // 立绘名不是 id
    QVERIFY(!keywordIdValid("not-a-keyword"));
}

void TestHotword::repoNormalizesAndDeduplicates()
{
    auto db = makeDb();
    model::HotwordRepo repo(db.get());

    QVERIFY(repo.upsert(QStringLiteral("  OMG  "), QStringLiteral("omg"), 1000));
    QVector<model::Hotword> items = repo.loadAll();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].word, QStringLiteral("omg")); // trim + toLower 后入库
    QCOMPARE(items[0].keywordId, QStringLiteral("omg"));

    // 大小写变体 → 归一化后是同一行，不新增
    QVERIFY(repo.upsert(QStringLiteral("omg"), QStringLiteral("doge"), 2000));
    items = repo.loadAll();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].keywordId, QStringLiteral("doge")); // 覆盖为新 id

    // 归一化口径对删除同样生效：用未归一化的原文也能删掉
    QVERIFY(repo.remove(QStringLiteral("  OmG ")));
    QCOMPARE(repo.loadAll().size(), 0);
}

void TestHotword::repoRejectsInvalidInput()
{
    auto db = makeDb();
    model::HotwordRepo repo(db.get());

    QVERIFY(!repo.upsert(QStringLiteral("   "), QStringLiteral("omg"), 1));  // 空词
    QVERIFY(!repo.upsert(QStringLiteral("开服"), QStringLiteral("nope"), 1)); // 非法 id
    QVERIFY(!repo.upsert(QStringLiteral("开服"), QString(), 1));              // 空 id
    QVERIFY(!repo.upsert(QStringLiteral("开服"), QStringLiteral("work-deploy"), 1)); // 立绘名非 id
    QCOMPARE(repo.loadAll().size(), 0); // 四条全被拒 → 一行都没写进去

    QVERIFY(repo.upsert(QStringLiteral("开服"), QStringLiteral("sike"), 1));
    QCOMPARE(repo.loadAll().size(), 1);
}

void TestHotword::repoKeepsInsertOrderAndRemoves()
{
    auto db = makeDb();
    model::HotwordRepo repo(db.get());

    QVERIFY(repo.upsert(QStringLiteral("甲"), QStringLiteral("omg"), 1));
    QVERIFY(repo.upsert(QStringLiteral("乙"), QStringLiteral("doge"), 2));
    QVERIFY(repo.upsert(QStringLiteral("丙"), QStringLiteral("kyun"), 3));

    QVector<model::Hotword> items = repo.loadAll();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items[0].word, QStringLiteral("甲")); // 按 id 升序 = 录入顺序 = 优先级
    QCOMPARE(items[1].word, QStringLiteral("乙"));
    QCOMPARE(items[2].word, QStringLiteral("丙"));

    // 覆盖不改变位置（同一条记录被更新，而非追加）
    QVERIFY(repo.upsert(QStringLiteral("甲"), QStringLiteral("sike"), 4));
    items = repo.loadAll();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items[0].word, QStringLiteral("甲"));
    QCOMPARE(items[0].keywordId, QStringLiteral("sike"));

    QVERIFY(repo.remove(QStringLiteral("乙")));
    items = repo.loadAll();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items[0].word, QStringLiteral("甲"));
    QCOMPARE(items[1].word, QStringLiteral("丙"));

    QVERIFY(repo.clear());
    QCOMPARE(repo.loadAll().size(), 0);
}

void TestHotword::repoTableRebuiltByMigrate()
{
    // hotwords 写在 v1 脚本里、**不升版本号**（DATA-MODEL §3.9）。这里模拟
    // "老库缺这张表" 的升级路径：删表后重新 migrate 应能补建，且不丢其它数据。
    auto db = makeDb();
    QVERIFY(db->isOpen());

    model::HotwordRepo repo(db.get());
    QVERIFY(repo.upsert(QStringLiteral("开服"), QStringLiteral("sike"), 1));
    QCOMPARE(repo.loadAll().size(), 1);

    QSqlQuery drop(db->db());
    QVERIFY(drop.exec(QStringLiteral("DROP TABLE hotwords")));

    QSqlDatabase handle = db->db();
    QVERIFY(model::Schema::migrate(handle));  // 幂等补建
    QCOMPARE(db->schemaVersion(), model::Schema::kVersion); // 版本号不漂移
    QCOMPARE(repo.loadAll().size(), 0); // 表被重建 → 内容为空（但程序不崩、可继续写入）

    QVERIFY(repo.upsert(QStringLiteral("回归"), QStringLiteral("bugtalk"), 2));
    QCOMPARE(repo.loadAll().size(), 1);
}

void TestHotword::explicitInputIgnoresKeywordSwitch()
{
    LineTable lines;
    lines.addLine("meme.sike", "x");
    viewmodel::ChatService chat(&lines);

    QVERIFY(!chat.keywordAware()); // 被动监听开关：默认关
    chat.setCustomHotwords({ { "开服", "sike" } });

    // 开关关着 → 被动匹配必须为空……
    QCOMPARE(qs(chat.matchText(QStringLiteral("今晚开服"))), QString());
    // ……但显式录入（全局热键 / 菜单）必须命中，否则关着开关就没法用
    QCOMPARE(qs(chat.matchHotword(QStringLiteral("今晚开服"))), QStringLiteral("sike"));
    // 显式路径同样回落到内置规则
    QCOMPARE(qs(chat.matchHotword(QStringLiteral("明天就是 deadline"))), QStringLiteral("ddl"));
    QCOMPARE(qs(chat.matchHotword(QStringLiteral("毫不相关的句子"))), QString());
    QCOMPARE(qs(chat.matchHotword(QString())), QString());
}

void TestHotword::passiveMatchingStillNeedsSwitch()
{
    LineTable lines;
    viewmodel::ChatService chat(&lines);
    chat.setCustomHotwords({ { "上线", "sike" } }); // 覆盖内置的 "上线"→deploy

    chat.setKeywordAware(true);
    QCOMPARE(qs(chat.matchText(QStringLiteral("准备上线"))), QStringLiteral("sike"));

    // 清空热词表 → 回到内置规则
    chat.setCustomHotwords({});
    QCOMPARE(qs(chat.matchText(QStringLiteral("准备上线"))), QStringLiteral("deploy"));

    chat.setKeywordAware(false);
    QCOMPARE(qs(chat.matchText(QStringLiteral("准备上线"))), QString());
}

QTEST_GUILESS_MAIN(TestHotword)
#include "test_hotword.moc"
