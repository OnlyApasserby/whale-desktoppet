// EX1.2 验收测试（离线部分）：Unity 后端判定 / dump.cs→profile 转换 /
// Mono 与 IL2CPP 适配器（假读取器）/ 工厂路由。
// 对应 docs/ROADMAP-ex1.md §2.6.1、§2.7、§4.3、§5.1。

#include <QtTest/QtTest>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/GameState.h"
#include "gamestate/GameProfile.h"
#include "gamestate/UnityDumpConverter.h"
#include "gamestate/UnityIl2CppAdapter.h"
#include "gamestate/UnityMonoAdapter.h"
#include "gamestate/UnityRuntime.h"

using namespace whalepet;

namespace {

// 与 test_game_memory.cpp 同构的最小假读取器（线性缓冲 + 模块表）。
class FakeMemoryReader final : public gamestate::IGameMemoryReader {
public:
    void reserve(std::uint64_t base, std::size_t size)
    {
        m_base = base;
        m_mem.assign(size, 0);
    }
    void setModule(const std::string &name, std::uint64_t base) { m_modules[name] = base; }
    void putI32(std::uint64_t address, std::int32_t value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putF32(std::uint64_t address, float value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putU64(std::uint64_t address, std::uint64_t value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }

    bool attach(const gamestate::GameProfile &, QString *) override
    {
        m_attached = true;
        return true;
    }
    void detach() override { m_attached = false; }
    bool attached() const override { return m_attached; }
    std::uint64_t moduleBase(const std::string &name) const override
    {
        const auto it = m_modules.find(name);
        return it == m_modules.end() ? 0 : it->second;
    }
    bool read(std::uint64_t address, void *buffer, std::size_t size) override
    {
        if (!m_attached || address < m_base) {
            return false;
        }
        const std::uint64_t offset = address - m_base;
        if (offset + size > m_mem.size()) {
            return false;
        }
        std::memcpy(buffer, m_mem.data() + static_cast<std::size_t>(offset), size);
        return true;
    }
    QString lastError() const override { return QStringLiteral("fake-read-error"); }
    int processId() const override { return 1; }

private:
    bool m_attached = false;
    std::uint64_t m_base = 0;
    std::vector<std::uint8_t> m_mem;
    std::map<std::string, std::uint64_t> m_modules;
};

const char *const kDumpCs = R"(// Namespace: Game
public class Player : MonoBehaviour // TypeDefIndex: 100
{
	// Fields
	public int hp; // 0x10
	public int gold; // 0x14
	public static float posX; // 0x18
	private Monster target; // 0x20

	// Methods
	// RVA: 0x1234 Offset: 0x1234 VA: 0x1234
	public void TakeDamage(int dmg) { }
}

// Namespace: 
public class Monster // TypeDefIndex: 200
{
	// Fields
	public int level; // 0x30
}
)";

} // namespace

class UnityAdaptersTest : public QObject {
    Q_OBJECT

private slots:
    void parseFieldOffsetsExtractsByClassAndName();
    void buildProfileResolvesNamesAndChains();
    void buildProfileFailsWhenFieldMissing();
    void detectBackendPrefersMonoThenIl2Cpp();
    void monoAdapterReadsViaConverterProfile();
    void il2cppAdapterDegradesWhenModuleMissing();
    void adaptersInvalidateAfterConsecutiveFailures();
    void factoryRoutesEngines();
};

void UnityAdaptersTest::parseFieldOffsetsExtractsByClassAndName()
{
    std::map<std::string, std::map<std::string, std::uint64_t>> offsets;
    QString error;
    QVERIFY2(gamestate::UnityDumpConverter::parseFieldOffsets(QString::fromUtf8(kDumpCs), &offsets,
                                                              &error),
             qPrintable(error));

    QVERIFY(offsets.count("Game.Player") == 1);
    QCOMPARE(offsets.at("Game.Player").at("hp"), std::uint64_t(0x10));
    QCOMPARE(offsets.at("Game.Player").at("gold"), std::uint64_t(0x14));
    QCOMPARE(offsets.at("Game.Player").at("posX"), std::uint64_t(0x18));
    QCOMPARE(offsets.at("Game.Player").at("target"), std::uint64_t(0x20));
    // 简单类名冗余登记
    QCOMPARE(offsets.at("Player").at("hp"), std::uint64_t(0x10));
    // 无命名空间的类
    QCOMPARE(offsets.at("Monster").at("level"), std::uint64_t(0x30));

    // 空输入 / 无字段输入必须失败并给出原因
    QString emptyError;
    QVERIFY(!gamestate::UnityDumpConverter::parseFieldOffsets(QString(), &offsets, &emptyError));
    QVERIFY(!emptyError.isEmpty());
    QString noFieldError;
    QVERIFY(!gamestate::UnityDumpConverter::parseFieldOffsets(
        QStringLiteral("public class A { }\n"), &offsets, &noFieldError));
    QVERIFY(!noFieldError.isEmpty());
}

void UnityAdaptersTest::buildProfileResolvesNamesAndChains()
{
    gamestate::UnityProfileSpec spec;
    spec.engine = "unity-mono";
    spec.process = "Game.exe";
    spec.staticBaseOffset = 0x100;
    spec.mappings = {
        { "hp", "int32", "Game.Player", "hp", "high", {} },
        { "gold", "int32", "Player", "gold", "mid", {} },
        { "posX", "float", "Game.Player", "posX", "high", {} },
        { "level", "int32", "Monster", "level", "high", {} },
    };

    gamestate::GameProfile profile;
    QString error;
    QVERIFY2(gamestate::UnityDumpConverter::buildProfile(QString::fromUtf8(kDumpCs), spec, &profile,
                                                         &error),
             qPrintable(error));

    QCOMPARE(QString::fromStdString(profile.engine), QStringLiteral("unity-mono"));
    // mono 默认原生模块名
    QCOMPARE(QString::fromStdString(profile.module), QStringLiteral("mono-2.0-bdwgc.dll"));
    QCOMPARE(profile.moduleBaseOffset, std::uint64_t(0x100));
    QCOMPARE(profile.fields.size(), std::size_t(4));
    QCOMPARE(profile.fields.at(0).chain.size(), std::size_t(1));
    QCOMPARE(profile.fields.at(0).chain.at(0), std::uint64_t(0x10));
    QCOMPARE(profile.fields.at(2).chain.at(0), std::uint64_t(0x18));

    // IL2CPP 默认模块名
    gamestate::UnityProfileSpec il2cppSpec;
    il2cppSpec.engine = "unity-il2cpp";
    il2cppSpec.process = "Game.exe";
    il2cppSpec.mappings = { { "hp", "int32", "Game.Player", "hp", "high", {} } };
    gamestate::GameProfile il2cppProfile;
    QVERIFY2(gamestate::UnityDumpConverter::buildProfile(QString::fromUtf8(kDumpCs), il2cppSpec,
                                                         &il2cppProfile, &error),
             qPrintable(error));
    QCOMPARE(QString::fromStdString(il2cppProfile.module), QStringLiteral("GameAssembly.dll"));
}

void UnityAdaptersTest::buildProfileFailsWhenFieldMissing()
{
    gamestate::UnityProfileSpec spec;
    spec.engine = "unity-il2cpp";
    spec.process = "Game.exe";
    spec.mappings = { { "hp", "int32", "Game.Player", "notAField", "high", {} } };

    gamestate::GameProfile profile;
    QString error;
    QVERIFY(!gamestate::UnityDumpConverter::buildProfile(QString::fromUtf8(kDumpCs), spec, &profile,
                                                         &error));
    QVERIFY(error.contains(QStringLiteral("notAField")));
}

void UnityAdaptersTest::detectBackendPrefersMonoThenIl2Cpp()
{
    FakeMemoryReader reader;
    reader.attach({}, nullptr);

    reader.setModule("GameAssembly.dll", 0x1000);
    auto backend = gamestate::detectUnityBackend(&reader);
    QCOMPARE(QString::fromStdString(backend.engine), QStringLiteral("unity-il2cpp"));

    reader.setModule("mono-2.0-bdwgc.dll", 0x2000);
    backend = gamestate::detectUnityBackend(&reader);
    QCOMPARE(QString::fromStdString(backend.engine), QStringLiteral("unity-mono"));
    QCOMPARE(QString::fromStdString(backend.module), QStringLiteral("mono-2.0-bdwgc.dll"));

    FakeMemoryReader none;
    none.attach({}, nullptr);
    QVERIFY(gamestate::detectUnityBackend(&none).engine.empty());
}

void UnityAdaptersTest::monoAdapterReadsViaConverterProfile()
{
    const std::uint64_t base = 0x50000000ULL;
    auto fake = std::make_unique<FakeMemoryReader>();
    fake->reserve(base, 0x1000);
    fake->setModule("mono-2.0-bdwgc.dll", base);
    // 静态字段区基址 = base + 0x100
    fake->putI32(base + 0x110, 123);
    fake->putI32(base + 0x114, 456);
    fake->putF32(base + 0x118, 1.5f);
    fake->putI32(base + 0x130, 7);

    gamestate::UnityProfileSpec spec;
    spec.engine = "unity-mono";
    spec.process = "Game.exe";
    spec.staticBaseOffset = 0x100;
    spec.mappings = {
        { "hp", "int32", "Game.Player", "hp", "high", {} },
        { "gold", "int32", "Game.Player", "gold", "mid", {} },
        { "posX", "float", "Game.Player", "posX", "high", {} },
        { "level", "int32", "Monster", "level", "high", {} },
    };
    gamestate::GameProfile profile;
    QString error;
    QVERIFY2(gamestate::UnityDumpConverter::buildProfile(QString::fromUtf8(kDumpCs), spec, &profile,
                                                         &error),
             qPrintable(error));

    gamestate::UnityMonoAdapter adapter(std::move(fake));
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));
    QVERIFY(adapter.attached());

    core::GameSample sample;
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QVERIFY(sample.available);
    QCOMPARE(sample.hp, 123.0);
    QCOMPARE(sample.gold, 456LL);
    QCOMPARE(sample.level, 7);
    QVERIFY(qAbs(sample.posX - 1.5) < 1e-6);
}

