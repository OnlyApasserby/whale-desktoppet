// 测试用最小 MCP server（stdio，Content-Length 分帧 JSON-RPC）。
//
// 仅供 tests/test_process_plugin.cpp 作为「外部进程插件」子进程使用，**不是产品组件**：
// 它把 ProcessPluginLoader（MCP Client）从「拉起 → 握手 → 发现 → 转发 → 超时 → 崩溃隔离」
// 整条链路跑通，并可控地产生「延迟响应 / 报错 / 进程崩溃」三类边界情形。
//
// 提供的 tool：
//   echo  {任意}          → 原样回显 arguments（验证转发与结果回投）
//   sleep {ms}            → 阻塞 ms 毫秒后响应（验证调用超时）
//   fail  {任意}          → 返回 -32001（验证错误透传；不导致进程退出）
//   crash {任意}          → 立即退出进程（验证崩溃隔离）
//
// 编译为**控制台**程序（不加 WIN32），stdin/stdout 即协议通道。
// I/O 一律走标准 C stdio（fread/fwrite + 二进制模式）：这是 stdio 管道场景下最可靠的方式，
// 不依赖 Qt 设备层（QFile(FILE*) 在管道上行为不确定）。

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef Q_OS_WIN
#  include <fcntl.h>
#  include <io.h>
#endif

namespace {

bool readExact(char *destination, std::size_t count)
{
    std::size_t got = 0;
    while (got < count) {
        const std::size_t read = std::fread(destination + got, 1, count - got, stdin);
        if (read == 0) {
            return false; // EOF / 错误
        }
        got += read;
    }
    return true;
}

bool readFrame(QByteArray &out)
{
    QByteArray header;
    char c = 0;
    while (true) {
        if (!readExact(&c, 1)) {
            return false;
        }
        header.append(c);
        if (header.endsWith("\r\n\r\n")) {
            break;
        }
    }

    int length = -1;
    const QList<QByteArray> lines = header.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            length = trimmed.mid(15).trimmed().toInt();
        }
    }
    if (length < 0) {
        return false;
    }

    out.resize(length);
    return readExact(out.data(), static_cast<std::size_t>(length));
}

void writeFrame(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.append("Content-Length: ");
    frame.append(QByteArray::number(payload.size()));
    frame.append("\r\n\r\n");
    frame.append(payload);
    std::fwrite(frame.constData(), 1, static_cast<std::size_t>(frame.size()), stdout);
    std::fflush(stdout);
}

QJsonObject resultMessage(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("result"), result);
    return message;
}

QJsonObject errorMessage(const QJsonValue &id, int code, const QString &text)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), text);
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("error"), error);
    return message;
}

QJsonObject makeTool(const QString &name, const QString &description)
{
    QJsonObject tool;
    tool.insert(QStringLiteral("name"), name);
    tool.insert(QStringLiteral("description"), description);
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), QJsonObject());
    tool.insert(QStringLiteral("inputSchema"), schema);
    return tool;
}

} // namespace

int main()
{
#ifdef Q_OS_WIN
    // 二进制模式：不把 \n 转成 \r\n，保证 Content-Length 精确
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::fprintf(stderr, "[mcp_test_server] started\n");
    std::fflush(stderr);

    while (true) {
        QByteArray body;
        if (!readFrame(body)) {
            break; // 父进程关闭 stdin
        }
        const QJsonObject request = QJsonDocument::fromJson(body).object();
        const QString method = request.value(QStringLiteral("method")).toString();
        const QJsonValue id = request.value(QStringLiteral("id"));

        if (method == QLatin1String("initialize")) {
            QJsonObject serverInfo;
            serverInfo.insert(QStringLiteral("name"), QStringLiteral("mcp-test-server"));
            serverInfo.insert(QStringLiteral("version"), QStringLiteral("1.0"));
            QJsonObject toolsCapability;
            toolsCapability.insert(QStringLiteral("listChanged"), false);
            QJsonObject capabilities;
            capabilities.insert(QStringLiteral("tools"), toolsCapability);
            QJsonObject result;
            result.insert(QStringLiteral("protocolVersion"), QStringLiteral("2026-01-01"));
            result.insert(QStringLiteral("serverInfo"), serverInfo);
            result.insert(QStringLiteral("capabilities"), capabilities);
            writeFrame(resultMessage(id, result));
        } else if (method == QLatin1String("notifications/initialized")) {
            // 通知：不响应
        } else if (method == QLatin1String("tools/list")) {
            QJsonArray tools;
            tools.append(makeTool(QStringLiteral("echo"), QStringLiteral("回显 arguments")));
            tools.append(makeTool(QStringLiteral("sleep"), QStringLiteral("延迟响应（测试超时）")));
            tools.append(makeTool(QStringLiteral("fail"), QStringLiteral("返回错误（不退出）")));
            tools.append(makeTool(QStringLiteral("crash"), QStringLiteral("立即退出（测试崩溃隔离）")));
            QJsonObject result;
            result.insert(QStringLiteral("tools"), tools);
            writeFrame(resultMessage(id, result));
        } else if (method == QLatin1String("tools/call")) {
            const QJsonObject params = request.value(QStringLiteral("params")).toObject();
            const QString name = params.value(QStringLiteral("name")).toString();
            const QJsonObject arguments = params.value(QStringLiteral("arguments")).toObject();

            if (name == QLatin1String("echo")) {
                QJsonObject content;
                content.insert(QStringLiteral("type"), QStringLiteral("text"));
                content.insert(QStringLiteral("text"),
                               QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));
                QJsonObject result;
                result.insert(QStringLiteral("content"), QJsonArray{ content });
                writeFrame(resultMessage(id, result));
            } else if (name == QLatin1String("sleep")) {
                const int ms = arguments.value(QStringLiteral("ms")).toInt(5000);
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                QJsonObject result;
                result.insert(QStringLiteral("sleptMs"), ms);
                writeFrame(resultMessage(id, result));
            } else if (name == QLatin1String("fail")) {
                writeFrame(errorMessage(id, -32001, QStringLiteral("故意失败")));
            } else if (name == QLatin1String("crash")) {
                std::fflush(stdout);
                return 3; // 模拟第三方插件崩溃
            } else {
                writeFrame(errorMessage(id, -32601, QStringLiteral("未知 tool")));
            }
        } else if (!id.isNull()) {
            writeFrame(errorMessage(id, -32601, QStringLiteral("未知方法")));
        }
    }

    return 0;
}
