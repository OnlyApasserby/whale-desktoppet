#pragma once

// 本地命名管道通道（P7.2，见 docs/CONTEXT-API.md §4）：
//   Context API 的**第二本地通道**，供 whalepet-mcp.exe（控制台桥接进程）连接。
//
// 设计要点：
//   * 用 QLocalServer 监听一个固定名字的命名管道（Windows 下即 \\.\pipe\<name>）；
//   * 每条连接**直接复用 StdioTransport**——它本就以「两个 QIODevice」构造，
//     因此分帧（Content-Length）、MCP 方法映射（initialize / tools/list / tools/call）
//     与 token 门控（initialize.params.token）与 stdio 通道**完全一致**，
//     不需要第二套协议实现，保证「两通道共用同一 dispatcher 与能力表」。
//   * 桥接进程（whalepet-mcp.exe）只做 stdin/stdout ↔ 管道的字节转发，不含业务逻辑。
//
// 访问控制：命名管道仅本机可见（LocalSocket 不跨主机）；token 非空时由
// StdioTransport 在 initialize 阶段校验，未通过前一切请求均被拒绝。

#include "contextapi/JsonRpcDispatcher.h"
#include "contextapi/transport/StdioTransport.h"

#include <QHash>
#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace whalepet::contextapi {

// 命名管道默认名：**主程序与 whalepet-mcp.exe 两侧共用的唯一约定**。
// 修改此处必须同步 docs/packages.md §2.1「本地通道命名约定」与桥接进程默认值。
inline constexpr const char *kDefaultContextPipeName = "whalepet-context-v1";

// 本地命名管道监听端（主程序侧）。
class LocalPipeTransport : public QObject {
    Q_OBJECT
public:
    explicit LocalPipeTransport(JsonRpcDispatcher *dispatcher, QObject *parent = nullptr);
    ~LocalPipeTransport() override;

    void setServerName(const QString &name) { m_serverName = name; }
    QString serverName() const { return m_serverName; }

    // token 非空时要求 initialize 携带匹配的 params.token（与 stdio / HTTP 同源）
    void setToken(const QString &token) { m_token = token; }

    // 开始监听；失败返回 false 并可用 errorString() 取原因
    bool start();
    // 停止监听并断开所有桥接进程
    void stop();

    bool isListening() const;
    QString errorString() const { return m_error; }
    // 当前已连接的桥接进程数（供诊断与单测）
    int connectionCount() const { return m_clients.size(); }

private:
    void onNewConnection();
    void removeClient(QLocalSocket *socket);

    JsonRpcDispatcher *m_dispatcher = nullptr;
    QLocalServer *m_server = nullptr;
    QString m_serverName = QString::fromLatin1(kDefaultContextPipeName);
    QString m_token;
    QString m_error;
    QHash<QLocalSocket *, StdioTransport *> m_clients;
};

} // namespace whalepet::contextapi
