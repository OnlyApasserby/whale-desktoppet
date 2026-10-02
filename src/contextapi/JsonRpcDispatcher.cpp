#include "contextapi/JsonRpcDispatcher.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <memory>
#include <utility>

namespace whalepet::contextapi {

namespace {

const char *const kFieldJsonRpc = "jsonrpc";
const char *const kFieldId = "id";
const char *const kFieldMethod = "method";
const char *const kFieldParams = "params";
const char *const kFieldResult = "result";
const char *const kFieldError = "error";
const char *const kFieldOk = "ok";
const char *const kFieldCapabilities = "capabilities";
const char *const kVersion = "2.0";

QJsonObject emptySchema()
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), QJsonObject());
    return schema;
}

} // namespace

QStringList JsonRpcDispatcher::builtinMethods()
{
    return { QStringLiteral("ping"), QStringLiteral("capabilities.list"),
             QStringLiteral("capability.invoke") };
}

QJsonObject capabilityDescriptorToJson(const plugin::CapabilityDescriptor &descriptor,
                                       bool available)
{
    QJsonObject out;
    out.insert(QStringLiteral("id"), descriptor.id);
    out.insert(QStringLiteral("version"), descriptor.version);
    out.insert(QStringLiteral("displayName"), descriptor.displayName);
    out.insert(QStringLiteral("description"), descriptor.description);
    out.insert(QStringLiteral("origin"), QString::fromLatin1(plugin::pluginOriginId(descriptor.origin)));
    out.insert(QStringLiteral("readOnly"), descriptor.readOnly);
    out.insert(QStringLiteral("available"), available);

    // paramsSchema 是轻量 JSON 字符串：能解析成对象就内联为 inputSchema（MCP tools/list 形状），
    // 解析失败则原样保留（不静默丢信息）
    if (descriptor.paramsSchema.isEmpty()) {
        out.insert(QStringLiteral("inputSchema"), emptySchema());
    } else {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(descriptor.paramsSchema.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            out.insert(QStringLiteral("inputSchema"), doc.object());
        } else {
            out.insert(QStringLiteral("inputSchema"), descriptor.paramsSchema);
        }
    }
    return out;
}

JsonRpcDispatcher::JsonRpcDispatcher(plugin::CapabilityRegistry *capabilities)
    : m_capabilities(capabilities)
{
}

void JsonRpcDispatcher::invokeCapability(const QString &capabilityId, const QJsonObject &params,
                                        const Responder &replyResult,
                                        const ErrorResponder &replyError)
{
    if (m_capabilities == nullptr) {
        replyError(plugin::kRpcErrorInternal, QStringLiteral("能力注册表不可用"), QJsonObject());
        return;
    }

    // responder 只捕获**按值**的回调：异步能力可能在本函数返回后才作答
    plugin::InvokeContext ctx([replyResult, replyError](const QJsonObject &response) {
        if (response.value(QLatin1String(kFieldOk)).toBool(true)) {
            replyResult(response.value(QLatin1String(kFieldResult)).toObject());
            return;
        }
        const QJsonObject error = response.value(QLatin1String(kFieldError)).toObject();
        replyError(plugin::rpcErrorCode(error), plugin::rpcErrorMessage(error),
                   plugin::rpcErrorData(error));
    });

    QJsonObject out;
    QJsonObject error;
    const bool done = m_capabilities->invoke(capabilityId, params, ctx, out, error);
    if (done) {
        if (error.isEmpty()) {
            replyResult(out);
        } else {
            replyError(plugin::rpcErrorCode(error), plugin::rpcErrorMessage(error),
                       plugin::rpcErrorData(error));
        }
        return;
    }
    if (ctx.answered()) {
        return; // 能力在同步阶段已自行作答
    }
    if (ctx.hasResponder()) {
        // 既未作答、也未取走回调 —— 契约违反，明确报错而不是永久挂起
        replyError(plugin::kRpcErrorInternal,
                   QStringLiteral("能力为异步实现但未取走回调（违反 InvokeContext 契约）"),
                   QJsonObject());
        return;
    }
    // 回调已被能力取走：异步完成后由其触发响应（分发核心不阻塞 GUI 线程）
}

