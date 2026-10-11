// P9-A 宿主服务注册化（docs/ROADMAP-P9-Fin.md §P9-A、docs/ARCHITECTURE.md §A.6）：
//   * 6 个宿主服务以 IPlugin 形式注册进能力总线（builtin 层）——P9-A 的 5 个
//     + EX6 的随机闪现（FlickerService）；
//   * 每个服务暴露 1 个只读状态能力（service.*），id 与 builtinServiceCapabilityIds() 一致；
//   * 服务未启动时能力返回 kRpcErrorCapabilityUnavailable（不伪造数据）；
//   * startAll 后服务句柄被回填、能力可读；缺 controller 时对话插件优雅降级。

#include "model/Database.h"
#include "plugin/Capability.h"
#include "plugin/PluginRegistry.h"
#include "viewmodel/FlickerService.h"
#include "viewmodel/builtin/BuiltinServicePlugins.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QtTest>

#include <algorithm>

using namespace whalepet;
using namespace whalepet::plugin;

namespace {

QStringList sortedCopy(QStringList list)
{
    std::sort(list.begin(), list.end());
    return list;
}

} // namespace

class TestServicePlugins : public QObject {
    Q_OBJECT

private slots:
    void capabilityIdsAreStableAndUnique();
    void registersAllBuiltinServicePlugins();
    void capabilitiesMatchDeclaredIds();
    void unavailableBeforeStart();
    void startAllFillsHandlesAndServesStatus();
};

void TestServicePlugins::capabilityIdsAreStableAndUnique()
{
    const QStringList ids = builtinServiceCapabilityIds();
    QCOMPARE(ids.size(), 6);

    QStringList unique = ids;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    QCOMPARE(unique.size(), 6); // 无重复

    for (const QString &expected : { QStringLiteral("service.growth"),
                                     QStringLiteral("service.stomach"),
                                     QStringLiteral("service.dialogue"),
                                     QStringLiteral("service.work"),
                                     QStringLiteral("service.recycleBin"),
                                     QStringLiteral("service.flicker") }) {
        QVERIFY2(ids.contains(expected), qPrintable(expected));
    }
}

void TestServicePlugins::registersAllBuiltinServicePlugins()
{
    QObject host;
    PluginRegistry registry;
    BuiltinServiceHooks hooks;
    BuiltinServiceHandles handles;

    QCOMPARE(registerBuiltinServicePlugins(registry, &host, hooks, &handles), 6);
    QCOMPARE(registry.count(), 6);

    for (const QString &pluginId : { QStringLiteral("builtin.growth"),
                                     QStringLiteral("builtin.stomach"),
                                     QStringLiteral("builtin.dialogue"),
                                     QStringLiteral("builtin.work"),
                                     QStringLiteral("builtin.recycleBin"),
                                     QStringLiteral("builtin.flicker") }) {
        QVERIFY2(registry.find(pluginId) != nullptr, qPrintable(pluginId));
    }

    // 参数非法：拒绝注册（返回 -1）且不产生插件
    PluginRegistry other;
    QCOMPARE(registerBuiltinServicePlugins(other, nullptr, hooks, &handles), -1);
    QCOMPARE(other.count(), 0);
}

void TestServicePlugins::capabilitiesMatchDeclaredIds()
{
    QObject host;
    PluginRegistry registry;
    BuiltinServiceHooks hooks;
    BuiltinServiceHandles handles;
    QCOMPARE(registerBuiltinServicePlugins(registry, &host, hooks, &handles), 6);

    QStringList actual;
    const QList<CapabilityDescriptor> descriptors = registry.capabilities().descriptors();
    QCOMPARE(descriptors.size(), 6);
    for (const CapabilityDescriptor &descriptor : descriptors) {
        actual << descriptor.id;
        // 服务状态能力固定：Builtin 来源 + 只读
        QVERIFY(descriptor.origin == PluginOrigin::Builtin);
        QVERIFY(descriptor.readOnly);
        QVERIFY(!descriptor.displayName.isEmpty());
    }
    QCOMPARE(sortedCopy(actual), sortedCopy(builtinServiceCapabilityIds()));
}

