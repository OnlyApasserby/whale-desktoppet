#pragma once

// Context API 装配与门控（docs/CONTEXT-API.md）。
//
// 职责：
//   * 组装「能力注册表 → dispatcher → 通道」；
//   * 门控：**默认关闭**——关闭时不注册上下文能力、不启动任何监听，
//     因此 `capabilities.list` 里也不会出现 context.*（隐私优先，见 §5）；
//   * 访问控制：仅本机回环 + 可选 token。
//
// 依赖方向：本类只依赖 plugin（能力总线）与自身接口 IContextProvider，
// 不依赖 view 层，因此可独立单测（注入假 provider）。

#include "contextapi/IContextProvider.h"
#include "plugin/PluginRegistry.h"

#include <QObject>
#include <QString>

#include <memory>

namespace whalepet::contextapi {

class JsonRpcDispatcher;
class LocalHttpTransport;
class LocalPipeTransport;

class ContextApiService : public QObject {
    Q_OBJECT
public:
    ContextApiService(plugin::PluginRegistry *registry, IContextProvider *provider,
                      QObject *parent = nullptr);
    ~ContextApiService() override;

    // 注册内置上下文能力（幂等）。返回注册的插件数（0 = 已注册过；-1 = 注册表不可用）。
    int registerBuiltinCapabilities();

    void setHttpPort(quint16 port) { m_port = port; }
    void setToken(const QString &token) { m_token = token; }
    // P7.2：命名管道名（默认 kDefaultContextPipeName，主程序与 whalepet-mcp.exe 共用）
    void setPipeName(const QString &name);

    // 启动通道（会先确保上下文能力已注册）。任一通道失败返回 false 并置 errorString()。
    // P7.2 起，总开关**同时**控制本机 HTTP 与命名管道：要么两通道都监听，要么都不监听。
    bool start();
    void stop();

    bool running() const;
    quint16 httpPort() const;  // 实际监听端口；未监听返回 0
    bool pipeListening() const; // P7.2：命名管道是否在监听
    QString pipeName() const;   // P7.2：当前命名管道名（诊断用）
    QString errorString() const { return m_error; }

    // 不经网络的请求入口：MCP stdio 桥接进程（P7.2）与单测使用；
    // 语义与本地 HTTP 通道完全一致（共用同一 dispatcher 与能力注册表）。
    QJsonObject handleRequest(const QJsonObject &request);

    plugin::CapabilityRegistry *capabilities() const;

signals:
    void started(quint16 port);
    void stopped();

private:
    void ensureDispatcher();
    // 运行期门控：通道关闭时把已注册的上下文能力标记为不可用
    // （能力无法从注册表移除，故用可用性开关如实表达「当前不可用」）
    void markCapabilitiesAvailable(bool available);

    plugin::PluginRegistry *m_registry = nullptr;
    IContextProvider *m_provider = nullptr;
    std::unique_ptr<JsonRpcDispatcher> m_dispatcher;
    std::unique_ptr<LocalHttpTransport> m_http;
    std::unique_ptr<LocalPipeTransport> m_pipe; // P7.2：命名管道（供 whalepet-mcp.exe 连接）

    QString m_token;
    QString m_error;
    QString m_pipeName; // P7.2：命名管道名（默认值在构造处取自 kDefaultContextPipeName）
    quint16 m_port = 0;
    bool m_builtinRegistered = false;
};

} // namespace whalepet::contextapi
