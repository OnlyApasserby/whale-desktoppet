// P9-C UI 宿主契约与贡献点协议（docs/ROADMAP-P9-Fin.md §P9-C）：
//   * PluginRegistry::collectContributions 的排序（order 升序、同序保持注册顺序）/ 去重 / 空 id；
//   * UiContributionHost 实现 IPluginUiHost（父窗口 / 生命周期回调 / 面板展示）；
//   * 贡献点按 kind 分发到右键菜单 / 托盘 / 设置页（createView 延迟创建、checkable 写回）；
//   * 试点 StatusPanelUiPlugin：右键 + 托盘两项贡献，展示前刷新面板，签到回传宿主。

#include "plugin/PluginRegistry.h"
#include "view/StatusPanel.h"
#include "view/ui/StatusPanelUiPlugin.h"
#include "view/ui/UiContributionHost.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QObject>
#include <QTabWidget>
#include <QtTest>
#include <QWidget>

#include <memory>
#include <utility>

using namespace whalepet;
using namespace whalepet::plugin;

namespace {

// 测试替身：声明固定贡献点的插件
class FakeUiPlugin : public IPlugin {
public:
    FakeUiPlugin(QString id, QList<PluginContribution> contributions)
        : m_id(std::move(id))
        , m_contributions(std::move(contributions))
    {
    }

    PluginInfo info() const override
    {
        PluginInfo info;
        info.id = m_id;
        return info;
    }

    void registerCapabilities(CapabilityRegistry &) override {}

    QList<PluginContribution> contributions(const IPluginUiHost &) const override
    {
        return m_contributions;
    }

private:
    QString m_id;
    QList<PluginContribution> m_contributions;
};

PluginContribution makeContribution(const QString &id, ContributionKind kind, int order,
                                    const QString &label = QStringLiteral("项"))
{
    PluginContribution contribution;
    contribution.pluginId = QStringLiteral("test.fake");
    contribution.id = id;
    contribution.kind = kind;
    contribution.label = label;
    contribution.order = order;
    return contribution;
}

} // namespace

class TestUiPluginHost : public QObject {
    Q_OBJECT

private slots:
    void collectContributionsSortsByOrder();
    void collectContributionsSkipsEmptyAndDuplicateIds();
    void hostProvidesUiContext();
    void contextMenuContributionCreatesViewOnTrigger();
    void menuKindIsIsolated();
    void settingsTabContributionAddsTab();
    void checkableContributionWritesBack();
    void statusPanelPluginExposesTwoContributions();
};

void TestUiPluginHost::collectContributionsSortsByOrder()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;

    registry.add(std::make_unique<FakeUiPlugin>(
        QStringLiteral("p.a"), QList<PluginContribution>{ makeContribution(QStringLiteral("b"),
                                                                          ContributionKind::ContextMenu, 50) }));
    registry.add(std::make_unique<FakeUiPlugin>(
        QStringLiteral("p.b"), QList<PluginContribution>{ makeContribution(QStringLiteral("a"),
                                                                          ContributionKind::ContextMenu, 10) }));
    registry.add(std::make_unique<FakeUiPlugin>(
        QStringLiteral("p.c"), QList<PluginContribution>{ makeContribution(QStringLiteral("c"),
                                                                          ContributionKind::ContextMenu, 10) }));

    const QList<PluginContribution> list = registry.collectContributions(uiHost);
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0).id, QStringLiteral("a"));
    QCOMPARE(list.at(1).id, QStringLiteral("c")); // 同 order 10：保持注册顺序（b 先于 c）
    QCOMPARE(list.at(2).id, QStringLiteral("b"));
}

void TestUiPluginHost::collectContributionsSkipsEmptyAndDuplicateIds()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;

    registry.add(std::make_unique<FakeUiPlugin>(
        QStringLiteral("p.a"),
        QList<PluginContribution>{ makeContribution(QString(), ContributionKind::ContextMenu, 1),
                                   makeContribution(QStringLiteral("dup"), ContributionKind::ContextMenu, 2) }));
    registry.add(std::make_unique<FakeUiPlugin>(
        QStringLiteral("p.b"),
        QList<PluginContribution>{ makeContribution(QStringLiteral("dup"), ContributionKind::ContextMenu, 3),
                                   makeContribution(QStringLiteral("keep"), ContributionKind::ContextMenu, 4) }));

    const QList<PluginContribution> list = registry.collectContributions(uiHost);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).id, QStringLiteral("dup"));
    QCOMPARE(list.at(0).order, 2); // 重复 id 保留先注册者（order=3 的后注册者被丢弃）
    QCOMPARE(list.at(1).id, QStringLiteral("keep"));
}

void TestUiPluginHost::hostProvidesUiContext()
{
    QWidget host;
    bool shutdownCalled = false;
    {
        ui::UiContributionHost uiHost(&host);
        QVERIFY(uiHost.hostObject() == static_cast<QObject *>(&host));
        QVERIFY(uiHost.hostWidget() == &host);
        QVERIFY(uiHost.initialLayout());
        uiHost.addShutdownCallback([&shutdownCalled] { shutdownCalled = true; });
    }
    QVERIFY(shutdownCalled); // 宿主关闭 → 回调被执行
}

