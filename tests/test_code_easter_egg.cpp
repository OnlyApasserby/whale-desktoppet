// P10 迁移后的代码彩蛋单测（docs/ARCHITECTURE.md 附录 D.4）：
//   旧「改写用户源码工作区」的机制（注释段落识别 / 注入 / 幂等原子写）**已整体退役**，
//   本用例只覆盖**仍在使用**的两件事：
//     1) 俏皮话池的安全性（一行一条 / 不含块注释闭合符 / 不以反斜杠结尾 / 无裸引号 / 互不重复）；
//     2) eggCommentLine()：把俏皮话渲染成一行注释，行尾带 kCodeEggMarker（空入参 → 空串）。
//   行为侧（会话内至多一次、产出时插入、目录安全）见 tests/test_python_blocks.cpp。

#include "core/CodeEasterEgg.h"

#include <QtTest>

#include <cstddef>

using namespace whalepet::core;

class TestCodeEasterEgg : public QObject {
    Q_OBJECT

private slots:
    void sayingsAreSafe();
    void eggCommentLineRendersMarker();
};

void TestCodeEasterEgg::sayingsAreSafe()
{
    std::size_t count = 0;
    const char *const *sayings = codeEggSayings(&count);
    QVERIFY(sayings != nullptr);
    QVERIFY(count >= 5);

    QStringList seen;
    for (std::size_t i = 0; i < count; ++i) {
        const QString saying = QString::fromUtf8(sayings[i]);
        QVERIFY(!saying.isEmpty());
        // 一行一条：不含换行（否则会破坏注入/产出脚本的结构）
        QVERIFY2(!saying.contains(QLatin1Char('\n')), qPrintable(saying));
        QVERIFY2(!saying.contains(QLatin1Char('\r')), qPrintable(saying));
        // 不含块注释闭合符（放进任何注释语境都不会提前闭合）
        QVERIFY2(!saying.contains(QStringLiteral("*/")), qPrintable(saying));
        // 不以反斜杠结尾（避免行注释续行吞掉下一行）
        QVERIFY2(!saying.endsWith(QLatin1Char('\\')), qPrintable(saying));
        // 不含裸的半角引号（放进任何语境时不产生歧义）
        QVERIFY2(!saying.contains(QLatin1Char('"')), qPrintable(saying));
        QVERIFY2(!saying.contains(QLatin1Char('\'')), qPrintable(saying));
        // 池内互不重复
        QVERIFY2(!seen.contains(saying), qPrintable(saying));
        seen.push_back(saying);
    }
}

void TestCodeEasterEgg::eggCommentLineRendersMarker()
{
    // 空入参 → 空串（调用方据此跳过插入）
    QVERIFY(eggCommentLine("").empty());

    const std::string line = eggCommentLine("鲸鱼娘路过");
    QCOMPARE(QString::fromStdString(line), QStringLiteral("# 鲸鱼娘路过  (whalepet-egg)"));
    QVERIFY(line.find(kCodeEggMarker) != std::string::npos);
    QVERIFY(line.find('\n') == std::string::npos); // 单行（产出脚本里只占一行）
}

QTEST_GUILESS_MAIN(TestCodeEasterEgg)
#include "test_code_easter_egg.moc"
