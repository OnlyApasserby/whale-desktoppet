// EX8 工作台「学习」栏单测（docs/STUDY.md，用例清单见 cmake/Tests.cmake）：
//   1) core::PythonImports —— import 归位（仅顶层 / 单行完整语句；落在注释头之后）与去重
//      （按「去行尾注释 + 折叠空白」比较，保留首次出现）+ 无 import 零改动 + 幂等；
//   2) viewmodel::WorkService::blockBody —— 按 id 读正文（非法 id / 缺失返回空）；
//   3) viewmodel::StudyService —— 手动导入（未知 id / 重复跳过）、外部代码条目、
//      只读阅读（功能块实时读源）、拼接预览（按导入顺序 + 既有组装口径 + import 归位去重）、
//      失效源处理、JSON 往返（study_imported）；
//   4) ui::StudyPanel / ui::StudyUiPlugin —— WorkbenchPage 贡献点（标题 / 顺序 / 懒建幂等）、
//      页内导入 / 移出 / 拼接、宿主清单注入与清单变化回写（不回声）。

#include "core/PythonBlocks.h"
#include "core/PythonImports.h"
#include "plugin/PluginRegistry.h"
#include "view/ui/StudyPanel.h"
#include "view/ui/StudyUiPlugin.h"
#include "view/ui/UiContributionHost.h"
#include "view/ui/WorkBenchPanel.h"
#include "viewmodel/StudyService.h"
#include "viewmodel/WorkService.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <string>

using namespace whalepet;
using namespace whalepet::core;
using namespace whalepet::plugin;

namespace {

QString blocksDirPath()
{
    return QString::fromLatin1(WHALEPET_BLOCKS_DIR);
}

// 指向磁盘内容层的工作台服务（学习页功能块的唯一来源）
std::unique_ptr<viewmodel::WorkService> makeWorkService()
{
    auto service = std::make_unique<viewmodel::WorkService>();
    service->setBlocksRoot(blocksDirPath());
    return service;
}

bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) >= 0;
}

int countOf(const QString &text, const QString &needle)
{
    return text.count(needle);
}

} // namespace

class StudyTest : public QObject {
    Q_OBJECT

private slots:
    // ---- core::PythonImports ----
    void hoistMovesImportsAfterCommentHead();
    void hoistDedupesByNormalizedStatement();
    void hoistKeepsIndentedAndMultilineImports();
    void hoistIsNoOpAndIdempotent();

    // ---- viewmodel::WorkService::blockBody ----
    void blockBodyRejectsBadIdsAndMissingBlocks();

    // ---- viewmodel::StudyService ----
    void importBlocksSkipsUnknownAndDuplicates();
    void externalItemsAndJsonRoundTrip();
    void assemblyFollowsImportOrderAndHoistsImports();
    void unusableSourcesAreListedButNotAssembled();

    // ---- ui::StudyPanel / ui::StudyUiPlugin ----
    void contributionIsAWorkbenchPage();
    void panelImportsAndPreviews();
    void hostJsonIsInjectedAndChangesAreWrittenBack();
};

void StudyTest::hoistMovesImportsAfterCommentHead()
{
    const std::string code = "#!/usr/bin/env python3\n"
                             "# 头注释\n"
                             "\n"
                             "import os\n"
                             "x = 1\n"
                             "import sys\n"
                             "print(x)\n";
    const QString out = QString::fromStdString(core::hoistImports(code));

    // 注释头（shebang / 编码行 / 生成说明）留在最上方；import 成为第一批语句
    QVERIFY(out.startsWith(QStringLiteral("#!/usr/bin/env python3\n# 头注释\n")));
    const int osIndex = out.indexOf(QStringLiteral("import os"));
    const int sysIndex = out.indexOf(QStringLiteral("import sys"));
    const int codeIndex = out.indexOf(QStringLiteral("x = 1"));
    QVERIFY(osIndex > 0);
    QVERIFY(sysIndex > osIndex);
    QVERIFY(codeIndex > sysIndex);
    // 非 import 语句的先后顺序不变
    QVERIFY(out.indexOf(QStringLiteral("print(x)")) > codeIndex);
}

