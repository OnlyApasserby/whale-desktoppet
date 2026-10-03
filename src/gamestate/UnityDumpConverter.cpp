#include "gamestate/UnityDumpConverter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <cstddef>

namespace whalepet::gamestate {

namespace {

// dump.cs 中的命名空间注释行：`// Namespace: Foo.Bar`（无命名空间时为空）。
const QRegularExpression &namespaceRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*//\s*Namespace:\s*(.*?)\s*$)"));
    return re;
}

// 类/结构体声明行：`public class Player : MonoBehaviour // TypeDefIndex: 123`
const QRegularExpression &classRe()
{
    // 注意：必须写成单一原始字符串——分段时 R"( 的 '(' 会被当作分隔符吞掉。
    static const QRegularExpression re(QStringLiteral(
        R"(^\s*(?:\[[^\]]*\]\s*)?(?:public\s+|internal\s+|private\s+|sealed\s+|abstract\s+|partial\s+|static\s+)*(?:class|struct|interface|enum)\s+([A-Za-z_][A-Za-z0-9_]*))"));
    return re;
}

// 字段行：`public static int gold; // 0x1234`。
// 取分号前「最后一个标识符」为字段名（`` 类型 名; // 0x偏移 ``），避免复杂类型（泛型/数组）误判。
const QRegularExpression &fieldRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(.*?)([A-Za-z_]\w*)\s*;\s*//\s*0x([0-9A-Fa-f]+))"));
    return re;
}

void registerClass(std::map<std::string, std::map<std::string, std::uint64_t>> *out,
                   const QString &simple, const QString &qualified)
{
    const std::string simpleKey = simple.toStdString();
    const std::string qualifiedKey = qualified.toStdString();
    (*out)[qualifiedKey]; // 先创建空表，保证未含字段的类也有登记
    if (qualifiedKey != simpleKey) {
        (*out)[simpleKey];
    }
}

} // namespace

QString UnityDumpConverter::defaultModule(const std::string &engine)
{
    if (engine == "unity-mono") {
        return QStringLiteral("mono-2.0-bdwgc.dll");
    }
    return QStringLiteral("GameAssembly.dll");
}

bool UnityDumpConverter::parseFieldOffsets(
    const QString &dumpCs, std::map<std::string, std::map<std::string, std::uint64_t>> *out,
    QString *error)
{
    if (out == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("输出参数为空");
        }
        return false;
    }
    out->clear();
    if (!namespaceRe().isValid() || !classRe().isValid() || !fieldRe().isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("内部 dump.cs 解析正则无效（实现缺陷）");
        }
        return false;
    }
    if (dumpCs.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("dump.cs 内容为空");
        }
        return false;
    }

    QString currentNamespace;
    std::string currentSimple;
    std::string currentQualified;
    bool haveClass = false;
    int fieldCount = 0;

    const QStringList lines = dumpCs.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QRegularExpressionMatch nsMatch = namespaceRe().match(raw);
        if (nsMatch.hasMatch()) {
            currentNamespace = nsMatch.captured(1).trimmed();
            continue;
        }

        const QRegularExpressionMatch classMatch = classRe().match(raw);
        if (classMatch.hasMatch()) {
            const QString simple = classMatch.captured(1);
            const QString qualified = currentNamespace.isEmpty()
                ? simple
                : currentNamespace + QLatin1Char('.') + simple;
            currentSimple = simple.toStdString();
            currentQualified = qualified.toStdString();
            haveClass = true;
            registerClass(out, simple, qualified);
            continue;
        }

        if (!haveClass) {
            continue;
        }
        const QRegularExpressionMatch fieldMatch = fieldRe().match(raw);
        if (!fieldMatch.hasMatch()) {
            continue;
        }
        const std::string fieldName = fieldMatch.captured(2).toStdString();
        bool ok = false;
        const std::uint64_t offset = fieldMatch.captured(3).toULongLong(&ok, 16);
        if (!ok) {
            continue;
        }
        (*out)[currentQualified][fieldName] = offset;
        if (currentQualified != currentSimple) {
            // 同名简单类多命名空间冲突时后写覆盖；文档已说明该限制。
            (*out)[currentSimple][fieldName] = offset;
        }
        ++fieldCount;
    }

    if (fieldCount == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("dump.cs 中未识别到任何带偏移（// 0x…）的字段");
        }
        return false;
    }
    return true;
}

bool UnityDumpConverter::buildProfile(const QString &dumpCs, const UnityProfileSpec &spec,
                                      GameProfile *out, QString *error)
{
    if (out == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("输出参数为空");
        }
        return false;
    }

    std::map<std::string, std::map<std::string, std::uint64_t>> offsets;
    if (!parseFieldOffsets(dumpCs, &offsets, error)) {
        return false;
    }

    const auto lookup = [&offsets](const std::string &cls, const std::string &field,
                                   std::uint64_t *offset) -> bool {
        const auto classIt = offsets.find(cls);
        if (classIt == offsets.end()) {
            return false;
        }
        const auto fieldIt = classIt->second.find(field);
        if (fieldIt == classIt->second.end()) {
            return false;
        }
        *offset = fieldIt->second;
        return true;
    };

    QJsonArray fields;
    for (const UnityFieldMapping &mapping : spec.mappings) {
        std::uint64_t offset = 0;
        if (!lookup(mapping.cls, mapping.field, &offset)) {
            if (error != nullptr) {
                *error = QStringLiteral("dump.cs 中未找到字段：%1.%2")
                             .arg(QString::fromStdString(mapping.cls),
                                  QString::fromStdString(mapping.field));
            }
            return false;
        }

        QJsonArray chain;
        chain.append(QStringLiteral("0x%1").arg(offset, 0, 16));
        for (const std::uint64_t hop : mapping.tail) {
            chain.append(QStringLiteral("0x%1").arg(hop, 0, 16));
        }

        QJsonObject field;
        field.insert(QStringLiteral("name"), QString::fromStdString(mapping.name));
        field.insert(QStringLiteral("kind"), QString::fromStdString(mapping.kind));
        field.insert(QStringLiteral("chain"), chain);
        if (!mapping.freq.empty()) {
            field.insert(QStringLiteral("freq"), QString::fromStdString(mapping.freq));
        }
        fields.append(field);
    }

    QJsonObject validation;
    validation.insert(QStringLiteral("maxJumps"), spec.validation.maxJumps);
    if (spec.validation.hasMagic) {
        validation.insert(QStringLiteral("magicOffset"),
                          QStringLiteral("0x%1").arg(spec.validation.magicOffset, 0, 16));
        validation.insert(QStringLiteral("magic"),
                          QStringLiteral("0x%1").arg(spec.validation.magic, 0, 16));
    }

    QJsonObject root;
    root.insert(QStringLiteral("engine"), QString::fromStdString(spec.engine));
    root.insert(QStringLiteral("process"), QString::fromStdString(spec.process));
    const QString module = spec.module.empty() ? defaultModule(spec.engine)
                                               : QString::fromStdString(spec.module);
    root.insert(QStringLiteral("module"), module);
    root.insert(QStringLiteral("moduleBaseOffset"),
                QStringLiteral("0x%1").arg(spec.staticBaseOffset, 0, 16));
    root.insert(QStringLiteral("validation"), validation);
    root.insert(QStringLiteral("maxBytesPerRound"),
                QString::number(static_cast<qulonglong>(spec.maxBytesPerRound)));
    root.insert(QStringLiteral("fields"), fields);

    // 经与运行期同一套校验复核，保证产物满足 §2.7 契约。
    return ProfileLoader::loadFromJson(root, out, error);
}

} // namespace whalepet::gamestate