void TestUiPluginHost::contextMenuContributionCreatesViewOnTrigger()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;

    QWidget *panel = nullptr;
    auto contribution = makeContribution(QStringLiteral("ui.panel"), ContributionKind::ContextMenu, 1,
                                         QStringLiteral("面板"));
    contribution.createView = [&panel](QWidget *) -> QWidget * {
        panel = new QWidget;
        return panel;
    };
    registry.add(std::make_unique<FakeUiPlugin>(QStringLiteral("p.a"),
                                                QList<PluginContribution>{ contribution }));

    QMenu menu;
    QCOMPARE(uiHost.appendToMenu(registry, ContributionKind::ContextMenu, &menu), 1);
    QCOMPARE(menu.actions().size(), 1);
    QCOMPARE(menu.actions().first()->text(), QStringLiteral("面板"));
    QVERIFY(!uiHost.initialLayout()); // 已布局过

    menu.actions().first()->trigger();
    QVERIFY(panel != nullptr);   // createView 被调用（延迟创建）
    QVERIFY(!panel->isHidden()); // presentPanel → show
    delete panel;                // 测试自管生命周期（无 QObject 父）
}

void TestUiPluginHost::menuKindIsIsolated()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;
    registry.add(std::make_unique<FakeUiPlugin>(
        QStringLiteral("p.a"),
        QList<PluginContribution>{ makeContribution(QStringLiteral("ui.ctx"), ContributionKind::ContextMenu, 1),
                                   makeContribution(QStringLiteral("ui.tray"), ContributionKind::TrayMenu, 1) }));

    QMenu contextMenu;
    QMenu trayMenu;
    QCOMPARE(uiHost.appendToMenu(registry, ContributionKind::ContextMenu, &contextMenu), 1);
    QCOMPARE(uiHost.appendToMenu(registry, ContributionKind::TrayMenu, &trayMenu), 1);
    QCOMPARE(contextMenu.actions().size(), 1);
    QCOMPARE(trayMenu.actions().size(), 1);
    QCOMPARE(contextMenu.actions().first()->text(), QStringLiteral("项"));
}

void TestUiPluginHost::settingsTabContributionAddsTab()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;

    auto contribution = makeContribution(QStringLiteral("ui.tab"), ContributionKind::SettingsTab, 1,
                                         QStringLiteral("插件面板"));
    contribution.createView = [](QWidget *parent) -> QWidget * { return new QWidget(parent); };
    registry.add(std::make_unique<FakeUiPlugin>(QStringLiteral("p.a"),
                                                QList<PluginContribution>{ contribution }));

    QTabWidget tabs;
    QCOMPARE(uiHost.appendToTabs(registry, &tabs), 1);
    QCOMPARE(tabs.count(), 1);
    QCOMPARE(tabs.tabText(0), QStringLiteral("插件面板"));
}

void TestUiPluginHost::checkableContributionWritesBack()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;

    bool checked = false;
    auto contribution = makeContribution(QStringLiteral("ui.toggle"), ContributionKind::ContextMenu, 1,
                                         QStringLiteral("开关"));
    contribution.checkable = true;
    contribution.isChecked = [&checked] { return checked; };
    contribution.setChecked = [&checked](bool on) { checked = on; };
    registry.add(std::make_unique<FakeUiPlugin>(QStringLiteral("p.a"),
                                                QList<PluginContribution>{ contribution }));

    QMenu menu;
    QCOMPARE(uiHost.appendToMenu(registry, ContributionKind::ContextMenu, &menu), 1);
    QAction *action = menu.actions().first();
    QVERIFY(action->isCheckable());
    QVERIFY(!action->isChecked());
    action->setChecked(true);
    QVERIFY(checked); // 勾选态写回插件
}

void TestUiPluginHost::statusPanelPluginExposesTwoContributions()
{
    QWidget host;
    ui::UiContributionHost uiHost(&host);
    PluginRegistry registry;

    int refreshCount = 0;
    int signInCount = 0;
    ui::StatusPanelUiPlugin::Hooks hooks;
    hooks.refresh = [&refreshCount](StatusPanel *) { ++refreshCount; };
    hooks.signIn = [&signInCount] { ++signInCount; };

    auto plugin = std::make_unique<ui::StatusPanelUiPlugin>(std::move(hooks));
    ui::StatusPanelUiPlugin *raw = plugin.get();
    QVERIFY(registry.add(std::move(plugin)));

    const QList<PluginContribution> list = registry.collectContributions(uiHost);
    QCOMPARE(list.size(), 2);
    QVERIFY(list.at(0).kind == ContributionKind::ContextMenu);
    QVERIFY(list.at(1).kind == ContributionKind::TrayMenu);
    QCOMPARE(list.at(0).label, QStringLiteral("状态"));

    QVERIFY(raw->panel() == nullptr); // 视图延迟创建

    QWidget *panel = list.at(0).createView(&host);
    QVERIFY(panel != nullptr);
    QVERIFY(raw->panel() == panel);
    QCOMPARE(refreshCount, 1); // 展示前刷新一次

    auto *statusPanel = qobject_cast<StatusPanel *>(panel);
    QVERIFY(statusPanel != nullptr);
    emit statusPanel->signInRequested();
    QCOMPARE(signInCount, 1); // 签到回传宿主
}

// 与其余 UI 测试一致：无显示环境时缺省 offscreen（构造 QMenu / QWidget / 面板需要平台插件）
int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    TestUiPluginHost testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "test_ui_plugin_host.moc"