void StudyTest::hoistDedupesByNormalizedStatement()
{
    const std::string code = "import os\n"
                             "import  os   # 行尾注释不参与比较\n"
                             "import\tos\n"
                             "from pathlib import Path\n"
                             "from pathlib import  Path\n"
                             "from pathlib import PurePath\n"
                             "print(1)\n";
    const QString out = QString::fromStdString(core::hoistImports(code));

    QCOMPARE(countOf(out, QStringLiteral("import os")), 1);      // 三条重复语句只留一条
    QCOMPARE(countOf(out, QStringLiteral("import  os")), 0);     // 重复行整行删除（含空白差异）
    QCOMPARE(countOf(out, QStringLiteral("from pathlib import Path")), 1);
    QCOMPARE(countOf(out, QStringLiteral("PurePath")), 1);       // 不同语句不去重
    QVERIFY(out.endsWith(QStringLiteral("print(1)\n")));
}

void StudyTest::hoistKeepsIndentedAndMultilineImports()
{
    const std::string code = "def run_x():\n"
                             "    import os\n"
                             "    return os.getcwd()\n"
                             "\n"
                             "from pathlib import (\n"
                             "    Path,\n"
                             "    PurePath,\n"
                             ")\n"
                             "\n"
                             "import json\n";
    const QString out = QString::fromStdString(core::hoistImports(code));

    // 缩进的 import 属函数体：原样留在原位
    QVERIFY(out.contains(QStringLiteral("def run_x():\n    import os\n    return os.getcwd()\n")));
    // 跨行语句（括号未闭合）保持原位：不搬、不去重
    QVERIFY(out.contains(QStringLiteral("from pathlib import (\n    Path,\n    PurePath,\n)")));
    // 顶层单行 import 归位到文件头（前导无注释时就是第一行）
    QVERIFY(out.startsWith(QStringLiteral("import json\n")));

    // 反斜杠续行同样保持原位
    const std::string continued = "import os\n"
                                  "import a \\\n"
                                  "    as b\n"
                                  "x = 1\n";
    const QString continuedOut = QString::fromStdString(core::hoistImports(continued));
    QVERIFY(continuedOut.contains(QStringLiteral("import a \\\n    as b\n")));
    QVERIFY(continuedOut.startsWith(QStringLiteral("import os\n")));
}

void StudyTest::hoistIsNoOpAndIdempotent()
{
    // 无 import：原样返回（含空串）
    const std::string plain = "def run_x():\n    return 1\n";
    QCOMPARE(QString::fromStdString(core::hoistImports(plain)), QString::fromStdString(plain));
    QCOMPARE(core::hoistImports(std::string()), std::string());

    // 有 import：结果幂等（再跑一次不变）
    const std::string code = "# 头\n"
                             "\n"
                             "import os\n"
                             "import sys  # 说明\n"
                             "import os\n"
                             "x = 1";
    const std::string once = core::hoistImports(code);
    QCOMPARE(core::hoistImports(once), once);
    QCOMPARE(QString::fromStdString(once).count(QStringLiteral("import os")), 1);
    QVERIFY(QString::fromStdString(once).contains(QStringLiteral("import sys  # 说明")));
}

void StudyTest::blockBodyRejectsBadIdsAndMissingBlocks()
{
    auto work = makeWorkService();
    QVERIFY(!work->blocks().isEmpty());

    const QString id = QString::fromStdString(work->blocks().first().id);
    const QString body = work->blockBody(id);
    QVERIFY(!body.isEmpty());
    QVERIFY(body.contains(QStringLiteral("def run_")));

    QVERIFY(work->blockBody(QStringLiteral("../evil")).isEmpty());   // 非法 id：不拼路径
    QVERIFY(work->blockBody(QStringLiteral("no-such-block")).isEmpty());
}

