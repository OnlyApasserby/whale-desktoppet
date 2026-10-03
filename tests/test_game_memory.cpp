// EX1.1 验收测试（离线部分）：profile 解析 / 指针链解析 / 连续失败失效。
// 对应 docs/ROADMAP-ex1.md §2.6.2.1、§2.7、§4.3、§5.1。

#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gamestate/ChainSampler.h"
#include "gamestate/GameProfile.h"
#include "gamestate/IGameMemoryReader.h"
#include "gamestate/PointerChainResolver.h"

using namespace whalepet;

namespace {

// 可编排的假内存：线性缓冲 + 可毒化的不可读地址（用于脱系统单测）
class FakeMemoryReader final : public gamestate::IGameMemoryReader {
public:
    void reserve(std::uint64_t base, std::size_t size)
    {
        m_base = base;
        m_mem.assign(size, 0);
    }
    void setModule(const std::string &name, std::uint64_t base) { m_modules[name] = base; }
    void poison(std::uint64_t address) { m_poison.insert(address); }

    void putU64(std::uint64_t address, std::uint64_t value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putI32(std::uint64_t address, std::int32_t value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putF32(std::uint64_t address, float value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putU8(std::uint64_t address, unsigned char value)
    {
        m_mem[static_cast<std::size_t>(address - m_base)] = value;
    }
    void putUtf16(std::uint64_t address, const char16_t *text, std::size_t units)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], text,
                    units * sizeof(char16_t));
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
        if (m_poison.count(address) != 0) {
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
    std::set<std::uint64_t> m_poison;
};

gamestate::GameFieldSpec makeField(const char *name, const char *kind,
                                   std::vector<std::uint64_t> chain)
{
    gamestate::GameFieldSpec spec;
    spec.name = name;
    spec.kind = kind;
    spec.chain = std::move(chain);
    return spec;
}

} // namespace

class GameMemoryTest : public QObject {
    Q_OBJECT

private slots:
    void profileLoaderAcceptsValidDocument();
    void profileLoaderRejectsInvalidDocuments();
    void resolverReadsAllFieldKinds();
    void resolverRejectsUnsafeChains();
    void resolverChecksMagic();
    void samplerInvalidatesAfterConsecutiveFailures();
};

void GameMemoryTest::profileLoaderAcceptsValidDocument()
{
    const char *json = R"({
        "engine": "generic",
        "process": "Game.exe",
        "module": "Game.exe",
        "moduleBaseOffset": "0x0",
        "validation": { "magicOffset": "0x10", "magic": "0x57484C31", "maxJumps": 4 },
        "fields": [
            { "name": "hp", "kind": "int32", "chain": ["0x1A2B3C", "0x18", "0x24"], "freq": "high" },
            { "name": "gold", "kind": "int64", "chain": ["0x1A2B3C", "0x18", "0x30"], "freq": "mid" }
        ]
    })";
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    gamestate::GameProfile profile;
    QString error;
    QVERIFY2(gamestate::ProfileLoader::loadFromJson(doc.object(), &profile, &error),
             qPrintable(error));
    QCOMPARE(QString::fromStdString(profile.engine), QStringLiteral("generic"));
    QCOMPARE(profile.validation.maxJumps, 4);
    QVERIFY(profile.validation.hasMagic);
    QCOMPARE(profile.validation.magic, std::uint32_t(0x57484C31));
    QCOMPARE(profile.validation.magicOffset, std::uint64_t(0x10));
    QCOMPARE(profile.fields.size(), std::size_t(2));
    QCOMPARE(profile.fields.front().chain.size(), std::size_t(3));
    QCOMPARE(profile.fields.front().chain[0], std::uint64_t(0x1A2B3C));
    QVERIFY(profile.isMemoryEngine());
    QCOMPARE(QString::fromStdString(profile.fields.front().name), QStringLiteral("hp"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("profile.json"));
    QVERIFY2(gamestate::ProfileLoader::saveToFile(profile, path, &error), qPrintable(error));
    gamestate::GameProfile reloaded;
    QVERIFY2(gamestate::ProfileLoader::loadFromFile(path, &reloaded, &error), qPrintable(error));
    QCOMPARE(reloaded.fields.size(), profile.fields.size());
    QCOMPARE(reloaded.validation.magic, profile.validation.magic);
    QCOMPARE(reloaded.fields.at(1).chain.at(2), std::uint64_t(0x30));
}

void GameMemoryTest::profileLoaderRejectsInvalidDocuments()
{
    QString error;
    gamestate::GameProfile profile;

    const auto reject = [&](const char *json) {
        error.clear();
        return !gamestate::ProfileLoader::loadFromJson(
            QJsonDocument::fromJson(json).object(), &profile, &error);
    };

    QVERIFY(reject(R"({"engine":"unreal","process":"a.exe","module":"a.exe","fields":[]})"));
    QVERIFY(!error.isEmpty());
    QVERIFY(reject(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "validation":{"maxJumps":1},
        "fields":[{"name":"hp","kind":"int32","chain":["0x1","0x2","0x3","0x4"]}]})"));
    QVERIFY(reject(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "fields":[{"name":"hp","kind":"vec3","chain":["0x1"]}]})"));
    QVERIFY(reject(R"({"engine":"unity-il2cpp","process":"a.exe",
        "fields":[{"name":"hp","kind":"int32","chain":["0x1"]}]})"));
    QVERIFY(reject(R"({"engine":"rpgmaker-rgss"})"));
    QVERIFY(reject(R"({"engine":"rpgmaker-mv"})"));
    QVERIFY(reject(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "validation":{"magicOffset":"nope"},
        "fields":[{"name":"hp","kind":"int32","chain":["0x1"]}]})"));
}

void GameMemoryTest::resolverReadsAllFieldKinds()
{
    const std::uint64_t base = 0x10000000ULL;
    FakeMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.setModule("fake.dll", base);
    reader.attach({}, nullptr);

    reader.putU64(base + 0x100, base + 0x200); // 静态根 → 对象
    reader.putI32(base + 0x210, 12345);
    reader.putF32(base + 0x214, 3.5f);
    reader.putU8(base + 0x218, 1);
    const char16_t text[] = u"Hi";
    reader.putUtf16(base + 0x220, text, 3);

    gamestate::PointerChainResolver resolver(&reader);
    resolver.setMaxJumps(4);
    QString error;

    struct Case {
        const char *kind;
        std::uint64_t offset;
    };
    const Case cases[] = { { "int32", 0x10 }, { "float", 0x14 }, { "bool", 0x18 } };
    for (const Case &c : cases) {
        gamestate::GameFieldValue value;
        QVERIFY2(resolver.readField(makeField("f", c.kind, { 0x100, c.offset }), base, &value, &error),
                 qPrintable(error));
        if (std::strcmp(c.kind, "int32") == 0) {
            QCOMPARE(value.integer, 12345LL);
            QCOMPARE(value.number, 12345.0);
        } else if (std::strcmp(c.kind, "float") == 0) {
            QVERIFY(qAbs(value.number - 3.5) < 1e-6);
        } else {
            QVERIFY(value.boolean);
        }
    }

    gamestate::GameFieldValue textValue;
    QVERIFY(resolver.readField(makeField("mapName", "utf16", { 0x100, 0x20 }), base, &textValue,
                               &error));
    QCOMPARE(QString::fromStdString(textValue.text), QStringLiteral("Hi"));

    // 三级链（2 跳）：base+0x100 → base+0x200 → +0x30 处指针 → base+0x300 → +0x20
    reader.putU64(base + 0x230, base + 0x300);
    reader.putI32(base + 0x320, 0xABCD);
    gamestate::GameFieldValue deep;
    QVERIFY(resolver.readField(makeField("deep", "int32", { 0x100, 0x30, 0x20 }), base, &deep,
                               &error));
    QCOMPARE(deep.integer, 0xABCDLL);
    QCOMPARE(resolver.bytesThisRound() > 0, true);
}

void GameMemoryTest::resolverRejectsUnsafeChains()
{
    const std::uint64_t base = 0x20000000ULL;
    FakeMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.attach({}, nullptr);
    reader.putU64(base + 0x100, base + 0x200);

    gamestate::PointerChainResolver resolver(&reader);
    QString error;
    std::uint64_t address = 0;

    resolver.setMaxJumps(1);
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0x100, 0x0, 0x0 }), base, &address, &error));
    QVERIFY(!error.isEmpty());

    resolver.setMaxJumps(4);
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0x100 }), 0, &address, &error));

    reader.putU64(base + 0x300, 0); // 空指针
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0x300, 0x0 }), base, &address, &error));

    reader.poison(base + 0x400); // 不可读
    gamestate::GameFieldValue value;
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x400 }), base, &value, &error));

    resolver.setMaxBytesPerRound(2); // 字节预算不足
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x100, 0x10 }), base, &value, &error));

    QVERIFY(!resolver.resolve(makeField("hp", "int32", {}), base, &address, &error));
}