void UnityAdaptersTest::il2cppAdapterDegradesWhenModuleMissing()
{
    const std::uint64_t base = 0x60000000ULL;
    auto fake = std::make_unique<FakeMemoryReader>();
    fake->reserve(base, 0x100);
    // 故意不注册 GameAssembly.dll

    gamestate::GameProfile profile;
    profile.engine = "unity-il2cpp";
    profile.process = "Game.exe";
    profile.module = "GameAssembly.dll";
    gamestate::GameFieldSpec hp;
    hp.name = "hp";
    hp.kind = "int32";
    hp.chain = { 0x10 };
    profile.fields.push_back(hp);

    gamestate::UnityIl2CppAdapter adapter(std::move(fake));
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));

    core::GameSample sample;
    QVERIFY(!adapter.read(&sample, &error));
    QVERIFY(!sample.available);
    QVERIFY(error.contains(QStringLiteral("GameAssembly.dll")));

    // 引擎不匹配必须拒绝
    gamestate::UnityMonoAdapter mono;
    QString mismatch;
    QVERIFY(!mono.attach(profile, &mismatch));
    QVERIFY(mismatch.contains(QStringLiteral("unity-il2cpp")));
}

void UnityAdaptersTest::adaptersInvalidateAfterConsecutiveFailures()
{
    const std::uint64_t base = 0x70000000ULL;
    auto fake = std::make_unique<FakeMemoryReader>();
    fake->reserve(base, 0x100);
    fake->setModule("mono-2.0-bdwgc.dll", base);
    fake->putU64(base + 0x80, 0); // 空指针：链必失败

    gamestate::GameProfile profile;
    profile.engine = "unity-mono";
    profile.process = "Game.exe";
    profile.module = "mono-2.0-bdwgc.dll";
    gamestate::GameFieldSpec hp;
    hp.name = "hp";
    hp.kind = "int32";
    hp.chain = { 0x80, 0x10 };
    profile.fields.push_back(hp);

    gamestate::UnityMonoAdapter adapter(std::move(fake));
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));

    for (int i = 0; i < gamestate::kGameInvalidateAfterFailures; ++i) {
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
    }
    QVERIFY(adapter.invalidated());
    QCOMPARE(adapter.consecutiveFailures(), gamestate::kGameInvalidateAfterFailures);
}

