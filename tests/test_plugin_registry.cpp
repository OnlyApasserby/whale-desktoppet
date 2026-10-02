#include <QtTest>

#include <QCoreApplication>
#include <QJsonObject>

#include "minigame/MiniGameCompatAdapter.h"
#include "plugin/Capability.h"
#include "plugin/PluginInterface.h"
#include "plugin/PluginRegistry.h"
#include "plugin/builtin/BuiltinPluginLoader.h"

#include <memory>
#include <utility>
#include <vector>

// P7 通用能力总线（docs/PLUGIN-ARCHITECTURE.md §4–§5、docs/ROADMAP-P7.md P7.0）。
//
// 覆盖点：
//   * 插件注册（空 id / 重复 id 的处理）与能力收集；
//   * 能力冲突仲裁：Builtin > Dll > Process，且冲突不静默；
//   * 能力调用与错误码（不存在 / 不可用 / 能力内部失败）；
//   * 异步能力契约（takeResponder + 回投），含「既不答复也不取走回调」的契约违反；
//   * 内置层装载器 BuiltinPluginLoader 的聚合语义与容错；
//   * 小游戏兼容适配：MiniGameRegistry 的既有插件以 minigame.* 能力出现在总线上。
//
// 注意：断言宏（QVERIFY/QCOMPARE）内不放花括号初始化列表或多层模板——moc 的预解析
// 会在这种写法上报 "missing ')' in macro usage"，故复杂表达式一律先落到局部变量。

namespace {

using whalepet::plugin::CapabilityDescriptor;
using whalepet::plugin::CapabilityRegistry;
using whalepet::plugin::InvokeContext;
using whalepet::plugin::PluginOrigin;

using CapList = std::vector<std::pair<QString, PluginOrigin>>;

CapList caps(std::initializer_list<std::pair<QString, PluginOrigin>> items)
{
    return CapList(items);
}

CapabilityDescriptor makeDescriptor(const QString &id, PluginOrigin origin)
{
    CapabilityDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = id;
    descriptor.origin = origin;
    descriptor.readOnly = true;
    descriptor.paramsSchema = QStringLiteral("{\"type\":\"object\",\"properties\":{}}");
    return descriptor;
}

class EchoCapability : public whalepet::plugin::SimpleCapability {
public:
    explicit EchoCapability(const QString &id, PluginOrigin origin = PluginOrigin::Builtin)
        : whalepet::plugin::SimpleCapability(makeDescriptor(id, origin))
    {
    }

protected:
    bool call(const QJsonObject &in, QJsonObject &out, QJsonObject &error) override
    {
        if (in.contains(QStringLiteral("fail"))) {
            error = whalepet::plugin::makeRpcError(whalepet::plugin::kRpcErrorCapabilityFailed,
                                                  QStringLiteral("故意失败"));
            return false;
        }
        out.insert(QStringLiteral("echo"), in.value(QStringLiteral("value")));
        return true;
    }
};

// 异步能力：返回 false 并取走回调（外部进程插件将来的形态）
class AsyncCapability : public whalepet::plugin::ICapability {
public:
    CapabilityDescriptor descriptor() const override
    {
        return makeDescriptor(QStringLiteral("ext.async"), PluginOrigin::Process);
    }

    bool invoke(const QJsonObject &in, InvokeContext &ctx, QJsonObject &out,
                QJsonObject &error) override
    {
        Q_UNUSED(in);
        Q_UNUSED(out);
        Q_UNUSED(error);
        m_responder = ctx.takeResponder();
        return false;
    }

    bool hasResponder() const { return static_cast<bool>(m_responder); }