void JsonRpcDispatcher::handle(const QJsonObject &request, const Responder &respond)
{
    const bool notification = !request.contains(QLatin1String(kFieldId));
    const QJsonValue id = request.value(QLatin1String(kFieldId));
    const Responder reply = respond ? respond : Responder();

    // 所有回调都只捕获按值副本：异步能力持有它们时不会引用已销毁的栈对象
    const auto replyResult = [id, notification, reply](const QJsonObject &result) {
        if (notification || !reply) {
            return;
        }
        QJsonObject response;
        response.insert(QLatin1String(kFieldJsonRpc), QLatin1String(kVersion));
        response.insert(QLatin1String(kFieldId), id);
        response.insert(QLatin1String(kFieldResult), result);
        reply(response);
    };
    const ErrorResponder replyError = [id, notification, reply](int code, const QString &message,
                                                               const QJsonObject &data) {
        if (notification || !reply) {
            return;
        }
        QJsonObject response;
        response.insert(QLatin1String(kFieldJsonRpc), QLatin1String(kVersion));
        response.insert(QLatin1String(kFieldId), id);
        response.insert(QLatin1String(kFieldError), plugin::makeRpcError(code, message, data));
        reply(response);
    };

    // ---- 请求校验 ----
    const QJsonValue jsonrpc = request.value(QLatin1String(kFieldJsonRpc));
    if (!jsonrpc.isUndefined() && jsonrpc.toString() != QLatin1String(kVersion)) {
        replyError(plugin::kRpcErrorInvalidRequest, QStringLiteral("仅支持 JSON-RPC 2.0"),
                   QJsonObject());
        return;
    }
    const QJsonValue methodValue = request.value(QLatin1String(kFieldMethod));
    if (!methodValue.isString() || methodValue.toString().isEmpty()) {
        replyError(plugin::kRpcErrorInvalidRequest, QStringLiteral("缺少 method"), QJsonObject());
        return;
    }
    const QJsonValue paramsValue = request.value(QLatin1String(kFieldParams));
    if (!paramsValue.isUndefined() && !paramsValue.isNull() && !paramsValue.isObject()) {
        replyError(plugin::kRpcErrorInvalidParams, QStringLiteral("params 必须是对象"),
                   QJsonObject());
        return;
    }
    const QString method = methodValue.toString();
    const QJsonObject params = paramsValue.toObject();

    // ---- 内置方法 ----
    if (method == QLatin1String("ping")) {
        QJsonObject out;
        out.insert(QStringLiteral("pong"), true);
        out.insert(QStringLiteral("nowMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        replyResult(out);
        return;
    }

    if (method == QLatin1String("capabilities.list")) {
        QJsonArray list;
        if (m_capabilities != nullptr) {
            for (const plugin::CapabilityDescriptor &descriptor : m_capabilities->descriptors()) {
                list.append(capabilityDescriptorToJson(descriptor,
                                                       m_capabilities->isAvailable(descriptor.id)));
            }
        }
        QJsonObject out;
        out.insert(QLatin1String(kFieldCapabilities), list);
        replyResult(out);
        return;
    }

    if (method == QLatin1String("capability.invoke")) {
        const QString capabilityId = params.value(QStringLiteral("id")).toString();
        if (capabilityId.isEmpty()) {
            replyError(plugin::kRpcErrorInvalidParams, QStringLiteral("缺少能力 id"), QJsonObject());
            return;
        }
        const QJsonValue capabilityParams = params.value(QStringLiteral("params"));
        if (!capabilityParams.isUndefined() && !capabilityParams.isNull()
            && !capabilityParams.isObject()) {
            replyError(plugin::kRpcErrorInvalidParams,
                       QStringLiteral("params.params 必须是对象"), QJsonObject());
            return;
        }
        invokeCapability(capabilityId, capabilityParams.toObject(), replyResult, replyError);
        return;
    }

    // ---- 能力别名：方法名 == 能力 id ⇒ 等价于 capability.invoke ----
    // 上下文能力（context.snapshot / context.environment / context.workState /
    // pet.status / session.stats）即以此方式暴露，因此新增能力无需改本文件。
    if (m_capabilities != nullptr && m_capabilities->contains(method)) {
        invokeCapability(method, params, replyResult, replyError);
        return;
    }

    replyError(plugin::kRpcErrorMethodNotFound, QStringLiteral("未知方法：%1").arg(method),
               QJsonObject());
}

QJsonObject JsonRpcDispatcher::handleSync(const QJsonObject &request)
{
    // 用共享指针承载结果：异步能力可能在本函数返回后才回投回调，
    // 按引用捕获会造成悬垂，故让回调持有共享状态而不是栈对象引用。
    auto box = std::make_shared<std::pair<QJsonObject, bool>>(QJsonObject(), false);
    handle(request, [box](const QJsonObject &response) {
        box->first = response;
        box->second = true;
    });
    if (box->second) {
        return box->first;
    }

    QJsonObject response;
    response.insert(QLatin1String(kFieldJsonRpc), QLatin1String(kVersion));
    response.insert(QLatin1String(kFieldId), request.value(QLatin1String(kFieldId)));
    response.insert(QLatin1String(kFieldError),
                    plugin::makeRpcError(plugin::kRpcErrorInternal,
                                         QStringLiteral("请求未在同步路径内完成"
                                                        "（通知或异步能力；请使用异步通道）")));
    return response;
}

} // namespace whalepet::contextapi
