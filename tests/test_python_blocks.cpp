// P10 工作台内容层单测（docs/ARCHITECTURE.md 附录 D.4）：
//   1) core::PythonBlocks —— 唯一索引解析 / 入口名与契约 / 组装（头注释 + 逐块**原样** +
//      末尾分发器 + 彩蛋注释；组装不做任何改写）；
//   2) core::CodeEasterEgg —— 话池安全性与 eggCommentLine 的幂等标记；
//   3) **资源契约（磁盘）** —— assets/python/blocks 的每个索引条目都必须有正文、正文定义
//      run_<entry>()、且已在 assets.qrc 登记（防止「新增块漏登记」静默失效）；
//   4) viewmodel::WorkService —— 会话产出（原子写 / **绝不覆盖** / 目录不可用即拒绝 /
//      「戳一戳」每次会话**至多一次** / 取消不产出）。

#include "core/CodeEasterEgg.h"
#include "core/IRandom.h"
#include "core/PythonBlocks.h"
#include "viewmodel/WorkService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <cstddef>
#include <string>
#include <vector>

using namespace whalepet;

namespace {

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString blocksDirPath()
{
    return QString::fromLatin1(WHALEPET_BLOCKS_DIR);
}

std::vector<core::PythonBlock> diskBlocks()
{
    const QString index = QDir(blocksDirPath()).filePath(QStringLiteral("index.txt"));
    return core::parseBlockIndex(readAll(index).toStdString());
}

} // namespace

class WorkBenchContentTest : public QObject {
    Q_OBJECT

private slots:
    // ---- core::PythonBlocks ----
    void parseIndexSkipsCommentsAndBadRows();
    void entryNameAndContract();
    void assembleKeepsBodiesVerbatim();
    void assembleAppendsDispatcherAndEgg();

    // ---- core::CodeEasterEgg ----
    void sayingsAreSafeAndUnique();
    void eggCommentLineCarriesMarker();

    // ---- 资源契约（磁盘）----
    void everyIndexedBlockHasBodyAndQrcEntry();

    // ---- viewmodel::WorkService ----
    void startRejectsUnusableConfiguration();
    void produceWritesScriptAndNeverOverwrites();
    void pokeArmsEggAtMostOncePerSession();
    void cancelProducesNothing();
};

void WorkBenchContentTest::parseIndexSkipsCommentsAndBadRows()
{
    const std::string text = "# 注释行\n"
                             "\n"
                             "hello-world|你好世界|入门|打印问候|标准库\n"
                             "no-requires|缺依赖列|入门|说明\n"
                             "Bad_Id|非法 id|入门|说明|标准库\n"
                             "|空 id|入门|说明|标准库\n";
    const std::vector<core::PythonBlock> blocks = core::parseBlockIndex(text);
    QCOMPARE(blocks.size(), static_cast<std::size_t>(2));
    QCOMPARE(QString::fromStdString(blocks[0].id), QStringLiteral("hello-world"));
    QCOMPARE(QString::fromStdString(blocks[0].requires), QStringLiteral("标准库"));
    QCOMPARE(QString::fromStdString(blocks[1].id), QStringLiteral("no-requires"));
    QCOMPARE(QString::fromStdString(blocks[1].requires), QStringLiteral("标准库")); // 依赖缺省

    QCOMPARE(core::isValidBlockId(std::string("Bad_Id")), false);
    QCOMPARE(core::isValidBlockId(std::string("-x")), false);
    QCOMPARE(core::isValidBlockId(std::string("ok-1")), true);
}

void WorkBenchContentTest::entryNameAndContract()
{
    QCOMPARE(QString::fromStdString(core::blockEntryName("read-csv")), QStringLiteral("run_read_csv"));
    QVERIFY(core::blockEntryName("Bad").empty());
    QVERIFY(core::blockBodyHasEntry("def run_read_csv():\n    pass\n", "read-csv"));
    QVERIFY(!core::blockBodyHasEntry("def other():\n    pass\n", "read-csv"));
}

