#include "gamestate/GameProfile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace whalepet::gamestate {

namespace {

const char *const kEngines[] = {
    "unity-mono", "unity-il2cpp", "rpgmaker-mv", "rpgmaker-mz", "rpgmaker-rgss", "generic",
};
constexpr std::size_t kEngineCount = sizeof(kEngines) / sizeof(kEngines[0]);

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

// 接受 "0x1A2B3C"（十六进制）或十进制字符串 / JSON 数字
bool parseU64(const QJsonValue &value, std::uint64_t *out, QString *error, const char *what)
{
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (d < 0.0) {
            return fail(error, QStringLiteral("%1 不能为负").arg(QString::fromLatin1(what)));
        }
        *out = static_cast<std::uint64_t>(d);
        return true;
    }
    if (!value.isString()) {
        return fail(error, QStringLiteral("%1 必须是字符串或数字").arg(QString::fromLatin1(what)));
    }
    const QString text = value.toString().trimmed();
    bool ok = false;
    std::uint64_t parsed = 0;
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        parsed = text.mid(2).toULongLong(&ok, 16);
    } else {
        parsed = text.toULongLong(&ok, 10);
    }
    if (!ok) {
        return fail(error, QStringLiteral("%1 无法解析为地址/偏移：%2")
                               .arg(QString::fromLatin1(what), text));
    }
    *out = parsed;
    return true;
}

} // namespace

const char *const *knownEngines(std::size_t *count)
{
    if (count != nullptr) {
        *count = kEngineCount;
    }
    return kEngines;
}

bool isKnownEngine(const std::string &engine)
{
    for (std::size_t i = 0; i < kEngineCount; ++i) {
        if (engine == kEngines[i]) {
            return true;
        }
    }
    return false;
}

const char *gameFieldKindId(GameFieldKind kind)
{
    switch (kind) {
    case GameFieldKind::Int32:
        return "int32";
    case GameFieldKind::Int64:
        return "int64";
    case GameFieldKind::Float:
        return "float";
    case GameFieldKind::Double:
        return "double";
    case GameFieldKind::Bool:
        return "bool";
    case GameFieldKind::Utf16:
        return "utf16";
    case GameFieldKind::Unknown:
        break;
    }
    return "unknown";
}

GameFieldKind gameFieldKindFromId(const std::string &id)
{
    for (int i = 0; i <= static_cast<int>(GameFieldKind::Utf16); ++i) {
        const GameFieldKind kind = static_cast<GameFieldKind>(i);
        if (id == gameFieldKindId(kind)) {
            return kind;
        }
    }
    return GameFieldKind::Unknown;
}