void StudyTest::importBlocksSkipsUnknownAndDuplicates()
{
    auto work = makeWorkService();
    viewmodel::StudyService study;
    study.setWorkService(work.get());
    const QVector<core::PythonBlock> blocks = study.availableBlocks();
    QVERIFY(blocks.size() >= 2);

    QSignalSpy spy(&study, &viewmodel::StudyService::changed);
    const QString first = QString::fromStdString(blocks.at(0).id);
    const QString second = QString::fromStdString(blocks.at(1).id);
    QCOMPARE(study.importBlocks({ QStringLiteral("no-such-block"), first, second }), 2);
    QCOMPARE(study.items().size(), 2);
    QCOMPARE(spy.count(), 1); // 一次导入只发一次变化

    QCOMPARE(study.importBlocks({ first }), 0); // 重复导入：跳过
    QCOMPARE(study.items().size(), 2);
    QCOMPARE(spy.count(), 1);

    // 条目标题 / 类别 / 依赖实时取自内容层唯一索引
    QCOMPARE(study.items().at(0).title, QString::fromStdString(blocks.at(0).title));
    QCOMPARE(study.items().at(0).category, QString::fromStdString(blocks.at(0).category));
    QCOMPARE(study.items().at(0).requires, QString::fromStdString(blocks.at(0).requires));
    QVERIFY(study.contains(first));
    QVERIFY(!study.contains(QStringLiteral("no-such-block")));

    QCOMPARE(study.removeItems({ first }), 1);
    QCOMPARE(study.items().size(), 1);
    study.clear();
    QVERIFY(study.items().isEmpty());
}

void StudyTest::externalItemsAndJsonRoundTrip()
{
    auto work = makeWorkService();
    viewmodel::StudyService study;
    study.setWorkService(work.get());

    const QString externalId =
        study.addExternal(QStringLiteral("片段"), QStringLiteral("import os\nprint(1)\n"));
    QVERIFY(externalId.startsWith(QStringLiteral("external:")));
    QVERIFY(study.addExternal(QStringLiteral("空正文"), QStringLiteral("   \n")).isEmpty()); // 空正文拒绝
    QCOMPARE(study.importBlocks({ QStringLiteral("dir-tree") }), 1);
    QCOMPARE(study.items().size(), 2);

    // 读外部条目：正文原样（与手动导入的文本逐字一致）
    const QString body = study.itemBody(study.items().at(0));
    QCOMPARE(body, QStringLiteral("import os\nprint(1)\n"));
    QVERIFY(study.itemUsable(study.items().at(0)));
    QVERIFY(study.itemUsable(study.items().at(1))); // 功能块条目在索引里 → 可用

    // JSON 往返：只落 id / kind（功能块）与 title / body（外部）
    const QString json = study.toJson();
    QVERIFY(json.contains(QStringLiteral("dir-tree")));
    QVERIFY(json.contains(externalId));

    viewmodel::StudyService restored;
    restored.setWorkService(work.get());
    QVERIFY(restored.loadFromJson(json));
    QCOMPARE(restored.items().size(), 2);
    QCOMPARE(restored.items().at(0).id, externalId);
    QCOMPARE(restored.items().at(0).title, QStringLiteral("片段"));
    QCOMPARE(restored.itemBody(restored.items().at(0)), body);
    QCOMPARE(restored.itemBody(restored.items().at(1)),
             study.itemBody(study.items().at(1))); // 功能块正文实时读源
    QCOMPARE(restored.toJson(), json);
    QVERIFY(!restored.loadFromJson(json)); // 与当前一致：不再变化（无回声）

    // 非法文本：保持现状（不静默清空）；空文本 = 默认值（空清单）
    QVERIFY(!restored.loadFromJson(QStringLiteral("{not json")));
    QCOMPARE(restored.items().size(), 2);
    QVERIFY(restored.loadFromJson(QString()));
    QVERIFY(restored.items().isEmpty());
}

