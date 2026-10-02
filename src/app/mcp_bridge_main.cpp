// whalepet-mcp —— WhalePet Context API 的 MCP stdio 桥接进程（P7.2）。
//
// 背景：主程序是 WIN32 GUI 子系统，没有可用的 stdin/stdout，无法直接充当
// MCP stdio Server（见 docs/CONTEXT-API.md §4 与 StdioTransport.h 的说明）。
// 因此把「MCP 客户端 ←→ 主程序」拆成两段：
//
//   MCP 客户端 ──stdio(Content-Length 分帧)──> whalepet-mcp.exe ──命名管道──> 主程序
//
// 本进程**只做字节转发**，不含任何业务逻辑：
//   * 读 stdin 的每一帧，原样转发到主程序监听的命名管道（kDefaultContextPipeName，
//     可用 --pipe 覆盖）；
//   * 若帧是**请求**（含 id），则阻塞等待管道的**一帧**响应并写回 stdout；
//     若是**通知**（无 id），不等待；
//   * 管道断开（主程序退出）即退出，避免留下孤儿进程；
//   * --token 非空时，仅把 token 注入 `initialize` 的 params（主程序侧按
//     `initialize.params.token` 鉴权，见 §5）。
//
// 主程序侧由 LocalPipeTransport 用 QLocalServer 监听，每条连接复用 StdioTransport，
// 因而分帧 / MCP 方法映射（initialize / tools/list / tools/call）/ token 门控与
// stdio 通道完全同源，保证「两通道共用同一 dispatcher 与能力表」。

#include "contextapi/transport/LocalPipeTransport.h" // kDefaultContextPipeName（唯一约定源）

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace {

// 管道名唯一约定源：whalepet::contextapi::kDefaultContextPipeName（LocalPipeTransport.h）
namespace contextapi = whalepet::contextapi;

constexpr int kConnectTimeoutMs = 5000; // 等待主程序管道就绪
constexpr int kIoTimeoutMs = 10000;     // 单次读 / 写等待上限

// stdio 必须二进制模式：否则 Windows 会把 \n 翻译成 \r\n，破坏 Content-Length 分帧
void setBinaryStdio()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

// Content-Length 分帧解析：
//   返回  1 = 成功取出一帧（写入 frame，并从 buf 前部移除）
//         0 = 数据不足，需要继续读
//        -1 = 报文非法（无 Content-Length / 头格式错误），不可恢复
int takeFrame(QByteArray &buf, QByteArray &frame)
{
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return 0;
    }

    int length = -1;
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.toLower().startsWith("content-length:")) {
            bool ok = false;
            length = line.mid(int(std::strlen("content-length:"))).trimmed().toInt(&ok);
            if (!ok) {
                return -1;
            }
        }
    }
    if (length < 0) {
        return -1;
    }

    const int bodyStart = headerEnd + 4;
    if (buf.size() - bodyStart < length) {
        return 0; // 正文未到齐
    }
    frame = buf.mid(bodyStart, length);
    buf.remove(0, bodyStart + length);
    return 1;
}

// 从 stdin 读一段「已到达」的字节。
//
// 注意：**不能用 std::fread 读管道**——MSVCRT/UCRT 的 fread 会一直重试直到读满请求的
// 字节数（或 EOF），而 MCP 客户端是「一问一答」式交互，永远不会有 4096 字节到达，
// 于是会永久阻塞。这里使用 read/_read，其语义是「返回当前可读的字节」。
// 返回：>0 读到的字节数；0 = EOF（客户端关闭 stdin）；<0 = 出错
int readStdinChunk(char *buffer, int capacity)
{
#ifdef _WIN32
    return _read(_fileno(stdin), buffer, static_cast<unsigned>(capacity));
#else
    return static_cast<int>(::read(STDIN_FILENO, buffer, static_cast<size_t>(capacity)));
#endif
}

// 阻塞读取 stdin 的下一帧；EOF / 非法报文返回 false
bool readStdinFrame(QByteArray &buf, QByteArray &frame)
{
    for (;;) {
        const int rc = takeFrame(buf, frame);
        if (rc == 1) {
            return true;
        }
        if (rc < 0) {
            frame.clear(); // 无法恢复：终止桥接，由客户端感知到 stdout 关闭
            return false;
        }
        char chunk[4096];
        const int n = readStdinChunk(chunk, int(sizeof(chunk)));
        if (n <= 0) {
            return false; // MCP 客户端关闭了 stdin（或读失败）
        }
        buf.append(chunk, n);
    }
}