const GameFieldSpec *GameProfile::field(const std::string &name) const
{
    for (const GameFieldSpec &spec : fields) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

bool GameProfile::isMemoryEngine() const
{
    return engine == "unity-mono" || engine == "unity-il2cpp" || engine == "generic";
}

bool ProfileLoader::loadFromJson(const QJsonObject &obj, GameProfile *out, QString *error)
{
    if (out == nullptr) {
        return fail(error, QStringLiteral("输出参数为空"));
    }
    GameProfile profile;

    profile.engine = obj.value(QStringLiteral("engine")).toString().toStdString();
    if (profile.engine.empty()) {
        return fail(error, QStringLiteral("缺少 engine 字段"));
    }
    if (!isKnownEngine(profile.engine)) {
        return fail(error, QStringLiteral("未知 engine：%1").arg(QString::fromStdString(profile.engine)));
    }
    profile.process = obj.value(QStringLiteral("process")).toString().toStdString();
    profile.module = obj.value(QStringLiteral("module")).toString().toStdString();

    if (obj.contains(QStringLiteral("moduleBaseOffset"))
        && !parseU64(obj.value(QStringLiteral("moduleBaseOffset")), &profile.moduleBaseOffset, error,
                     "moduleBaseOffset")) {
        return false;
    }

    const QJsonValue validationValue = obj.value(QStringLiteral("validation"));
    if (validationValue.isObject()) {
        const QJsonObject validation = validationValue.toObject();
        if (validation.contains(QStringLiteral("magicOffset"))
            && !parseU64(validation.value(QStringLiteral("magicOffset")),
                         &profile.validation.magicOffset, error, "validation.magicOffset")) {
            return false;
        }
        if (validation.contains(QStringLiteral("magic"))) {
            std::uint64_t magic = 0;
            if (!parseU64(validation.value(QStringLiteral("magic")), &magic, error,
                          "validation.magic")) {
                return false;
            }
            profile.validation.magic = static_cast<std::uint32_t>(magic);
            profile.validation.hasMagic = true;
        }
        const QJsonValue jumpsValue = validation.value(QStringLiteral("maxJumps"));
        if (jumpsValue.isDouble()) {
            profile.validation.maxJumps = jumpsValue.toInt(4);
            if (profile.validation.maxJumps < 0) {
                return fail(error, QStringLiteral("validation.maxJumps 不能为负"));
            }
        }
    }

    const QJsonValue fieldsValue = obj.value(QStringLiteral("fields"));
    if (!fieldsValue.isUndefined() && !fieldsValue.isArray()) {
        return fail(error, QStringLiteral("fields 必须是数组"));
    }
    const QJsonArray fields = fieldsValue.toArray();
    for (const QJsonValue &fieldValue : fields) {
        if (!fieldValue.isObject()) {
            return fail(error, QStringLiteral("fields[] 元素必须是对象"));
        }
        const QJsonObject fieldObj = fieldValue.toObject();
        GameFieldSpec spec;
        spec.name = fieldObj.value(QStringLiteral("name")).toString().toStdString();
        if (spec.name.empty()) {
            return fail(error, QStringLiteral("fields[].name 不能为空"));
        }
        spec.kind = fieldObj.value(QStringLiteral("kind")).toString().toStdString();
        if (gameFieldKindFromId(spec.kind) == GameFieldKind::Unknown) {
            return fail(error, QStringLiteral("字段 %1 的 kind 非法：%2")
                                   .arg(QString::fromStdString(spec.name),
                                        QString::fromStdString(spec.kind)));
        }
        spec.freq = fieldObj.value(QStringLiteral("freq")).toString().toStdString();

        const QJsonValue chainValue = fieldObj.value(QStringLiteral("chain"));
        if (!chainValue.isUndefined()) {
            if (!chainValue.isArray()) {
                return fail(error, QStringLiteral("字段 %1 的 chain 必须是数组")
                                       .arg(QString::fromStdString(spec.name)));
            }
            const QJsonArray chain = chainValue.toArray();
            for (const QJsonValue &offset : chain) {
                std::uint64_t parsed = 0;
                if (!parseU64(offset, &parsed, error, "chain 偏移")) {
                    return false;
                }
                spec.chain.push_back(parsed);
            }
            if (!spec.chain.empty()
                && static_cast<int>(spec.chain.size()) - 1 > profile.validation.maxJumps) {
                return fail(error, QStringLiteral("字段 %1 的跳数 %2 超过 maxJumps %3")
                                       .arg(QString::fromStdString(spec.name))
                                       .arg(spec.chain.size() - 1)
                                       .arg(profile.validation.maxJumps));
            }
        }
        profile.fields.push_back(spec);
    }

    profile.rpgmaker = obj.value(QStringLiteral("rpgmaker")).toObject();
    profile.bridge = obj.value(QStringLiteral("bridge")).toObject();

    const QJsonValue budgetValue = obj.value(QStringLiteral("maxBytesPerRound"));
    if (budgetValue.isDouble()) {
        const double b = budgetValue.toDouble();
        if (b <= 0.0) {
            return fail(error, QStringLiteral("maxBytesPerRound 必须为正"));
        }
        profile.maxBytesPerRound = static_cast<std::uint64_t>(b);
    }

    // 按通道做必填校验（不伪造数据：缺依赖就报错，绝不静默降级）
    if (profile.isMemoryEngine()) {
        if (profile.process.empty()) {
            return fail(error, QStringLiteral("内存通道要求 process 非空"));
        }
        if (profile.module.empty()) {
            return fail(error, QStringLiteral("内存通道要求 module 非空"));
        }
        if (profile.fields.empty()) {
            return fail(error, QStringLiteral("内存通道要求 fields 非空"));
        }
        for (const GameFieldSpec &spec : profile.fields) {
            if (spec.chain.empty()) {
                return fail(error, QStringLiteral("内存通道的字段 %1 缺少 chain")
                                       .arg(QString::fromStdString(spec.name)));
            }
        }
    } else if (profile.engine == "rpgmaker-rgss") {
        const QString kind = profile.bridge.value(QStringLiteral("kind")).toString();
        if (kind != QStringLiteral("file") && kind != QStringLiteral("socket")) {
            return fail(error, QStringLiteral("rpgmaker-rgss 需要 bridge.kind = file | socket"));
        }
        if (profile.bridge.value(QStringLiteral("path")).toString().isEmpty()) {
            return fail(error, QStringLiteral("rpgmaker-rgss 需要 bridge.path"));
        }
    } else if (profile.engine == "rpgmaker-mv" || profile.engine == "rpgmaker-mz") {
        // 方案 A：CDP；chain 可省略（改用 rpgmaker.expressions）。无调试端口时回退 bridge。
        const bool hasExpressions = profile.rpgmaker.value(QStringLiteral("expressions")).isObject();
        const bool hasBridge = profile.bridge.value(QStringLiteral("kind")).isString();
        if (!hasExpressions && !hasBridge) {
            return fail(error, QStringLiteral("%1 需要 rpgmaker.expressions 或 bridge")
                                   .arg(QString::fromStdString(profile.engine)));
        }
    }

    *out = profile;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool ProfileLoader::loadFromFile(const QString &path, GameProfile *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("无法打开 profile：%1（%2）")
                               .arg(path, file.errorString()));
    }
    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(error, QStringLiteral("profile JSON 解析失败（偏移 %1）：%2")
                               .arg(parseError.offset)
                               .arg(parseError.errorString()));
    }
    if (!doc.isObject()) {
        return fail(error, QStringLiteral("profile 根节点必须是 JSON 对象"));
    }
    return loadFromJson(doc.object(), out, error);
}