    void complete()
    {
        if (!m_responder) {
            return;
        }
        InvokeContext::Responder responder = m_responder;
        m_responder = InvokeContext::Responder();
        InvokeContext ctx(responder);
        QJsonObject result;
        result.insert(QStringLiteral("done"), true);
        ctx.respond(result);
    }

private:
    InvokeContext::Responder m_responder;
};

class FakePlugin : public whalepet::plugin::SimplePlugin {
public:
    FakePlugin(const QString &id, const CapList &items)
        : whalepet::plugin::SimplePlugin(whalepet::plugin::PluginInfo{
              id, id, QStringLiteral("测试插件"), QStringLiteral("1.0"), QString() })
        , m_caps(items)
    {
    }

    void registerCapabilities(CapabilityRegistry &registry) override
    {
        for (const std::pair<QString, PluginOrigin> &item : m_caps) {
            registry.add(std::make_unique<EchoCapability>(item.first, item.second));
        }
    }

private:
    CapList m_caps;
};

// 生命周期钩子记录（验证 startAll / stopAll 的顺序与容错）
class LifecyclePlugin : public FakePlugin {
public:
    LifecyclePlugin(const QString &id, std::vector<QString> *log, bool failStart = false)
        : FakePlugin(id, CapList())
        , m_log(log)
        , m_failStart(failStart)
    {
    }

    bool start(whalepet::plugin::PluginContext &ctx) override
    {
        Q_UNUSED(ctx);
        m_log->push_back(QStringLiteral("start:") + info().id);
        return !m_failStart;
    }

    void stop() override { m_log->push_back(QStringLiteral("stop:") + info().id); }

private:
    std::vector<QString> *m_log = nullptr;
    bool m_failStart = false;
};

// 假小游戏插件（不创建任何界面，故可在 headless 下安全构造）
class FakeMiniGame : public whalepet::IMiniGamePlugin {
public:
    whalepet::MiniGameInfo info() const override
    {
        whalepet::MiniGameInfo info;
        info.id = QStringLiteral("fake");
        info.displayName = QStringLiteral("假游戏");
        info.menuLabel = QStringLiteral("假游戏");
        info.description = QStringLiteral("测试用");
        return info;
    }

    whalepet::MiniGameView *createView(const whalepet::MiniGameContext &, QWidget *) override
    {
        return nullptr;
    }
};

} // namespace

class PluginRegistryTest : public QObject {
    Q_OBJECT
private slots:
    void registryCollectsCapabilities();
    void rejectsEmptyIdAndDuplicatePlugin();
    void rejectsInvalidCapabilityId();
    void capabilityConflictPrefersBuiltin();
    void invokeRoutesAndReportsErrors();
    void asyncCapabilityKeepsResponder();
    void builtinLoaderAggregatesRegisterFns();
    void lifecycleStartStopIsFaultTolerant();
    void miniGamePluginsAppearOnBus();
};

void PluginRegistryTest::registryCollectsCapabilities()
{
    whalepet::plugin::PluginRegistry registry;
    CapList items = caps({ { QStringLiteral("t.echo"), PluginOrigin::Builtin },
                           { QStringLiteral("t.other"), PluginOrigin::Builtin } });
    std::unique_ptr<whalepet::plugin::IPlugin> plugin =
        std::make_unique<FakePlugin>(QStringLiteral("p1"), items);

    const bool added = registry.add(std::move(plugin));
    QVERIFY(added);
    QCOMPARE(registry.count(), 1);
    QCOMPARE(registry.capabilities().count(), 2);
    QVERIFY(registry.find(QStringLiteral("p1")) != nullptr);
    QVERIFY(registry.capabilities().contains(QStringLiteral("t.echo")));

    // 清单按 id 升序且稳定
    const QList<CapabilityDescriptor> list = registry.capabilities().descriptors();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.first().id, QStringLiteral("t.echo"));
    QCOMPARE(list.last().id, QStringLiteral("t.other"));
}