void StudyTest::assemblyFollowsImportOrderAndHoistsImports()
{
    auto work = makeWorkService();
    viewmodel::StudyService study;
    study.setWorkService(work.get());

    QVERIFY(study.assemblePreview().isEmpty()); // 空清单：不产出
    QVERIFY(study.addExternal(QStringLiteral("外部"), QStringLiteral("print(1)\n"))
            .startsWith(QStringLiteral("external:")));
    QVERIFY(study.assemblePreview().isEmpty()); // 只有外部条目：没有可拼接的功能块

    QCOMPARE(study.importBlocks({ QStringLiteral("http-status"), QStringLiteral("json-tidy") }), 2);
    QCOMPARE(study.usableBlockCount(), 2);
    const QString script = study.assemblePreview();
    QVERIFY(!script.isEmpty());

    // 组装口径不变：头注释 + 逐块正文 + 末尾分发器
    QVERIFY(script.startsWith(QStringLiteral("#!/usr/bin/env python3")));
    QVERIFY(script.contains(QStringLiteral("def run_http_status(")));
    QVERIFY(script.contains(QStringLiteral("def run_json_tidy(")));
    QVERIFY(script.contains(QStringLiteral("if __name__ == \"__main__\":")));
    QVERIFY(script.contains(QStringLiteral("未经运行验证")));

    // 拼接顺序 = 导入顺序
    QVERIFY(script.indexOf(QStringLiteral("def run_http_status("))
            < script.indexOf(QStringLiteral("def run_json_tidy(")));

    // import 归位去重：两块都 import argparse，只留一条，且在两块正文之前
    QCOMPARE(countOf(script, QStringLiteral("import argparse")), 1);
    QCOMPARE(countOf(script, QStringLiteral("import urllib.request")), 1);
    QCOMPARE(countOf(script, QStringLiteral("import json")), 1);
    const int argparseIndex = script.indexOf(QStringLiteral("import argparse"));
    QVERIFY(argparseIndex > 0);
    QVERIFY(argparseIndex < script.indexOf(QStringLiteral("def run_http_status(")));
    // 归位后的 import 仍在注释头之后（shebang 与生成说明留在最上方）
    QVERIFY(argparseIndex > script.indexOf(QStringLiteral("#!/usr/bin/env python3")));
}

void StudyTest::unusableSourcesAreListedButNotAssembled()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY(writeText(first.filePath(QStringLiteral("index.txt")),
                      QStringLiteral("only-block|唯一块|入门|说明|标准库\n")));
    QVERIFY(writeText(first.filePath(QStringLiteral("only-block.txt")),
                      QStringLiteral("import os\n\n\ndef run_only_block():\n    return os.name\n")));
    QVERIFY(writeText(second.filePath(QStringLiteral("index.txt")),
                      QStringLiteral("another|另一个|入门|说明|标准库\n")));

    auto work = std::make_unique<viewmodel::WorkService>();
    work->setBlocksRoot(first.path());
    viewmodel::StudyService study;
    study.setWorkService(work.get());
    QCOMPARE(study.importBlocks({ QStringLiteral("only-block") }), 1);
    QVERIFY(!study.assemblePreview().isEmpty());
    QVERIFY(study.itemUsable(study.items().at(0)));

    // 内容层换成不含该块的目录：条目仍在清单里（便于用户自行移出），但标记失效、不进拼接
    work->setBlocksRoot(second.path());
    QCOMPARE(study.items().size(), 1);
    QCOMPARE(study.items().at(0).category, QStringLiteral("已失效"));
    QVERIFY(!study.itemUsable(study.items().at(0)));
    QVERIFY(study.itemBody(study.items().at(0)).isEmpty());
    QVERIFY(study.assemblePreview().isEmpty());
    QCOMPARE(study.usableBlockCount(), 0);
}

