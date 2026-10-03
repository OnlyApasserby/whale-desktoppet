#pragma once

// 极简 Chrome DevTools Protocol（CDP）客户端：仅用于 RPG Maker MV/MZ 的**只读求值**。
// 归属：docs/ROADMAP-ex1.md §2.6.2（方案 A）。
// 【只读】只发送 `Runtime.evaluate`（returnByValue=true），绝不发送任何会改动游戏状态的域
//         （Input / Runtime.callFunctionOn / Page.navigate / Heapprofiler … 一律禁用）。
// 【友好】本地回环；权限失败或未开调试端口时以 bool + error 优雅降级，不崩溃。

#include <QJsonValue>
#include <QObject>
#include <QString>

#include <memory>

namespace whalepet::gamestate {

class CdpWebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit CdpWebSocketClient(QObject *parent = nullptr);
    ~CdpWebSocketClient() override;

    // 经 `http://127.0.0.1:<port>/json` 发现首个 page 目标的 webSocketDebuggerUrl。
    // 失败返回 false 并填 *error（调用方可据此提示用户「是否加 --remote-debugging-port」）。
    static bool discoverWebSocketUrl(quint16 port, QString *url, QString *error,
                                     int timeoutMs = 3000);

    // 直连一个 webSocketDebuggerUrl（如 ws://127.0.0.1:9222/devtools/page/<id>）。
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
