#include "contextapi/transport/StdioTransport.h"

#include <QDebug>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace whalepet::contextapi {

namespace {

const char *const kHeaderSeparator = "\r\n\r\n";
const char *const kContentLength = "content-length:";

const char *const kFieldJsonRpc = "jsonrpc";
const char *const kFieldId = "id";
const char *const kFieldMethod = "method";
const char *const kFieldParams = "params";
const char *const kFieldResult = "result";
const char *const kFieldError = "error";
const char *const kFieldTools = "tools";
const char *const kVersion = "2.0";

QJsonObject makeResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QLatin1String(kFieldJsonRpc), QLatin1String(kVersion));
    response.insert(QLatin1String(kFieldId), id);
    response.insert(QLatin1String(kFieldResult), result);
    return response;
}

} // namespace

StdioTransport::StdioTransport(JsonRpcDispatcher *dispatcher, QObject *parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
{
}

StdioTransport::~StdioTransport() = default;

void StdioTransport::bind(QIODevice *input, QIODevice *output)
{
    if (m_input != nullptr) {
        disconnect(m_input, nullptr, this, nullptr);
    }
    m_input = input;
    m_output = output;
    if (m_input != nullptr) {
        connect(m_input, &QIODevice::readyRead, this, &StdioTransport::onReadyRead);
        if (m_input->bytesAvailable() > 0) {
            onReadyRead(); // 绑定前已有数据：立即处理，避免依赖下一次 readyRead
        }
    }
}

void StdioTransport::onReadyRead()
{
    if (m_input == nullptr) {
        return;
    }
    feed(m_input->readAll());
}

QByteArray StdioTransport::takePendingOutput()
{
    const QByteArray out = m_pending;
    m_pending.clear();
    return out;
}

void StdioTransport::feed(const QByteArray &data)
{
    m_buffer.append(data);

    while (true) {
        const int headerEnd = m_buffer.indexOf(kHeaderSeparator);
        if (headerEnd < 0) {
            return; // 头部尚未收全
        }

        int contentLength = -1;
        const QByteArray header = m_buffer.left(headerEnd);
        const QList<QByteArray> lines = header.split('\n');
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith(kContentLength)) {
                bool ok = false;
                const int value =
                    trimmed.mid(static_cast<int>(qstrlen(kContentLength))).trimmed().toInt(&ok);
                if (ok) {
                    contentLength = value;
                }
            }
        }
        if (contentLength < 0) {
            // 丢弃这一帧头，避免死循环；明确报协议错误（不静默）
            m_buffer.remove(0, headerEnd + static_cast<int>(qstrlen(kHeaderSeparator)));
            writeError(QJsonValue(QJsonValue::Null), plugin::kRpcErrorParseError,
                       QStringLiteral("缺少或非法的 Content-Length"));
            continue;
        }

        const int bodyStart = headerEnd + static_cast<int>(qstrlen(kHeaderSeparator));
        if (m_buffer.size() - bodyStart < contentLength) {
            return; // 正文尚未收全
        }
        const QByteArray payload = m_buffer.mid(bodyStart, contentLength);
        m_buffer.remove(0, bodyStart + contentLength);
        ++m_framesIn;
        handleFrame(payload);
    }
}

void StdioTransport::handleFrame(const QByteArray &payload)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        writeError(QJsonValue(QJsonValue::Null), plugin::kRpcErrorParseError,
                   QStringLiteral("请求体不是合法 JSON 对象"));
        return;
    }
    const QJsonObject request = doc.object();
    const QString method = request.value(QLatin1String(kFieldMethod)).toString();

    // ---- MCP 协议层方法（由本类处理，不下发到能力表）----
    if (method == QLatin1String("initialize")) {
        handleInitialize(request);
        return;
    }
    if (method == QLatin1String("notifications/initialized")) {
        return; // 通知：不响应
    }

    // ---- 鉴权门控 ----
    if (!authenticated()) {
        writeError(request.value(QLatin1String(kFieldId)), plugin::kRpcErrorUnauthorized,
                   QStringLiteral("未授权：请先完成 initialize 并携带正确 token"));
        return;
    }

    if (method == QLatin1String("tools/list")) {
        handleToolsList(request);
        return;
    }
    if (method == QLatin1String("tools/call")) {
        handleToolsCall(request);
        return;
    }

    if (m_dispatcher == nullptr) {
        writeError(request.value(QLatin1String(kFieldId)), plugin::kRpcErrorInternal,
                   QStringLiteral("分发核心不可用"));
        return;
    }
    m_dispatcher->handle(request, [this](const QJsonObject &response) { writeFrame(response); });
}

