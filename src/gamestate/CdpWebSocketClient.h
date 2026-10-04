#pragma once

// 极简 Chrome DevTools Protocol（CDP）客户端：仅用于 RPG Maker MV/MZ 的**只读求值**。
// 归属：docs/ROADMAP-ex1.md §2.6.2（方案 A）。
// 【只读】只发送 `Runtime.evaluate`（returnByValue=true），绝不发送任何会改动游戏状态的域
//         （Input / Runtime.callFunctionOn / Page.navigate / Heapprofiler … 一律禁用）。
// 【友好】本地回环；权限失败或未开调试端口时以 bool + error 优雅降级，不崩溃。
//
// ── 安全与资源边界（SECURITY-REVIEW.md §极端边界测试建议 4）───────────────────────
//  * `/json` 的响应内容是**不可信输入**：`webSocketDebuggerUrl` 必须校验为
//    `ws`/`wss` + **回环主机** + 无 userinfo + 合法端口，否则一律拒绝连接
//    （否则一个伪造的调试目标能把只读求值通道指向任意外部主机）。
//  * `/json` 响应与单条 WebSocket 消息都有**大小上限**，超限即中止。
//  * 超时 / 断连 / 重试后不留下活动连接或悬挂请求（pendingId 与事件循环指针均复位）。

#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

namespace whalepet::gamestate {

class CdpWebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit CdpWebSocketClient(QObject *parent = nullptr);
    ~CdpWebSocketClient() override;

    // 资源上限（public 便于单测核验契约）
    static constexpr int kMaxDiscoveryBytes = 1024 * 1024;     // `/json` 响应上限（1 MiB）
    static constexpr int kMaxMessageBytes = 4 * 1024 * 1024;    // 单条 WebSocket 消息上限（4 MiB）

    // 调试端点白名单校验：`ws`/`wss` + 回环主机 + 无 userinfo + 端口 1…65535。
    // 供 `discoverWebSocketUrl` 与 `connectToUrl` 共用，也可单测直接核验。
    static bool isTrustedDebuggerUrl(const QUrl &url, QString *error);

    // 经 `http://127.0.0.1:<port>/json` 发现首个 page 目标的 webSocketDebuggerUrl。
    // 失败返回 false 并填 *error（调用方可据此提示用户「是否加 --remote-debugging-port」）。
    static bool discoverWebSocketUrl(quint16 port, QString *url, QString *error,
                                     int timeoutMs = 3000);

    // 直连一个 webSocketDebuggerUrl（如 ws://127.0.0.1:9222/devtools/page/<id>）。
    // 非可信端点（远程主机 / 非 ws 协议 / userinfo / 非法端口）直接拒绝，不发起连接。
    bool connectToUrl(const QString &url, QString *error, int timeoutMs = 3000);
    bool connected() const;
    void close();

    // 只读求值：`Runtime.evaluate(expression, returnByValue=true)`，取回 JSON 值。
    bool evaluate(const QString &expression, QJsonValue *value, QString *error,
                  int timeoutMs = 3000);

    // 引擎不再响应时置位（供适配器做失效处理）。
    QString lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace whalepet::gamestate