void PluginRegistryTest::rejectsEmptyIdAndDuplicatePlugin()
{
    whalepet::plugin::PluginRegistry registry;
    std::unique_ptr<whalepet::plugin::IPlugin> nullPlugin;
    QVERIFY(!registry.add(std::move(nullPlugin)));

    CapList one = caps({ { QStringLiteral("t.a"), PluginOrigin::Builtin } });
    std::unique_ptr<whalepet::plugin::IPlugin> emptyId =
        std::make_unique<FakePlugin>(QString(), one);
    QVERIFY(!registry.add(std::move(emptyId)));
    QCOMPARE(registry.count(), 0);

    CapList first = caps({ { QStringLiteral("t.b"), PluginOrigin::Builtin } });
    std::unique_ptr<whalepet::plugin::IPlugin> dup1 =
        std::make_unique<FakePlugin>(QStringLiteral("dup"), first);
    QVERIFY(registry.add(std::move(dup1)));

    // 重复 id 被丢弃（不静默：有告警日志），且其能力不得被并入
    CapList second = caps({ { QStringLiteral("t.c"), PluginOrigin::Builtin } });
    std::unique_ptr<whalepet::plugin::IPlugin> dup2 =
        std::make_unique<FakePlugin>(QStringLiteral("dup"), second);
    QVERIFY(!registry.add(std::move(dup2)));
    QCOMPARE(registry.count(), 1);
    QVERIFY(!registry.capabilities().contains(QStringLiteral("t.c")));
}

void PluginRegistryTest::rejectsInvalidCapabilityId()
{
    CapabilityRegistry registry;
    std::unique_ptr<whalepet::plugin::ICapability> nullCapability;
    QVERIFY(!registry.add(std::move(nullCapability)));

    std::unique_ptr<whalepet::plugin::ICapability> emptyId =
        std::make_unique<EchoCapability>(QString());
    QVERIFY(!registry.add(std::move(emptyId)));
    QCOMPARE(registry.count(), 0);
}

void PluginRegistryTest::capabilityConflictPrefersBuiltin()
{
    CapabilityRegistry registry;

    // 先注册进程层：应被更高优先级者替换
    std::unique_ptr<whalepet::plugin::ICapability> process =
        std::make_unique<EchoCapability>(QStringLiteral("x.y"), PluginOrigin::Process);
    QVERIFY(registry.add(std::move(process)));
    std::unique_ptr<whalepet::plugin::ICapability> dll =
        std::make_unique<EchoCapability>(QStringLiteral("x.y"), PluginOrigin::Dll);
    QVERIFY(registry.add(std::move(dll)));
    QCOMPARE(registry.find(QStringLiteral("x.y"))->descriptor().origin, PluginOrigin::Dll);

    std::unique_ptr<whalepet::plugin::ICapability> builtin =
        std::make_unique<EchoCapability>(QStringLiteral("x.y"), PluginOrigin::Builtin);
    QVERIFY(registry.add(std::move(builtin)));
    QCOMPARE(registry.find(QStringLiteral("x.y"))->descriptor().origin, PluginOrigin::Builtin);

    // 反过来：低优先级不得覆盖高优先级
    std::unique_ptr<whalepet::plugin::ICapability> lower =
        std::make_unique<EchoCapability>(QStringLiteral("x.y"), PluginOrigin::Process);
    QVERIFY(!registry.add(std::move(lower)));
    QCOMPARE(registry.find(QStringLiteral("x.y"))->descriptor().origin, PluginOrigin::Builtin);
    QCOMPARE(registry.count(), 1);
}