void StdioTransport::handleInitialize(const QJsonObject &request)
{
    const QJsonValue id = request.value(QLatin1String(kFieldId));
    const QJsonObject params = request.value(QLatin1String(kFieldParams)).toObject();

    if (!m_token.isEmpty()) {
        const QString provided = params.value(QStringLiteral("token")).toString();
        if (provided != m_token) {
            m_authenticated = false;
            writeError(id, plugin::kRpcErrorUnauthorized, QStringLiteral("token 不匹配"));
            return;
        }
    }
    m_authenticated = true;

    QJsonObject serverInfo;
    serverInfo.insert(QStringLiteral("name"), QStringLiteral("whalepet"));
    serverInfo.insert(QStringLiteral("version"), QStringLiteral("0.2.0"));

    QJsonObject tools;
    tools.insert(QStringLiteral("listChanged"), false);

    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("tools"), tools);

    QJsonObject result;
    result.insert(QStringLiteral("protocolVersion"),
                  params.value(QStringLiteral("protocolVersion")).toString(QStringLiteral("1.0")));
    result.insert(QStringLiteral("serverInfo"), serverInfo);
    result.insert(QStringLiteral("capabilities"), capabilities);
    writeFrame(makeResponse(id, result));
}

void StdioTransport::handleToolsList(const QJsonObject &request)
{
    const QJsonValue id = request.value(QLatin1String(kFieldId));
    if (m_dispatcher == nullptr) {
        writeError(id, plugin::kRpcErrorInternal, QStringLiteral("分发核心不可用"));
        return;
    }

    // 复用 capabilities.list（同一形状），再整形为 MCP tools 列表
    QJsonObject listRequest;
    listRequest.insert(QLatin1String(kFieldJsonRpc), QLatin1String(kVersion));
    listRequest.insert(QLatin1String(kFieldId), 1);
    listRequest.insert(QLatin1String(kFieldMethod), QStringLiteral("capabilities.list"));
    const QJsonObject listResponse = m_dispatcher->handleSync(listRequest);
    const QJsonObject listResult = listResponse.value(QLatin1String(kFieldResult)).toObject();

    QJsonArray tools;
    const QJsonArray capabilities = listResult.value(QStringLiteral("capabilities")).toArray();
    for (const QJsonValue &value : capabilities) {
        const QJsonObject descriptor = value.toObject();
        QJsonObject tool;
        tool.insert(QStringLiteral("name"), descriptor.value(QStringLiteral("id")));
        tool.insert(QStringLiteral("description"), descriptor.value(QStringLiteral("description")));
        tool.insert(QStringLiteral("inputSchema"), descriptor.value(QStringLiteral("inputSchema")));
        if (!descriptor.value(QStringLiteral("available")).toBool(true)) {
            // 不可用能力不隐藏，但标注出来（客户端可自行决定是否调用）
            tool.insert(QStringLiteral("x-whalepet-unavailable"), true);
        }
        tools.append(tool);
    }

    QJsonObject result;
    result.insert(QLatin1String(kFieldTools), tools);
    writeFrame(makeResponse(id, result));
}

void StdioTransport::handleToolsCall(const QJsonObject &request)
{
    const QJsonValue id = request.value(QLatin1String(kFieldId));
    if (m_dispatcher == nullptr) {
        writeError(id, plugin::kRpcErrorInternal, QStringLiteral("分发核心不可用"));
        return;
    }
    const QJsonObject params = request.value(QLatin1String(kFieldParams)).toObject();
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        writeError(id, plugin::kRpcErrorInvalidParams, QStringLiteral("缺少 tool name"));
        return;
    }

    // tools/call { name, arguments } → capability.invoke { id, params }
    QJsonObject invokeParams;
    invokeParams.insert(QStringLiteral("id"), name);
    invokeParams.insert(QStringLiteral("params"),
                        params.value(QStringLiteral("arguments")).toObject());
    QJsonObject forwarded = request;
    forwarded.insert(QLatin1String(kFieldMethod), QStringLiteral("capability.invoke"));
    forwarded.insert(QLatin1String(kFieldParams), invokeParams);

    m_dispatcher->handle(forwarded, [this](const QJsonObject &response) { writeFrame(response); });
}

void StdioTransport::writeFrame(const QJsonObject &response)
{
    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.append("Content-Length: ");
    frame.append(QByteArray::number(payload.size()));
    frame.append(kHeaderSeparator);
    frame.append(payload);

    if (m_output != nullptr) {
        m_output->write(frame);
        if (auto *file = qobject_cast<QFileDevice *>(m_output)) {
            file->flush();
        }
        return;
    }
    m_pending.append(frame);
    emit outputReady(frame);
}

void StdioTransport::writeError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject response;
    response.insert(QLatin1String(kFieldJsonRpc), QLatin1String(kVersion));
    response.insert(QLatin1String(kFieldId), id);
    response.insert(QLatin1String(kFieldError), plugin::makeRpcError(code, message));
    writeFrame(response);
}

} // namespace whalepet::contextapi