void StudyTest::contributionIsAWorkbenchPage()
{
    QWidget hostWindow;
    ui::UiContributionHost uiHost(&hostWindow);
    auto work = makeWorkService();

    ui::StudyUiPlugin::Hooks hooks;
    hooks.workService = [workPtr = work.get()]() -> viewmodel::WorkService * { return workPtr; };

    PluginRegistry registry;
    ui::StudyUiPlugin *plugin = nullptr;
    {
        auto owned = std::make_unique<ui::StudyUiPlugin>(hooks);
        plugin = owned.get();
        QVERIFY(registry.add(std::move(owned)));
    }
    PluginContext ctx;
    QVERIFY(registry.startAll(ctx) >= 1);

    // 贡献点声明：一个 WorkbenchPage（标题「学习」，排在「干活」之后、绘画三页之前）
    const QList<PluginContribution> list = registry.collectContributions(uiHost);
    QCOMPARE(list.size(), 1);
    QCOMPARE(static_cast<int>(list.at(0).kind),
             static_cast<int>(ContributionKind::WorkbenchPage));
    QCOMPARE(list.at(0).label, QStringLiteral("学习"));
    QCOMPARE(list.at(0).id, QStringLiteral("ui.study.page"));
    QCOMPARE(list.at(0).pluginId, QStringLiteral("builtin.study"));
    QVERIFY(list.at(0).order > 0);
    QVERIFY(list.at(0).order < 10); // 小于绘画三页的 order（10 / 20 / 30）

    // 宿主侧通用分发：挂成工作台页签（宿主不认具体页面）
    PluginRegistry &registryRef = registry;
    ui::WorkBenchPanel panel(work.get(), [&uiHost, &registryRef](QTabWidget *tabs) {
        uiHost.appendToTabs(registryRef, tabs, ContributionKind::WorkbenchPage);
    });
    QCOMPARE(panel.tabs()->count(), 2);
    QCOMPARE(panel.tabs()->tabText(0), QStringLiteral("干活"));
    QCOMPARE(panel.tabs()->tabText(1), QStringLiteral("学习"));
    QVERIFY(plugin->page() != nullptr);
    QVERIFY(panel.tabs()->widget(1) == static_cast<QWidget *>(plugin->page()));

    // 懒建幂等：再次分发仍返回同一页面
    QWidget *again = list.at(0).createView(panel.tabs());
    QVERIFY(again == static_cast<QWidget *>(plugin->page()));
    QCOMPARE(panel.tabs()->count(), 2);
}

void StudyTest::panelImportsAndPreviews()
{
    auto work = makeWorkService();
    viewmodel::StudyService study;
    study.setWorkService(work.get());
    ui::StudyPanel panel(&study);

    QCOMPARE(panel.availableList()->count(), study.availableBlocks().size());
    QVERIFY(panel.availableList()->count() >= 2);

    // 手动导入：勾选前两条 →「导入选中」
    panel.availableList()->item(0)->setSelected(true);
    panel.availableList()->item(1)->setSelected(true);
    auto *importSelected = panel.findChild<QPushButton *>(QStringLiteral("StudyImportSelected"));
    QVERIFY(importSelected != nullptr);
    importSelected->click();
    QCOMPARE(study.items().size(), 2);
    QCOMPARE(panel.importedList()->count(), 2);
    // 导入后自动展示最近导入条目：条目信息 + 原样代码块正文
    const QString code = panel.codeView()->toPlainText();
    QVERIFY(code.contains(QStringLiteral("# 条目：")));
    QVERIFY(code.contains(QStringLiteral("def run_")));
    // 候选项标记「（已导入）」
    QVERIFY(panel.availableList()->item(0)->text().contains(QStringLiteral("（已导入）")));

    // 拼接整脚本（import 归位去重）
    auto *preview = panel.findChild<QPushButton *>(QStringLiteral("StudyPreviewScript"));
    QVERIFY(preview != nullptr);
    preview->click();
    const QString script = panel.codeView()->toPlainText();
    QVERIFY(script.contains(QStringLiteral("def run_")));
    QVERIFY(script.contains(QStringLiteral("if __name__ == \"__main__\":")));
    QCOMPARE(countOf(script, QStringLiteral("import argparse")), 1); // 两个块都 import argparse

    // 移出选中（先清掉「导入后自动选中最新一条」留下的选择，只留要移出的那条）
    panel.importedList()->clearSelection();
    panel.importedList()->item(0)->setSelected(true);
    auto *removeSelected = panel.findChild<QPushButton *>(QStringLiteral("StudyRemoveSelected"));
    QVERIFY(removeSelected != nullptr);
    removeSelected->click();
    QCOMPARE(study.items().size(), 1);
    QCOMPARE(panel.importedList()->count(), 1);
}