void PluginRegistryTest::invokeRoutesAndReportsErrors()
{
    CapabilityRegistry registry;
    registry.add(std::make_unique<EchoCapability>(QStringLiteral("t.echo")));

    // 同步成功
    InvokeContext ctx;
    QJsonObject out;
    QJsonObject error;
    QJsonObject params;
    params.insert(QStringLiteral("value"), 42);
    const bool done = registry.invoke(QStringLiteral("t.echo"), params, ctx, out, error);
    QVERIFY(done);
    QVERIFY(error.isEmpty());
    QCOMPARE(out.value(QStringLiteral("echo")).toInt(), 42);

    // 能力内部失败 → 错误对象带能力自报的 code
    QJsonObject failParams;
    failParams.insert(QStringLiteral("fail"), true);
    QJsonObject failOut;
    QJsonObject failError;
    InvokeContext failCtx;
    QVERIFY(registry.invoke(QStringLiteral("t.echo"), failParams, failCtx, failOut, failError));
    QCOMPARE(whalepet::plugin::rpcErrorCode(failError),
             whalepet::plugin::kRpcErrorCapabilityFailed);

    // 能力不存在
    QJsonObject missingError;
    InvokeContext missingCtx;
    QVERIFY(registry.invoke(QStringLiteral("nope"), QJsonObject(), missingCtx, out, missingError));
    QCOMPARE(whalepet::plugin::rpcErrorCode(missingError),
             whalepet::plugin::kRpcErrorMethodNotFound);

    // 能力被标记不可用（外部进程插件退出时的形态）
    registry.setAvailable(QStringLiteral("t.echo"), false);
    QVERIFY(!registry.isAvailable(QStringLiteral("t.echo")));
    QJsonObject unavailableError;
    InvokeContext unavailableCtx;
    QVERIFY(registry.invoke(QStringLiteral("t.echo"), QJsonObject(), unavailableCtx, out,
                            unavailableError));
    QCOMPARE(whalepet::plugin::rpcErrorCode(unavailableError),
             whalepet::plugin::kRpcErrorCapabilityUnavailable);
}

void PluginRegistryTest::asyncCapabilityKeepsResponder()
{
    CapabilityRegistry registry;
    std::unique_ptr<AsyncCapability> owned = std::make_unique<AsyncCapability>();
    AsyncCapability *raw = owned.get();
    std::unique_ptr<whalepet::plugin::ICapability> capability = std::move(owned);
    registry.add(std::move(capability));

    QVector<QJsonObject> responses;
    InvokeContext ctx([&responses](const QJsonObject &response) { responses.append(response); });

    QJsonObject out;
    QJsonObject error;
    const bool done = registry.invoke(QStringLiteral("ext.async"), QJsonObject(), ctx, out, error);
    QVERIFY2(!done, "异步能力应返回 false（已受理）");
    QVERIFY(!ctx.answered());
    QVERIFY2(!ctx.hasResponder(), "异步能力必须取走回调，否则分发方会判为违反契约");
    QVERIFY(raw->hasResponder());
    QCOMPARE(responses.size(), 0);

    // 能力完成后回投 → 分发侧收到 ok/result
    raw->complete();
    QCOMPARE(responses.size(), 1);
    QVERIFY(responses.first().value(QStringLiteral("ok")).toBool());
    QVERIFY(responses.first().value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("done"))
                .toBool());
}

void PluginRegistryTest::builtinLoaderAggregatesRegisterFns()
{
    whalepet::plugin::PluginRegistry registry;
    whalepet::plugin::BuiltinPluginLoader loader;

    loader.addRegisterFn([](whalepet::plugin::PluginRegistry &target) {
        CapList items = caps({ { QStringLiteral("a.one"), PluginOrigin::Builtin } });
        std::unique_ptr<whalepet::plugin::IPlugin> plugin =
            std::make_unique<FakePlugin>(QStringLiteral("builtin.a"), items);
        return target.add(std::move(plugin)) ? 1 : 0;
    });
    // 返回负值表示该注册函数失败：只记录并继续
    loader.addRegisterFn([](whalepet::plugin::PluginRegistry &) { return -1; });
    loader.addRegisterFn([](whalepet::plugin::PluginRegistry &target) {
        CapList items = caps({ { QStringLiteral("b.one"), PluginOrigin::Builtin } });
        std::unique_ptr<whalepet::plugin::IPlugin> plugin =
            std::make_unique<FakePlugin>(QStringLiteral("builtin.b"), items);
        return target.add(std::move(plugin)) ? 1 : 0;
    });

    QCOMPARE(loader.registerFnCount(), 3);
    QCOMPARE(loader.load(registry), 2);
    QCOMPARE(registry.count(), 2);
    QCOMPARE(registry.capabilities().count(), 2);
}