void GameMemoryTest::resolverChecksMagic()
{
    const std::uint64_t base = 0x30000000ULL;
    FakeMemoryReader reader;
    reader.reserve(base, 0x100);
    reader.attach({}, nullptr);
    reader.putI32(base + 0x10, 0x57484C31);

    gamestate::PointerChainResolver resolver(&reader);
    gamestate::GameProfile::Validation validation;
    validation.hasMagic = true;
    validation.magicOffset = 0x10;
    validation.magic = 0x57484C31;

    QString error;
    QVERIFY2(resolver.verifyMagic(base, validation, &error), qPrintable(error));

    validation.magic = 0xDEADBEEF;
    QVERIFY(!resolver.verifyMagic(base, validation, &error));
    QVERIFY(!error.isEmpty());

    validation.hasMagic = false;
    QVERIFY(resolver.verifyMagic(base, validation, &error));
}

void GameMemoryTest::samplerInvalidatesAfterConsecutiveFailures()
{
    const std::uint64_t base = 0x40000000ULL;
    FakeMemoryReader reader;
    reader.reserve(base, 0x100);
    reader.setModule("fake.dll", base);
    reader.attach({}, nullptr);

    gamestate::GameProfile profile;
    profile.engine = "generic";
    profile.module = "fake.dll";
    profile.validation.maxJumps = 4;
    profile.fields.push_back(makeField("hp", "int32", { 0x80, 0x0 })); // 初始为空指针

    gamestate::ChainSampler sampler(&reader, &profile);
    for (int i = 0; i < gamestate::kGameInvalidateAfterFailures; ++i) {
        core::GameSample sample;
        QString error;
        QVERIFY(!sampler.sample(&sample, &error));
        QVERIFY(!sample.available);
        QVERIFY(!error.isEmpty());
    }
    QVERIFY(sampler.invalidated());
    QCOMPARE(sampler.consecutiveFailures(), gamestate::kGameInvalidateAfterFailures);

    // 失效后即使数据恢复也不再读取（须显式 reset 重连）
    reader.putU64(base + 0x80, base + 0x90);
    reader.putI32(base + 0x90, 77);
    core::GameSample sample;
    QString error;
    QVERIFY(!sampler.sample(&sample, &error));
    QVERIFY(!sample.available);

    sampler.reset();
    QVERIFY2(sampler.sample(&sample, &error), qPrintable(error));
    QCOMPARE(sample.hp, 77.0);
    QVERIFY(sample.available);
}

QTEST_MAIN(GameMemoryTest)
#include "test_game_memory.moc"