void TestServicePlugins::unavailableBeforeStart()
{
    QObject host;
    PluginRegistry registry;
    BuiltinServiceHooks hooks;
    BuiltinServiceHandles handles;
    QCOMPARE(registerBuiltinServicePlugins(registry, &host, hooks, &handles), 6);

    // 未 start：服务尚未创建 → 能力报「不可用」，不伪造数据
    InvokeContext ctx;
    QJsonObject out;
    QJsonObject error;
    const bool handled = registry.capabilities().invoke(QStringLiteral("service.growth"),
                                                        QJsonObject(), ctx, out, error);
    QVERIFY(handled);            // 同步完成（失败也在 error 中）
    QVERIFY(!error.isEmpty());   // 失败必须填 error
    QCOMPARE(rpcErrorCode(error), kRpcErrorCapabilityUnavailable);
    QVERIFY(out.isEmpty());

    QVERIFY(handles.growth == nullptr);
    QVERIFY(handles.stomach == nullptr);
    QVERIFY(handles.dialogue == nullptr);
    QVERIFY(handles.work == nullptr);
    QVERIFY(handles.recycleBin == nullptr);
    QVERIFY(handles.flicker == nullptr);
}

void TestServicePlugins::startAllFillsHandlesAndServesStatus()
{
    // 声明顺序即析构逆序：db 最先声明 → 最后析构，确保服务析构时数据库仍存活。
    model::Database db;
    QVERIFY(db.openMemory());

    QObject host;
    PluginRegistry registry;
    BuiltinServiceHooks hooks;
    BuiltinServiceHandles handles;
    QCOMPARE(registerBuiltinServicePlugins(registry, &host, hooks, &handles), 6);

    PluginContext pluginCtx;
    pluginCtx.db = &db;
    // controller 为空：对话插件应优雅降级（计入未启动），其余 5 个服务正常创建。
    QCOMPARE(registry.startAll(pluginCtx), 5);

    QVERIFY(handles.growth != nullptr);
    QVERIFY(handles.stomach != nullptr);
    QVERIFY(handles.recycleBin != nullptr);
    QVERIFY(handles.work != nullptr);     // P10：工作台（会话由宿主设置注入，服务本身不依赖 controller）
    QVERIFY(handles.flicker != nullptr);  // EX6：随机闪现（不依赖 controller）
    QVERIFY(handles.dialogue == nullptr); // 无 PetController → 未认领

    // 启动后：能力可读，字段来自真实服务状态
    InvokeContext ctx;
    QJsonObject out;
    QJsonObject error;
    QVERIFY(registry.capabilities().invoke(QStringLiteral("service.growth"), QJsonObject(), ctx, out,
                                           error));
    QVERIFY(error.isEmpty());
    QVERIFY(out.contains(QStringLiteral("level")));
    QCOMPARE(out.value(QStringLiteral("level")).toInt(), 1);

    // EX6：闪现服务状态能力可读，字段来自真实服务（未启动调度 → running=false）
    QVERIFY(registry.capabilities().invoke(QStringLiteral("service.flicker"), QJsonObject(), ctx, out,
                                           error));
    QVERIFY(error.isEmpty());
    QCOMPARE(out.value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(out.value(QStringLiteral("minIntervalMs")).toInt(),
             viewmodel::FlickerService::kMinIntervalMs);
    QCOMPARE(out.value(QStringLiteral("lastDelayMs")).toInt(), 0);

    // 幂等：再次 startAll 不重复创建服务（句柄不变）
    viewmodel::GrowthService *growth = handles.growth;
    QCOMPARE(registry.startAll(pluginCtx), 5);
    QVERIFY(handles.growth == growth);

    registry.stopAll();
}

// 只用 QObject / Database，不创建任何 Widget → 用 GUILESS 入口，
// 与 test_growth / test_database 一致（无显示环境亦可跑）。
QTEST_GUILESS_MAIN(TestServicePlugins)
#include "test_service_plugins.moc"