void StudyTest::hostJsonIsInjectedAndChangesAreWrittenBack()
{
    auto work = makeWorkService();
    QStringList written;

    ui::StudyUiPlugin::Hooks hooks;
    hooks.workService = [workPtr = work.get()]() -> viewmodel::WorkService * { return workPtr; };
    hooks.importedChanged = [&written](const QString &json) { written.push_back(json); };

    ui::StudyUiPlugin plugin(hooks);
    PluginContext ctx;
    QVERIFY(plugin.start(ctx));

    // 宿主注入持久化清单（模拟 applySettings）：只加载，不回写（抑制回声）
    plugin.setImportedJson(QStringLiteral("[{\"id\":\"hello-world\",\"kind\":\"block\"},"
                                          "{\"id\":\"external:3\",\"kind\":\"external\","
                                          "\"title\":\"片段\",\"body\":\"print(1)\\n\"}]"));
    QCOMPARE(plugin.service()->items().size(), 2);
    QVERIFY(written.isEmpty());
    QVERIFY(!plugin.service()->items().at(0).title.isEmpty());
    QCOMPARE(plugin.service()->items().at(1).id, QStringLiteral("external:3"));
    QCOMPARE(plugin.service()->itemBody(plugin.service()->items().at(1)),
             QStringLiteral("print(1)\n"));

    // 页内导入 → 回写一次
    QCOMPARE(plugin.service()->importBlocks({ QStringLiteral("dir-tree") }), 1);
    QCOMPARE(written.size(), 1);
    QVERIFY(written.last().contains(QStringLiteral("dir-tree")));

    // 注入同一份文本：无变化，不再回写
    plugin.setImportedJson(plugin.importedJson());
    QCOMPARE(written.size(), 1);
    QCOMPARE(plugin.service()->items().size(), 3);

    // 建页（贡献点懒建；页的所有权在页签容器，这里直接用宿主窗口当父）
    QWidget hostWindow;
    ui::UiContributionHost uiHost(&hostWindow);
    const QList<PluginContribution> contributions = plugin.contributions(uiHost);
    QCOMPARE(contributions.size(), 1);
    QVERIFY(contributions.at(0).createView(&hostWindow) != nullptr);

    // 经 ui::StudyPanel 的「导入全部」也走同一条回写链路
    ui::StudyPanel *page = plugin.page();
    QVERIFY(page != nullptr);
    auto *importAll = page->findChild<QPushButton *>(QStringLiteral("StudyImportAll"));
    QVERIFY(importAll != nullptr);
    importAll->click();
    QCOMPARE(written.size(), 2);
    // 全部功能块 + 先前注入的 1 条外部代码条目
    QCOMPARE(plugin.service()->items().size(),
             static_cast<int>(plugin.service()->availableBlocks().size()) + 1);
}

// 与其余控件测试一致：无显示环境缺省 offscreen
int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    StudyTest testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "test_study.moc"
