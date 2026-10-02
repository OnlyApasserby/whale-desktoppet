#pragma once

// MCP stdio 通道（docs/CONTEXT-API.md §4）：`Content-Length` 分帧的 JSON-RPC。
//
// 关键约束：主程序是 WIN32 GUI 子系统（`qt_add_executable(WhalePet WIN32 …)`），
// 没有可用的 stdin/stdout，**无法**直接充当 MCP stdio Server。因此本类与
// 「是不是控制台」解耦：以两个 QIODevice 构造（读 / 写），
//   * 未来的控制台桥接 exe（`whalepet-mcp.exe`，P7.2）传入 stdin/stdout；
//   * 单测传入内存设备对，即可验证分帧、MCP 方法映射与错误处理。
//
// MCP 方法映射（见 docs/CONTEXT-API.md §3.2）：
//   initialize            → 握手 + token 鉴权（本类直接处理）
//   notifications/initialized → 通知，忽略
//   tools/list            → capabilities.list（整形为 { tools: [...] }）
//   tools/call            → capability.invoke（{ name, arguments }）
//   其它                  → 直通 JsonRpcDispatcher

#include "contextapi/JsonRpcDispatcher.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QIODevice;

namespace whalepet::contextapi {

class StdioTransport : public QObject {
    Q_OBJECT
public:
    explicit StdioTransport(JsonRpcDispatcher *dispatcher, QObject *parent = nullptr);
    ~StdioTransport() override;

    // 绑定读写设备（**不接管所有权**）；绑定后自动连接 readyRead
    void bind(QIODevice *input, QIODevice *output);

    // token 非空时要求 initialize 携带匹配的 token（params.token）
    void setToken(const QString &token) { m_token = token; }

    // 投喂一段已到达的字节（设备路径由 readyRead 自动调用；测试可直接调用）
    void feed(const QByteArray &data);

    // 无输出设备时输出会累积在此（测试路径）
    QByteArray takePendingOutput();

    bool authenticated() const { return m_token.isEmpty() || m_authenticated; }
    qint64 framesIn() const { return m_framesIn; }

signals:
    // 有帧要写出（无输出设备时供外部取走）
    void outputReady(const QByteArray &frame);

private:
    void onReadyRead();
    void handleFrame(const QByteArray &payload);
    void handleInitialize(const QJsonObject &request);
    void handleToolsList(const QJsonObject &request);
    void handleToolsCall(const QJsonObject &request);

    void writeFrame(const QJsonObject &response);
    void writeError(const QJsonValue &id, int code, const QString &message);

    JsonRpcDispatcher *m_dispatcher = nullptr;
    QIODevice *m_input = nullptr;
    QIODevice *m_output = nullptr;
    QByteArray m_buffer;
    QByteArray m_pending;
    QString m_token;
    bool m_authenticated = false;
    qint64 m_framesIn = 0;
};

} // namespace whalepet::contextapi