void UnityAdaptersTest::factoryRoutesEngines()
{
    QString error;

    gamestate::GameProfile monoProfile;
    monoProfile.engine = "unity-mono";
    monoProfile.process = "Game.exe";
    monoProfile.module = "mono-2.0-bdwgc.dll";
    auto mono = gamestate::createGameStateAdapter(monoProfile, &error);
    QVERIFY(mono != nullptr);
    QVERIFY(dynamic_cast<gamestate::UnityMonoAdapter *>(mono.get()) != nullptr);

    gamestate::GameProfile il2cppProfile;
    il2cppProfile.engine = "unity-il2cpp";
    il2cppProfile.process = "Game.exe";
    il2cppProfile.module = "GameAssembly.dll";
    auto il2cpp = gamestate::createGameStateAdapter(il2cppProfile, &error);
    QVERIFY(il2cpp != nullptr);
    QVERIFY(dynamic_cast<gamestate::UnityIl2CppAdapter *>(il2cpp.get()) != nullptr);

    gamestate::GameProfile genericProfile;
    genericProfile.engine = "generic";
    genericProfile.process = "Game.exe";
    genericProfile.module = "Game.exe";
    auto generic = gamestate::createGameStateAdapter(genericProfile, &error);
    QVERIFY(generic != nullptr);
    QVERIFY(dynamic_cast<gamestate::UnityMonoAdapter *>(generic.get()) == nullptr);
    QVERIFY(dynamic_cast<gamestate::UnityIl2CppAdapter *>(generic.get()) == nullptr);

    gamestate::GameProfile unknown;
    unknown.engine = "rpgmaker-mv";
    QVERIFY(gamestate::createGameStateAdapter(unknown, &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(UnityAdaptersTest)
#include "test_unity_adapters.moc"
