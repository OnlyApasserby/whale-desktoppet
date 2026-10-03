// EX1.1 验收测试（端到端）：合成靶进程 + 只读读取 + 「未启用即零开销」。
// 对应 docs/ROADMAP-ex1.md §2.6.2.1、§4.3、§4.1（默认关闭即零开销）。

#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <string>
#include <vector>

#include "gamestate/GameProfile.h"
#include "gamestate/GenericChainAdapter.h"
#include "gamestate/IGameMemoryReader.h"

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

using namespace whalepet;

namespace {

std::uint64_t parseHex(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        trimmed = trimmed.mid(2);
    }
    bool ok = false;
    const std::uint64_t value = trimmed.toULongLong(&ok, 16);
    return ok ? value : 0;
}

gamestate::GameFieldSpec makeField(const char *name, const char *kind,
                                   std::vector<std::uint64_t> chain)
{
    gamestate::GameFieldSpec spec;
    spec.name = name;
    spec.kind = kind;
    spec.chain = std::move(chain);
    return spec;
}

bool waitForDescriptor(const QString &path, QJsonObject *out, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            QJsonParseError error {};
            const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                *out = doc.object();
                return true;
            }
        }
        QTest::qWait(50);
    }
    return false;
}

} // namespace

class GameMemoryE2ETest : public QObject {
    Q_OBJECT

private slots:
    void endToEndReadsSynthTarget();
    void zeroOverheadWhenDisabled();
};

void GameMemoryE2ETest::endToEndReadsSynthTarget()
{
    if (!gamestate::Win32GameMemoryReader::isSupported()) {
        QSKIP("当前平台不支持只读进程内存");
    }
    const QString exe = QStringLiteral(WHALEPET_GAME_TARGET_EXE);
    QVERIFY2(QFile::exists(exe), qPrintable(exe));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString descriptorPath = dir.filePath(QStringLiteral("descriptor.json"));

    QProcess process;
    process.setProgram(exe);
    process.setArguments({ descriptorPath, QStringLiteral("--hp2=60"),
                           QStringLiteral("--hp2-delay=300") });
    process.start();
    QVERIFY(process.waitForStarted(5000));

    QJsonObject descriptor;
    QVERIFY2(waitForDescriptor(descriptorPath, &descriptor, 8000), "靶进程未写出描述文件");

    gamestate::GameProfile profile;
    profile.engine = "generic";
    profile.process = QFileInfo(exe).fileName().toStdString();
    profile.module = descriptor.value(QStringLiteral("module")).toString().toStdString();
    profile.validation.magicOffset =
        parseHex(descriptor.value(QStringLiteral("magicRva")).toString());
    profile.validation.magic =
        static_cast<std::uint32_t>(parseHex(descriptor.value(QStringLiteral("magic")).toString()));
    profile.validation.hasMagic = true;
    profile.validation.maxJumps = 4;

    const std::uint64_t rootRva = parseHex(descriptor.value(QStringLiteral("rootRva")).toString());
    const QJsonObject chainOffsets = descriptor.value(QStringLiteral("chainOffsets")).toObject();
    const QJsonObject fieldOffsets = descriptor.value(QStringLiteral("fieldOffsets")).toObject();
    auto chainOf = [&](const QString &name) {
        return std::vector<std::uint64_t> {
            rootRva,
            static_cast<std::uint64_t>(
                chainOffsets.value(QStringLiteral("p1")).toDouble()),
            static_cast<std::uint64_t>(
                chainOffsets.value(QStringLiteral("p2")).toDouble()),
            static_cast<std::uint64_t>(
                chainOffsets.value(QStringLiteral("p3")).toDouble()),
            static_cast<std::uint64_t>(fieldOffsets.value(name).toDouble()),
        };
    };
    profile.fields.push_back(makeField("hp", "int32", chainOf(QStringLiteral("hp"))));
    profile.fields.push_back(makeField("hpMax", "int32", chainOf(QStringLiteral("hpMax"))));
    profile.fields.push_back(makeField("gold", "int64", chainOf(QStringLiteral("gold"))));
    profile.fields.push_back(makeField("level", "int32", chainOf(QStringLiteral("level"))));
    profile.fields.push_back(makeField("posX", "float", chainOf(QStringLiteral("posX"))));
    profile.fields.push_back(makeField("posY", "float", chainOf(QStringLiteral("posY"))));
    profile.fields.push_back(makeField("mapName", "utf16", chainOf(QStringLiteral("mapName"))));

    gamestate::GenericChainAdapter adapter;
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));
    QVERIFY(adapter.attached());

    core::GameSample sample;
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QVERIFY(sample.available);
    QCOMPARE(sample.hp, 150.0);
    QCOMPARE(sample.hpMax, 200.0);
    QCOMPARE(sample.gold, 3500LL);
    QCOMPARE(sample.level, 7);
    QVERIFY(qAbs(sample.posX - 12.5) < 1e-6);
    QVERIFY(qAbs(sample.posY - 30.5) < 1e-6);
    QCOMPARE(QString::fromStdString(sample.mapName), QStringLiteral("Map001"));
    QCOMPARE(sample.specialScene, 0);

    // 运行期数值变化能被再次读到（验证是实时读取而非一次性快照）
    QTest::qWait(900);
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QCOMPARE(sample.hp, 60.0);

    // 目标退出 → 明确失败且不伪造数据；连续 3 次 → 档案失效
    process.kill();
    QVERIFY(process.waitForFinished(5000));
    for (int i = 0; i < 3; ++i) {
        core::GameSample dead;
        QString deadError;
        QVERIFY(!adapter.read(&dead, &deadError));
        QVERIFY(!dead.available);
        QVERIFY(!deadError.isEmpty());
    }
    QVERIFY(adapter.invalidated());
}

void GameMemoryE2ETest::zeroOverheadWhenDisabled()
{
    if (!gamestate::Win32GameMemoryReader::isSupported()) {
        QSKIP("当前平台不支持只读进程内存");
    }

    // 未 attach：不打开任何进程、不返回任何数据
    gamestate::Win32GameMemoryReader reader;
    QVERIFY(!reader.attached());
    QCOMPARE(reader.moduleBase("kernel32.dll"), std::uint64_t(0));
    QVERIFY(!reader.read(0x1000, nullptr, 0));
    QCOMPARE(reader.processId(), 0);

#if defined(Q_OS_WIN)
    DWORD before = 0;
    DWORD after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    {
        gamestate::Win32GameMemoryReader idle;
        QVERIFY(!idle.attached());
    }
    GetProcessHandleCount(GetCurrentProcess(), &after);
    QCOMPARE(after, before); // 未启用 → 不新增任何句柄（零开销）
#endif

    // 目标不存在 → 明确报错，不静默成功
    gamestate::GameProfile profile;
    profile.engine = "generic";
    profile.process = "__no_such_process__.exe";
    profile.module = "x.dll";
    profile.fields.push_back(makeField("hp", "int32", { 0x10 }));
    QString error;
    gamestate::Win32GameMemoryReader missing;
    QVERIFY(!missing.attach(profile, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!missing.attached());
}

QTEST_MAIN(GameMemoryE2ETest)
#include "test_game_memory_e2e.moc"