void WorkBenchContentTest::assembleKeepsBodiesVerbatim()
{
    const std::vector<core::PythonBlock> blocks{
        { "a-block", "甲", "入门", "第一块", "标准库" },
        { "b-block", "乙", "文件", "第二块", "标准库" },
    };
    const std::string bodyA = "def run_a_block():\n    return 1\n"; // 有尾换行
    const std::string bodyB = "def run_b_block():\n    return 2";   // 无尾换行
    const std::string script = core::assembleScript(
        blocks, { bodyA, bodyB }, { "WhalePet 工作台", "生成时间：X" }, "");

    const QString text = QString::fromStdString(script);
    QVERIFY(text.startsWith(QStringLiteral("#!/usr/bin/env python3")));
    QVERIFY(text.contains(QStringLiteral("# WhalePet 工作台")));
    QVERIFY(text.contains(QStringLiteral("# 生成时间：X")));
    QVERIFY(text.contains(QStringLiteral("未经运行验证")));
    // 逐块**原样**：正文整段出现（未做任何改写）
    QVERIFY(text.contains(QString::fromStdString(bodyA)));
    QVERIFY(text.contains(QString::fromStdString(bodyB)));
    QVERIFY(text.contains(QStringLiteral("（a-block）")));
}

void WorkBenchContentTest::assembleAppendsDispatcherAndEgg()
{
    const std::vector<core::PythonBlock> blocks{ { "a-block", "甲", "入门", "说明", "标准库" } };
    const std::string body = "def run_a_block():\n    return 1\n";

    const QString plain = QString::fromStdString(core::assembleScript(blocks, { body }, {}, ""));
    QVERIFY(plain.contains(QStringLiteral("if __name__ == \"__main__\":")));
    QVERIFY(plain.contains(QStringLiteral("\"a-block\": run_a_block,")));
    QVERIFY(!plain.contains(QString::fromLatin1(core::kCodeEggMarker)));

    const QString withEgg = QString::fromStdString(
        core::assembleScript(blocks, { body }, {}, std::string("鲸鱼娘路过")));
    QVERIFY(withEgg.contains(QString::fromLatin1(core::kCodeEggMarker)));
    QVERIFY(withEgg.endsWith(QStringLiteral("# 鲸鱼娘路过  (whalepet-egg)\n")));
}

void WorkBenchContentTest::sayingsAreSafeAndUnique()
{
    std::size_t count = 0;
    const char *const *sayings = core::codeEggSayings(&count);
    QVERIFY(sayings != nullptr);
    QVERIFY(count >= 5);

    QStringList seen;
    for (std::size_t i = 0; i < count; ++i) {
        const QString saying = QString::fromUtf8(sayings[i]);
        QVERIFY(!saying.isEmpty());
        QVERIFY2(!saying.contains(QLatin1Char('\n')), qPrintable(saying));
        QVERIFY2(!saying.contains(QStringLiteral("*/")), qPrintable(saying));
        QVERIFY2(!saying.endsWith(QLatin1Char('\\')), qPrintable(saying));
        QVERIFY2(!saying.contains(QLatin1Char('"')) && !saying.contains(QLatin1Char('\'')),
                 qPrintable(saying));
        QVERIFY2(!seen.contains(saying), qPrintable(saying));
        seen.push_back(saying);
    }
}

void WorkBenchContentTest::eggCommentLineCarriesMarker()
{
    QVERIFY(core::eggCommentLine("").empty());
    QCOMPARE(QString::fromStdString(core::eggCommentLine("测试")),
             QStringLiteral("# 测试  (whalepet-egg)"));
}

void WorkBenchContentTest::everyIndexedBlockHasBodyAndQrcEntry()
{
    const QDir dir(blocksDirPath());
    QVERIFY2(dir.exists(), qPrintable(QStringLiteral("功能块目录不存在: ") + blocksDirPath()));

    const std::vector<core::PythonBlock> blocks = diskBlocks();
    QVERIFY2(blocks.size() >= 5, "唯一索引里的功能块太少（至少应提供 5 个）");

    const QString qrc = readAll(QDir(blocksDirPath()).filePath(QStringLiteral("../../assets.qrc")));
    QVERIFY2(!qrc.isEmpty(), "读不到 assets/assets.qrc（资源登记守卫无法生效）");

    for (const core::PythonBlock &block : blocks) {
        const QString id = QString::fromStdString(block.id);
        const QString body = readAll(dir.filePath(id + QStringLiteral(".txt")));
        QVERIFY2(!body.isEmpty(), qPrintable(QStringLiteral("缺少块正文: ") + id));
        QVERIFY2(core::blockBodyHasEntry(body.toStdString(), block.id),
                 qPrintable(QStringLiteral("块正文缺少入口函数 def %1(")
                                .arg(QString::fromStdString(core::blockEntryName(block.id)))));
        QVERIFY2(!block.title.empty(), qPrintable(id));
        QVERIFY2(!block.summary.empty(), qPrintable(id));
        QVERIFY2(qrc.contains(QStringLiteral("python/blocks/") + id + QStringLiteral(".txt")),
                 qPrintable(QStringLiteral("块未登记进 assets.qrc: ") + id));
    }
}