bool ProfileLoader::saveToFile(const GameProfile &profile, const QString &path, QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("engine"), QString::fromStdString(profile.engine));
    root.insert(QStringLiteral("process"), QString::fromStdString(profile.process));
    root.insert(QStringLiteral("module"), QString::fromStdString(profile.module));
    root.insert(QStringLiteral("moduleBaseOffset"),
                QStringLiteral("0x%1").arg(profile.moduleBaseOffset, 0, 16));

    QJsonObject validation;
    validation.insert(QStringLiteral("magicOffset"),
                      QStringLiteral("0x%1").arg(profile.validation.magicOffset, 0, 16));
    if (profile.validation.hasMagic) {
        validation.insert(QStringLiteral("magic"),
                          QStringLiteral("0x%1")
                              .arg(profile.validation.magic, 8, 16, QLatin1Char('0'))
                              .toUpper());
    }
    validation.insert(QStringLiteral("maxJumps"), profile.validation.maxJumps);
    root.insert(QStringLiteral("validation"), validation);

    QJsonArray fields;
    for (const GameFieldSpec &spec : profile.fields) {
        QJsonObject fieldObj;
        fieldObj.insert(QStringLiteral("name"), QString::fromStdString(spec.name));
        fieldObj.insert(QStringLiteral("kind"), QString::fromStdString(spec.kind));
        QJsonArray chain;
        for (std::uint64_t offset : spec.chain) {
            chain.append(QStringLiteral("0x%1").arg(offset, 0, 16));
        }
        fieldObj.insert(QStringLiteral("chain"), chain);
        if (!spec.freq.empty()) {
            fieldObj.insert(QStringLiteral("freq"), QString::fromStdString(spec.freq));
        }
        fields.append(fieldObj);
    }
    root.insert(QStringLiteral("fields"), fields);

    if (!profile.rpgmaker.isEmpty()) {
        root.insert(QStringLiteral("rpgmaker"), profile.rpgmaker);
    }
    if (!profile.bridge.isEmpty()) {
        root.insert(QStringLiteral("bridge"), profile.bridge);
    }
    root.insert(QStringLiteral("maxBytesPerRound"), static_cast<double>(profile.maxBytesPerRound));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(error, QStringLiteral("无法写入 profile：%1（%2）")
                               .arg(path, file.errorString()));
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return fail(error, QStringLiteral("写入 profile 失败：%1").arg(file.errorString()));
    }
    return true;
}

} // namespace whalepet::gamestate