// 按 MCP 约定做 Content-Length 分帧（与主程序侧 StdioTransport 完全同一格式）。
// 注意：**两条边路都必须分帧**——管道侧的解析方是 StdioTransport，它同样只认
// `Content-Length: N\r\n\r\n{...}`；转发裸 JSON 会让对端一直等头部而静默死锁。
QByteArray makeFrame(const QByteArray &payload)
{
    QByteArray out = "Content-Length: ";
    out.append(QByteArray::number(payload.size()));
    out.append("\r\n\r\n");
    out.append(payload);
    return out;
}

void writeStdoutFrame(const QByteArray &payload)
{
    const QByteArray out = makeFrame(payload);
    std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
    std::fflush(stdout);
}

// 阻塞读取管道的下一帧；管道断开返回 false（主程序已退出）
bool readPipeFrame(QLocalSocket &socket, QByteArray &buf, QByteArray &frame)
{
    for (;;) {
        const int rc = takeFrame(buf, frame);
        if (rc == 1) {
            return true;
        }
        if (rc < 0) {
            frame.clear();
            return false;
        }
        if (socket.state() != QLocalSocket::ConnectedState) {
            return false;
        }
        if (!socket.waitForReadyRead(kIoTimeoutMs)) {
            if (socket.state() != QLocalSocket::ConnectedState) {
                return false; // 主程序退出 → 管道断开
            }
            continue; // 只是暂时没有数据，继续等
        }
        buf.append(socket.readAll());
    }
}

// 是否为「请求」（含 id）——通知（无 id）不产生响应
bool frameHasId(const QByteArray &payload)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return true; // 无法判定时保守地等待响应
    }
    return doc.object().contains(QStringLiteral("id"));
}

// --token 非空时，仅把 token 注入 initialize 的 params（§5 的鉴权约定）
QByteArray injectTokenIntoInitialize(const QByteArray &payload, const QString &token)
{
    if (token.isEmpty()) {
        return payload;
    }
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return payload;
    }
    QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("method")).toString() != QStringLiteral("initialize")) {
        return payload;
    }
    QJsonObject params = obj.value(QStringLiteral("params")).toObject();
    params.insert(QStringLiteral("token"), token);
    obj.insert(QStringLiteral("params"), params);
    doc.setObject(obj);
    return doc.toJson(QJsonDocument::Compact);
}

void printUsage()
{
    std::fprintf(stderr,
                 "whalepet-mcp: WhalePet 本地 Context API 的 MCP stdio 桥接进程\n"
                 "用法: whalepet-mcp [--pipe <name>] [--token <token>]\n"
                 "  --pipe   主程序监听的命名管道名（默认 %s）\n"
                 "  --token  非空时注入到 initialize.params.token\n",
                 contextapi::kDefaultContextPipeName);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("whalepet-mcp"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QString pipeName = QString::fromLatin1(contextapi::kDefaultContextPipeName);
    QString token;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--pipe") && i + 1 < args.size()) {
            pipeName = args.at(++i);
        } else if (arg == QStringLiteral("--token") && i + 1 < args.size()) {
            token = args.at(++i);
        } else if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            printUsage();
            return 0;
        }
    }

    setBinaryStdio();

    QLocalSocket socket;
    socket.connectToServer(pipeName);
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        std::fprintf(stderr,
                     "whalepet-mcp: 无法连接命名管道 '%s'（%s）\n"
                     "请确认 WhalePet 正在运行且已开启「本地 Context API」。\n",
                     pipeName.toUtf8().constData(),
                     socket.errorString().toUtf8().constData());
        return 2;
    }

    QByteArray stdinBuf;
    QByteArray pipeBuf;

    for (;;) {
        QByteArray request;
        if (!readStdinFrame(stdinBuf, request)) {
            break; // 客户端关闭 stdin → 桥接退出
        }

        const QByteArray forwarded = injectTokenIntoInitialize(request, token);
        socket.write(makeFrame(forwarded));
        if (!socket.waitForBytesWritten(kIoTimeoutMs)) {
            break;
        }
        if (!frameHasId(forwarded)) {
            continue; // 通知：无响应
        }

        QByteArray response;
        if (!readPipeFrame(socket, pipeBuf, response)) {
            break; // 主程序退出 → 桥接退出
        }
        writeStdoutFrame(response);
    }

    socket.disconnectFromServer();
    return 0;
}