void WorkBenchContentTest::startRejectsUnusableConfiguration()
{
    viewmodel::WorkService service;
    service.setBlocksRoot(blocksDirPath());
    QVERIFY(service.blocks().size() >= 5);
    QSignalSpy failedSpy(&service, &viewmodel::WorkService::failed);

    QVERIFY(!service.start(QStringList{ QStringLiteral("hello-world") })); // 未设置产出目录
    QCOMPARE(failedSpy.count(), 1);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    service.setOutputDir(tmp.filePath(QStringLiteral("nope")));
    QVERIFY(!service.start(QStringList{ QStringLiteral("hello-world") })); // 目录不存在
    QCOMPARE(failedSpy.count(), 2);

    service.setOutputDir(tmp.path());
    QVERIFY(!service.start(QStringList{})); // 未选功能块
    QCOMPARE(failedSpy.count(), 3);

    QVERIFY(!service.start(QStringList{ QStringLiteral("no-such-block") })); // 未知 id 被过滤
    QCOMPARE(failedSpy.count(), 4);
    QVERIFY(!service.running());
}

void WorkBenchContentTest::produceWritesScriptAndNeverOverwrites()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    viewmodel::WorkService service;
    service.setBlocksRoot(blocksDirPath());
    service.setOutputDir(tmp.path());
    QSignalSpy finishedSpy(&service, &viewmodel::WorkService::finished);

    QVERIFY(service.start(QStringList{ QStringLiteral("hello-world"), QStringLiteral("file-hash") },
                          60000));
    QVERIFY(service.running());
    QVERIFY(service.produceNow());
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!service.running());

    const QString first = service.lastOutputFile();
    QVERIFY(QFile::exists(first));
    QVERIFY(first.endsWith(QStringLiteral(".py")));
    const QString script = readAll(first);
    QVERIFY(script.contains(QStringLiteral("def run_hello_world():")));
    QVERIFY(script.contains(QStringLiteral("def run_file_hash():")));
    QVERIFY(!script.contains(QString::fromLatin1(core::kCodeEggMarker))); // 未戳过 → 无俏皮话

    // 「绝不覆盖」：再次产出 → 新文件（同名自动顺延）
    QVERIFY(service.start(QStringList{ QStringLiteral("hello-world") }));
    QVERIFY(service.produceNow());
    const QString second = service.lastOutputFile();
    QVERIFY(second != first);
    QVERIFY(QFile::exists(first));
    QVERIFY(QFile::exists(second));
}

void WorkBenchContentTest::pokeArmsEggAtMostOncePerSession()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    core::ScriptedRandom rng({ 0.0 }); // nextInt → 0：确定性取第一句
    viewmodel::WorkService service(&rng);
    service.setBlocksRoot(blocksDirPath());
    service.setOutputDir(tmp.path());

    // 开关关闭（默认）：戳一戳不标记
    QVERIFY(service.start(QStringList{ QStringLiteral("hello-world") }, 60000));
    QVERIFY(!service.onPoke());
    service.cancel();

    // 开关开启：本会话**至多一次**
    service.setEnabled(true);
    QVERIFY(service.start(QStringList{ QStringLiteral("hello-world") }, 60000));
    QVERIFY(service.onPoke());
    QVERIFY(service.eggArmed());
    QVERIFY(!service.onPoke()); // 第二次不再触发
    QVERIFY(service.produceNow());
    QVERIFY(readAll(service.lastOutputFile()).contains(QString::fromLatin1(core::kCodeEggMarker)));

    // 新会话：标记复位（可再次触发）
    QVERIFY(service.start(QStringList{ QStringLiteral("hello-world") }, 60000));
    QVERIFY(!service.eggArmed());
    QVERIFY(service.onPoke());
    service.cancel();
}

void WorkBenchContentTest::cancelProducesNothing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    viewmodel::WorkService service;
    service.setBlocksRoot(blocksDirPath());
    service.setOutputDir(tmp.path());
    QSignalSpy finishedSpy(&service, &viewmodel::WorkService::finished);
    QSignalSpy failedSpy(&service, &viewmodel::WorkService::failed);

    QVERIFY(service.start(QStringList{ QStringLiteral("hello-world") }, 60000));
    service.cancel();
    QVERIFY(!service.running());
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(QDir(tmp.path()).entryList(QStringList{ QStringLiteral("*.py") }, QDir::Files).size(), 0);
    QVERIFY(!service.produceNow()); // 会话已取消 → 不再产出
}

QTEST_GUILESS_MAIN(WorkBenchContentTest)
#include "test_python_blocks.moc"
