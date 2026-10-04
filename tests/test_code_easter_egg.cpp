// EX 彩蛋（experiment/easter-egg1）单测：
//   1) core::injectCodeEgg —— 正则识别注释段落（// 行注释段 / /* */ 多行块注释 / Python #）与幂等注入；
//   2) viewmodel::EasterEggService —— 5% 触发、工作区边界、幂等与「不改代码」。
// 约定同其它用例：随机源可注入（IRandom），故「触发 / 不触发」完全确定。
//
// 关键不变量（对应需求「不影响代码功能」）：把注入的那一行删掉，文件内容必须**逐字节等于原文**。

#include "core/CodeEasterEgg.h"
#include "core/IRandom.h"
#include "viewmodel/EasterEggService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <string>

using namespace whalepet;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(bytes) == bytes.size();
}

// 删除注入行后应与原内容逐字节一致（证明只「加了一行注释」）
QString withoutLine(const std::string &content, int line)
{
    QStringList lines = QString::fromStdString(content).split(QLatin1Char('\n'));
    if (line >= 0 && line < lines.size()) {
        lines.removeAt(line);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

class TestCodeEasterEgg : public QObject {
    Q_OBJECT

private slots:
    // ---------------- core：注释段落识别与注入 ----------------

    void cLineCommentRunInjected()
    {
        const std::string source = "// header\n// second\nint x = 1;\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".cpp", 0);

        QVERIFY(r.changed);
        QCOMPARE(r.insertedLine, 2);
        QVERIFY(r.content.find(core::kCodeEggMarker) != std::string::npos); // 幂等标记随行写入
        QVERIFY(r.content.find("\n// ") != std::string::npos);
        QCOMPARE(withoutLine(r.content, r.insertedLine), QString::fromStdString(source));

        // 直接回灌：带标记的内容再次注入应「未改动」
        const core::CodeEggResult again = core::injectCodeEgg(r.content, ".cpp", 0);
        QVERIFY(!again.changed);
        QCOMPARE(again.content, r.content);
    }

    void cBlockCommentInjected()
    {
        const std::string source = "/*\n * docs\n */\nint x;\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".h", 0);

        QVERIFY(r.changed);
        QCOMPARE(withoutLine(r.content, r.insertedLine), QString::fromStdString(source));
        // 注入行落在块注释内、且以 " * " 开头（保持块注释排版）
        const QStringList lines = QString::fromStdString(r.content).split(QLatin1Char('\n'));
        QCOMPARE(lines.at(r.insertedLine),
                 QStringLiteral(" * ") + QString::fromUtf8(core::codeEggSayings(nullptr)[0])
                     + QStringLiteral("  (whalepet-egg)"));
    }

    void pythonHashCommentInjected()
    {
        const std::string source = "# header\nx = 1\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".py", 0);

        QVERIFY(r.changed);
        QCOMPARE(r.insertedLine, 1);
        QCOMPARE(withoutLine(r.content, r.insertedLine), QString::fromStdString(source));
        QVERIFY(r.content.find("# ") != std::string::npos);
    }

    void pythonTripleQuotedStringIsNotACommentAnchor()
    {
        // 唯一像注释的行在三引号字符串里 → 不应被当成注释段落
        const std::string source = "s = \"\"\"\n# not a comment\n\"\"\"\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".py", 0);
        QVERIFY(!r.changed);
        QCOMPARE(r.content, source);
    }

    void inlineCommentsAreNotAnchors()
    {
        // 行内注释（代码在前）不是「注释段落」，不构成插入点 → 不改动
        const std::string source = "int x = 1; // trailing\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".cpp", 0);
        QVERIFY(!r.changed);
        QCOMPARE(r.content, source);
    }

    void noCommentSectionUntouched()
    {
        const std::string source = "int main() { return 0; }\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".c", 0);
        QVERIFY(!r.changed);
        QCOMPARE(r.content, source);
    }

    void unsupportedExtensionUntouched()
    {
        const std::string source = "// header\nvalue = 1\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".txt", 0);
        QVERIFY(!r.changed);
        QCOMPARE(r.content, source);
    }

    void idempotentWhenMarkerPresent()
    {
        // 已藏过（含 kCodeEggMarker）→ 不再改动
        const std::string source =
            "// header\n// whalepet-egg already here\nint x = 1;\n";
        const core::CodeEggResult r = core::injectCodeEgg(source, ".cpp", 0);
        QVERIFY(!r.changed);
        QCOMPARE(r.content, source);
    }

    void sayingIndexSelectsDifferentSpot()
    {
        const std::string source = "// a\nint x;\n\n// b\nint y;\n";
        const core::CodeEggResult r0 = core::injectCodeEgg(source, ".cpp", 0);
        const core::CodeEggResult r1 = core::injectCodeEgg(source, ".cpp", 1);
        QVERIFY(r0.changed && r1.changed);
        QVERIFY(r0.insertedLine != r1.insertedLine); // 不同下标落到不同注释段落
    }

    void sayingsAreSafe()
    {
        std::size_t count = 0;
        const char *const *pool = core::codeEggSayings(&count);
        QVERIFY(count > 0);
        for (std::size_t i = 0; i < count; ++i) {
            const std::string s = pool[i];
            QVERIFY(!s.empty());
            QVERIFY(s.find('\n') == std::string::npos);
            QVERIFY(s.find("*/") == std::string::npos); // 不会提前闭合块注释
            QVERIFY(s.back() != '\\');                  // 不会续行吞掉后续代码
            QVERIFY(s.find("//") == std::string::npos); // 不引入新的行注释标记
        }
    }

    // ---------------- service：触发概率与工作区边界 ----------------

    void serviceNoOpWhenDisabled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("a.cpp"));
        QVERIFY(writeAll(file, QByteArray("// header\nint x = 1;\n")));

        core::ScriptedRandom rng({ 0.0 }); // 概率命中
        viewmodel::EasterEggService svc(&rng);
        svc.setWorkspace(dir.path());
        svc.setEnabled(false); // 但开关关着
        QVERIFY(!svc.poke());
        QCOMPARE(readAll(file), QByteArray("// header\nint x = 1;\n"));
    }

    void serviceNoOpWhenWorkspaceEmpty()
    {
        core::ScriptedRandom rng({ 0.0 });
        viewmodel::EasterEggService svc(&rng);
        svc.setEnabled(true);
        QVERIFY(svc.workspace().isEmpty());
        QVERIFY(!svc.poke());
    }

    void serviceWorkspaceMustExist()
    {
        core::ScriptedRandom rng({ 0.0 });
        viewmodel::EasterEggService svc(&rng);
        svc.setWorkspace(QStringLiteral("Z:/definitely/not/here"));
        QVERIFY(svc.workspace().isEmpty());
    }

    void serviceNoTriggerOnHighRoll()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("a.cpp"));
        QVERIFY(writeAll(file, QByteArray("// header\nint x = 1;\n")));

        core::ScriptedRandom rng({ 0.99 }); // 0.99 >= 5% → 不触发
        viewmodel::EasterEggService svc(&rng);
        svc.setWorkspace(dir.path());
        svc.setEnabled(true);
        QVERIFY(!svc.poke());
        QCOMPARE(readAll(file), QByteArray("// header\nint x = 1;\n"));
    }

    void serviceForcedTriggerInjectsOnce()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("a.cpp"));
        const QByteArray original = "// header\nint x = 1;\n";
        QVERIFY(writeAll(file, original));

        core::ScriptedRandom rng({ 0.0 }); // 触发 + 选中第一个文件 / 第一句
        viewmodel::EasterEggService svc(&rng);
        svc.setWorkspace(dir.path());
        svc.setEnabled(true);

        QSignalSpy spy(&svc, &viewmodel::EasterEggService::eggPlanted);
        QVERIFY(svc.poke());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(svc.lastFile(), file);
        QVERIFY(!svc.lastSaying().isEmpty());

        const QByteArray after = readAll(file);
        QVERIFY(after != original);
        QVERIFY(after.contains("// "));

        // 幂等：同一文件已藏过 → 再次戳不会重复追加
        QVERIFY(!svc.poke());
        QCOMPARE(readAll(file), after);
    }

    void serviceSkipsUnsupportedAndBinaryFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // 只有「不支持扩展名」与「二进制」两类文件 → 没有可注入的文件
        QVERIFY(writeAll(dir.filePath(QStringLiteral("notes.txt")), QByteArray("// header\n")));
        QVERIFY(writeAll(dir.filePath(QStringLiteral("bin.cpp")),
                         QByteArray("// header\n\0\0binary", 18)));

        core::ScriptedRandom rng({ 0.0 });
        viewmodel::EasterEggService svc(&rng);
        svc.setWorkspace(dir.path());
        svc.setEnabled(true);
        QVERIFY(!svc.poke());
    }

    void serviceScansSubdirectories()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("src/deep")));
        const QString file = dir.filePath(QStringLiteral("src/deep/mod.py"));
        const QByteArray original = "# header\nx = 1\n";
        QVERIFY(writeAll(file, original));

        core::ScriptedRandom rng({ 0.0 });
        viewmodel::EasterEggService svc(&rng);
        svc.setWorkspace(dir.path());
        svc.setEnabled(true);
        QVERIFY(svc.poke());
        QVERIFY(readAll(file).contains("# "));
    }
};

QTEST_GUILESS_MAIN(TestCodeEasterEgg)
#include "test_code_easter_egg.moc"
