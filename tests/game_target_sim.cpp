// 合成靶进程（game_target_sim）：为「只读内存读取器」提供**结构已知、值确定、可复现**的目标。
//
// 用途：EX1.1 端到端验证（模块基址 + 静态根 RVA + 4 级指针链 + 魔数 + 多类型字段）。
// 用法：game_target_sim <descriptor.json> [--hp2=<value>] [--hp2-delay=<ms>]
//   * 启动后把「模块名 / 根 RVA / 魔数 RVA / 各级偏移 / 字段偏移 / 初值」写入 descriptor.json，
//     随后常驻直到被终止；--hp2 用于验证「运行期数值变化能被再次读到」。
//
// 【红线】本进程只暴露内存布局，不含任何写入能力；测试方亦只做只读访问。

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <cstdint>

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

namespace {

struct SimPayload {
    std::int32_t hp;
    std::int32_t hpMax;
    std::int64_t gold;
    std::int32_t level;
    float posX;
    float posY;
    char16_t mapName[32];
};

struct SimLevel3 {
    char pad[8];
    SimPayload *payload;
};

struct SimLevel2 {
    char pad[24];
    SimLevel3 *l3;
};

struct SimLevel1 {
    char pad[16];
    SimLevel2 *l2;
};

struct SimRoot {
    SimLevel1 *l1;
};

std::uint32_t g_magic = 0x57484C31u; // "WHL1"：档案有效性魔数
SimPayload g_payload;
SimLevel3 g_l3;
SimLevel2 g_l2;
SimLevel1 g_l1;
SimRoot g_root;

std::uint64_t moduleBaseAddress()
{
#if defined(Q_OS_WIN)
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
#else
    return 0;
#endif
}

std::string moduleName()
{
#if defined(Q_OS_WIN)
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0) {
        return "game_target_sim.exe";
    }
    QString full = QString::fromWCharArray(path, static_cast<int>(length));
    return full.mid(full.lastIndexOf(QLatin1Char('\\')) + 1).toStdString();
#else
    return "game_target_sim.exe";
#endif
}

QString hex(std::uint64_t value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

void writeDescriptor(const QString &path)
{
    const std::uint64_t base = moduleBaseAddress();
    const std::uint64_t rootRva = reinterpret_cast<std::uintptr_t>(&g_root) - base;
    const std::uint64_t magicRva = reinterpret_cast<std::uintptr_t>(&g_magic) - base;

    QJsonObject descriptor;
    descriptor.insert(QStringLiteral("module"), QString::fromStdString(moduleName()));
    descriptor.insert(QStringLiteral("moduleBase"), hex(base));
    descriptor.insert(QStringLiteral("rootRva"), hex(rootRva));
    descriptor.insert(QStringLiteral("magicRva"), hex(magicRva));
    descriptor.insert(QStringLiteral("magic"), hex(g_magic));
    descriptor.insert(QStringLiteral("maxJumps"), 4);

    QJsonObject chainOffsets;
    chainOffsets.insert(QStringLiteral("p1"), static_cast<double>(offsetof(SimLevel1, l2)));
    chainOffsets.insert(QStringLiteral("p2"), static_cast<double>(offsetof(SimLevel2, l3)));
    chainOffsets.insert(QStringLiteral("p3"), static_cast<double>(offsetof(SimLevel3, payload)));
    descriptor.insert(QStringLiteral("chainOffsets"), chainOffsets);

    QJsonObject fieldOffsets;
    fieldOffsets.insert(QStringLiteral("hp"), static_cast<double>(offsetof(SimPayload, hp)));
    fieldOffsets.insert(QStringLiteral("hpMax"), static_cast<double>(offsetof(SimPayload, hpMax)));
    fieldOffsets.insert(QStringLiteral("gold"), static_cast<double>(offsetof(SimPayload, gold)));
    fieldOffsets.insert(QStringLiteral("level"), static_cast<double>(offsetof(SimPayload, level)));
    fieldOffsets.insert(QStringLiteral("posX"), static_cast<double>(offsetof(SimPayload, posX)));
    fieldOffsets.insert(QStringLiteral("posY"), static_cast<double>(offsetof(SimPayload, posY)));
    fieldOffsets.insert(QStringLiteral("mapName"), static_cast<double>(offsetof(SimPayload, mapName)));
    descriptor.insert(QStringLiteral("fieldOffsets"), fieldOffsets);

    QJsonObject values;
    values.insert(QStringLiteral("hp"), g_payload.hp);
    values.insert(QStringLiteral("hpMax"), g_payload.hpMax);
    values.insert(QStringLiteral("gold"), static_cast<double>(g_payload.gold));
    values.insert(QStringLiteral("level"), g_payload.level);
    values.insert(QStringLiteral("posX"), static_cast<double>(g_payload.posX));
    values.insert(QStringLiteral("posY"), static_cast<double>(g_payload.posY));
    values.insert(QStringLiteral("mapName"), QStringLiteral("Map001"));
    descriptor.insert(QStringLiteral("values"), values);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Indented));
    file.flush();
    file.close();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 布局自检：任何偏移与预期不符都直接拒绝启动（避免靶子本身不可信）
    static_assert(sizeof(std::int32_t) == 4 && sizeof(float) == 4 && sizeof(std::int64_t) == 8,
                  "字段宽度必须为 4/4/8");
    static_assert(offsetof(SimPayload, gold) == 8, "gold 偏移必须为 8");
    static_assert(offsetof(SimPayload, level) == 16, "level 偏移必须为 16");

    g_payload.hp = 150;
    g_payload.hpMax = 200;
    g_payload.gold = 3500;
    g_payload.level = 7;
    g_payload.posX = 12.5f;
    g_payload.posY = 30.5f;
    const char16_t name[] = u"Map001";
    for (std::size_t i = 0; i < sizeof(name) / sizeof(name[0]); ++i) {
        g_payload.mapName[i] = name[i];
    }

    g_l3.payload = &g_payload;
    g_l2.l3 = &g_l3;
    g_l1.l2 = &g_l2;
    g_root.l1 = &g_l1;

    const QStringList args = app.arguments();
    QString descriptorPath;
    if (args.size() > 1) {
        descriptorPath = args.at(1);
    }

    int hp2 = 0;
    int hp2DelayMs = 1200;
    for (const QString &arg : args) {
        if (arg.startsWith(QStringLiteral("--hp2="))) {
            hp2 = arg.mid(6).toInt();
        } else if (arg.startsWith(QStringLiteral("--hp2-delay="))) {
            hp2DelayMs = arg.mid(12).toInt();
        }
    }

    if (!descriptorPath.isEmpty()) {
        writeDescriptor(descriptorPath);
    }
    if (hp2 > 0) {
        QTimer::singleShot(hp2DelayMs, &app, [hp2]() { g_payload.hp = hp2; });
    }

    return app.exec();
}
