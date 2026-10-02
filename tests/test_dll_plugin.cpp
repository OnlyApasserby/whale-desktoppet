#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "plugin/Capability.h"
#include "plugin/PluginRegistry.h"
#include "plugin/dll/DllPluginLoader.h"

#include <memory>

// P7.3 动态插件（DLL）（docs/PLUGIN-ARCHITECTURE.md §4.1、docs/ROADMAP-P7-Fin.md P7.3）。
//
// 以**真实构建的插件 DLL**（src/plugin/examples/hello 与 .../badabi）端到端验证：
//   * 合法 DLL 被装载 → 能力注册且可调用（capabilities.list 可见）；
//   * 元数据 apiVersion 高于宿主的 DLL 被**跳过**，且不影响其它插件与装载器继续工作；
//   * 非插件文件（缺 Q_PLUGIN_METADATA）被跳过；
//   * 目录不存在属正常状态（无第三方插件），不报错、不崩溃。
//
// WIN32 专属：DllPluginLoader 按 `*.dll` 扫描，插件目标亦为 Windows 共享库。

using whalepet::plugin::DllLoadReport;
using whalepet::plugin::DllPluginLoader;
using whalepet::plugin::PluginRegistry;

namespace {

QString helloArtifact()
{
    return QString::fromUtf8(WHALEPET_HELLO_PLUGIN);
}

QString badAbiArtifact()
{
    return QString::fromUtf8(WHALEPET_BADABI_PLUGIN);
}

// DllPluginLoader 只按目录扫描，故把构建产物复制进临时目录
bool copyInto(const QString &dir, const QString &source, const QString &name)
{
    const QString dst = dir + QLatin1Char('/') + name;
    QFile::remove(dst);
    return QFile::copy(source, dst);
}

} // namespace

class TestDllPlugin : public QObject {
    Q_OBJECT

private slots:
    void loadsValidPluginAndRegistersCapability();
    void skipsAbiMismatchedPluginAndKeepsOthersWorking();
    void skipsNonPluginFiles();
    void missingDirectoryIsNotAnError();
    void loadedCapabilityIsInvokable();
};

void TestDllPlugin::loadsValidPluginAndRegistersCapability()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(copyInto(dir.path(), helloArtifact(), QStringLiteral("ext_hello.dll")));

    DllPluginLoader loader(dir.path());
    PluginRegistry registry;
    const DllLoadReport report = loader.loadAll(registry);

    QCOMPARE(report.loaded.size(), 1);
    QCOMPARE(report.skipped.size(), 0);
    QCOMPARE(loader.loadedPluginIds(), QStringList{ QStringLiteral("ext.hello") });
    QCOMPARE(registry.count(), 1);
    QVERIFY(registry.find(QStringLiteral("ext.hello")) != nullptr);
    QVERIFY(registry.capabilities().contains(QStringLiteral("ext.hello.greet")));
}

void TestDllPlugin::skipsAbiMismatchedPluginAndKeepsOthersWorking()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 目录名序保证 ext_badabi.dll 先于 ext_hello.dll 被扫描
    QVERIFY(copyInto(dir.path(), badAbiArtifact(), QStringLiteral("ext_badabi.dll")));
    QVERIFY(copyInto(dir.path(), helloArtifact(), QStringLiteral("ext_hello.dll")));

    DllPluginLoader loader(dir.path());
    PluginRegistry registry;
    const DllLoadReport report = loader.loadAll(registry);

    QCOMPARE(report.loaded.size(), 1);
    QCOMPARE(report.skipped, QStringList{ QStringLiteral("ext_badabi.dll") });
    QVERIFY(loader.loadedPluginIds().contains(QStringLiteral("ext.hello")));
    QVERIFY(!loader.loadedPluginIds().contains(QStringLiteral("ext.badabi")));
    // 被跳过者不留任何痕迹，合法插件不受影响
    QVERIFY(!registry.capabilities().contains(QStringLiteral("ext.badabi.greet")));
    QVERIFY(registry.capabilities().contains(QStringLiteral("ext.hello.greet")));
}

void TestDllPlugin::skipsNonPluginFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 名为 *.dll 的普通文件：无 Q_PLUGIN_METADATA，必须被跳过而非 Fatal
    QFile garbage(dir.path() + QStringLiteral("/not_a_plugin.dll"));
    QVERIFY(garbage.open(QIODevice::WriteOnly));
    garbage.write("this is definitely not a Qt plugin");
    garbage.close();

    DllPluginLoader loader(dir.path());
    PluginRegistry registry;
    const DllLoadReport report = loader.loadAll(registry);

    QCOMPARE(report.loaded.size(), 0);
    QCOMPARE(report.skipped, QStringList{ QStringLiteral("not_a_plugin.dll") });
    QCOMPARE(registry.count(), 0);
}

void TestDllPlugin::missingDirectoryIsNotAnError()
{
    const QString missing = QDir::tempPath() + QStringLiteral("/whalepet-no-such-plugin-dir-xyz");
    DllPluginLoader loader(missing);
    PluginRegistry registry;
    const DllLoadReport report = loader.loadAll(registry);

    QCOMPARE(report.loaded.size(), 0);
    QCOMPARE(report.skipped.size(), 0);
    QCOMPARE(loader.loadedPluginIds().size(), 0);
}

void TestDllPlugin::loadedCapabilityIsInvokable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(copyInto(dir.path(), helloArtifact(), QStringLiteral("ext_hello.dll")));

    DllPluginLoader loader(dir.path());
    PluginRegistry registry;
    QVERIFY(loader.loadAll(registry).loaded.size() == 1);

    whalepet::plugin::CapabilityRegistry &caps = registry.capabilities();
    whalepet::plugin::InvokeContext ctx;
    QJsonObject in;
    in.insert(QStringLiteral("name"), QStringLiteral("WhalePet"));
    QJsonObject out;
    QJsonObject error;
    QVERIFY(caps.invoke(QStringLiteral("ext.hello.greet"), in, ctx, out, error));
    QVERIFY(error.isEmpty());
    QCOMPARE(out.value(QStringLiteral("message")).toString(), QStringLiteral("hello, WhalePet"));

    // 来源层必须标为 Dll（影响同名能力仲裁优先级）
    bool found = false;
    const QList<whalepet::plugin::CapabilityDescriptor> descriptors = caps.descriptors();
    for (const whalepet::plugin::CapabilityDescriptor &d : descriptors) {
        if (d.id == QStringLiteral("ext.hello.greet")) {
            found = true;
            QVERIFY(d.origin == whalepet::plugin::PluginOrigin::Dll);
        }
    }
    QVERIFY(found);
}

QTEST_MAIN(TestDllPlugin)
#include "test_dll_plugin.moc"