void PluginRegistryTest::lifecycleStartStopIsFaultTolerant()
{
    std::vector<QString> log;
    whalepet::plugin::PluginRegistry registry;
    registry.add(std::make_unique<LifecyclePlugin>(QStringLiteral("p1"), &log));
    registry.add(std::make_unique<LifecyclePlugin>(QStringLiteral("p2"), &log, true)); // 启动失败
    registry.add(std::make_unique<LifecyclePlugin>(QStringLiteral("p3"), &log));

    whalepet::plugin::PluginContext ctx;
    QCOMPARE(registry.startAll(ctx), 2); // p2 失败被跳过，不阻断其它插件
    QCOMPARE(ctx.capabilities, &registry.capabilities());
    QCOMPARE(static_cast<int>(log.size()), 3);

    log.clear();
    registry.stopAll(); // 逆序停止，且只停止已启动者
    QCOMPARE(static_cast<int>(log.size()), 2);
    QCOMPARE(log.at(0), QStringLiteral("stop:p3"));
    QCOMPARE(log.at(1), QStringLiteral("stop:p1"));
}

void PluginRegistryTest::miniGamePluginsAppearOnBus()
{
    // 既有小游戏链路：MiniGameRegistry 保持原样（内置插件：扫雷 / 找小猫 / 国际象棋）
    whalepet::MiniGameRegistry minigames;
    whalepet::registerBuiltinMiniGames(minigames);
    QCOMPARE(minigames.count(), 3);

    // 经适配器出现在通用总线上（能力 id = minigame.<pluginId>）
    whalepet::plugin::PluginRegistry registry;
    QCOMPARE(whalepet::registerMiniGamePlugins(minigames, registry), 3);
    QVERIFY(registry.capabilities().contains(QStringLiteral("minigame.minesweeper")));
    QVERIFY(registry.capabilities().contains(QStringLiteral("minigame.kitten")));
    QVERIFY(registry.capabilities().contains(QStringLiteral("minigame.chess")));

    const CapabilityDescriptor sweep =
        registry.capabilities().find(QStringLiteral("minigame.minesweeper"))->descriptor();
    QCOMPARE(sweep.displayName, QStringLiteral("扫雷"));
    QCOMPARE(sweep.origin, PluginOrigin::Builtin);

    const CapabilityDescriptor chess =
        registry.capabilities().find(QStringLiteral("minigame.chess"))->descriptor();
    QCOMPARE(chess.displayName, QStringLiteral("国际象棋"));
    QCOMPARE(chess.origin, PluginOrigin::Builtin);

    // 调用返回插件自述元数据
    InvokeContext ctx;
    QJsonObject out;
    QJsonObject error;
    const bool done = registry.capabilities().invoke(QStringLiteral("minigame.kitten"), QJsonObject(),
                                                     ctx, out, error);
    QVERIFY(done);
    QVERIFY(error.isEmpty());
    QCOMPARE(out.value(QStringLiteral("id")).toString(), QStringLiteral("kitten"));
    QCOMPARE(out.value(QStringLiteral("menuLabel")).toString(), QStringLiteral("找小猫"));
    QVERIFY(out.value(QStringLiteral("description")).toString().contains(QStringLiteral("外部资源")));

    // 适配器不拥有插件：MiniGameRegistry 仍可正常按 id 查找
    QVERIFY(minigames.find(QStringLiteral("minesweeper")) != nullptr);

    // 自造一个假小游戏同样可以接入（保持「新增游戏无需改宿主」）
    whalepet::MiniGameRegistry custom;
    custom.add(std::make_unique<FakeMiniGame>());
    whalepet::plugin::PluginRegistry customRegistry;
    QCOMPARE(whalepet::registerMiniGamePlugins(custom, customRegistry), 1);
    QVERIFY(customRegistry.capabilities().contains(QStringLiteral("minigame.fake")));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    PluginRegistryTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_plugin_registry.moc"
