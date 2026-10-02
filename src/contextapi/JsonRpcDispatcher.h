#pragma once

// JSON-RPC 2.0 分发核心（docs/CONTEXT-API.md §3）：**唯一**分发点，双通道共用。
//
// 方法表刻意保持极小——因为「一切皆能力」：
//   * `ping`              ：存活探测；
//   * `capabilities.list` ：列出三层插件注册的全部能力（含可用性）；
//   * `capability.invoke` ：统一能力调用（params = { id, params }）；
//   * `<能力 id>`         ：**别名**——方法名与某个能力 id 相同时，等价于
//                           `capability.invoke` + `{ id: 方法名, params }`。
//                           上下文能力（context.* / pet.status / session.stats）
//                           即以此方式暴露，因此**新增能力无需改分发核心**。
//
// 异步：能力若返回 false 并取走 InvokeContext 的 responder（外部进程插件用），
// 则响应在其完成时经同一 responder 回投——分发核心不阻塞 GUI 线程。

#include "plugin/Capability.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace whalepet::contextapi {

class JsonRpcDispatcher {
public:
    using Responder = std::function<void(const QJsonObject &response)>;
    // 错误响应回调：lambda **不能带默认参数**，故错误路径统一显式传 3 个实参
    using ErrorResponder =
        std::function<void(int code, const QString &message, const QJsonObject &data)>;

    explicit JsonRpcDispatcher(plugin::CapabilityRegistry *capabilities);
    ~JsonRpcDispatcher() = default;

    JsonRpcDispatcher(const JsonRpcDispatcher &) = delete;
    JsonRpcDispatcher &operator=(const JsonRpcDispatcher &) = delete;

    // 处理一个请求对象。
    //   * 带 id（请求）：`respond` 恰好被调用一次（同步立即 / 异步完成时）；
    //   * 无 id（通知）：执行但不调用 respond；
    //   * 请求非法（非对象 / 缺 method / params 非对象）：立即以错误响应。
    void handle(const QJsonObject &request, const Responder &respond);

    // 便捷同步封装（本地调试与单测）：异步能力在同步路径下无法等待，返回内部错误
    QJsonObject handleSync(const QJsonObject &request);

    // 内置方法名（不含能力别名），供测试与文档核对
    static QStringList builtinMethods();

private:
    // 把一名能力转发为 JSON-RPC 响应（同步或异步）
    void invokeCapability(const QString &capabilityId, const QJsonObject &params,
                          const Responder &replyResult, const ErrorResponder &replyError);

    plugin::CapabilityRegistry *m_capabilities = nullptr;
};

// 能力描述符 → JSON（`capabilities.list` 与 MCP `tools/list` 共用同一形状）
QJsonObject capabilityDescriptorToJson(const plugin::CapabilityDescriptor &descriptor,
                                       bool available);

} // namespace whalepet::contextapi
