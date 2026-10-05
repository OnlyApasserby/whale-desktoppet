#include "plugin/process/ProcessPluginConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace whalepet::plugin {

bool ProcessPluginConfig::parse(const QByteArray &json, ProcessPluginConfig *out, QString *errorOut)
{
    if (out == nullptr) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("输出参数为空");
        }
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }
    if (!document.isArray()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("外部插件配置必须是 JSON 数组");
        }
        return false;
    }

    ProcessPluginConfig result;
    const QJsonArray servers = document.array();
    for (const QJsonValue &value : servers) {
        if (!value.isObject()) {
            ++result.skippedCount;
            result.warnings.append(
                QStringLiteral("已忽略一个非对象条目（应为 {pluginId, program, ...}）"));
            continue;
        }
        const QJsonObject object = value.toObject();
        ProcessServerSpec spec;
        spec.pluginId = object.value(QStringLiteral("pluginId")).toString();
        spec.program = object.value(QStringLiteral("program")).toString();
        const QJsonArray arguments = object.value(QStringLiteral("arguments")).toArray();
        for (const QJsonValue &argument : arguments) {
            spec.arguments.append(argument.toString());
        }
        spec.timeoutMs = object.value(QStringLiteral("timeoutMs")).toInt(2000);
        result.servers.push_back(spec);
    }

    if (errorOut != nullptr) {
        errorOut->clear();
    }
    *out = result;
    return true;
}

bool ProcessPluginConfig::loadFromFile(const QString &path, ProcessPluginConfig *out,
                                       QString *errorOut)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("未发现外部插件配置（plugins.json）");
        }
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("无法读取外部插件配置：%1").arg(file.errorString());
        }
        return false;
    }
    return parse(file.readAll(), out, errorOut);
}

} // namespace whalepet::plugin
