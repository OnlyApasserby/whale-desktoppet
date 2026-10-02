#pragma once

// 外部显式信号源（ACP / IDE / 文件事件）——**P7.5 预留，仅接口，不实现协议**
// （docs/CONTEXT-API.md §6）。
//
// 定位：platform 层是「**推断**」，本接口是「**显式告知**」。
//   * 显式信号优先于推断（例如 IDE 扩展直接上报「正在与 Agent 快速迭代」→ vibe-coding），
//     因此 WorkState 的判定链在 P7.5 会把 CoreSignal 作为**覆盖性输入**接在推断之前；
//   * 本期只固定「谁在什么时候告诉我什么」，不固定传输与协议细节，
//     避免在 ACP 协议仍在演进时过度设计。

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace whalepet::contextapi {

// 一条外部显式信号（协议无关载荷）
struct CoreSignal {
    QString sourceId;        // "vscode" / "acp" / "git" …
    QString kind;            // "file.saved" / "agent.turn" / "edit.burst" …
    QJsonObject payload;     // 信号自定义载荷
    qint64 atMs = 0;         // 信号发生时刻（墙钟毫秒）
};

class ISignalSource {
public:
    virtual ~ISignalSource() = default;

    virtual QString id() const = 0;
    virtual bool available() const = 0;

    // 取一条待处理信号；返回 false 表示当前无信号。
    // **不得**为了「看起来有数据」而产出假信号——宁可没有。
    virtual bool poll(qint64 nowMs, CoreSignal &out) = 0;
};

} // namespace whalepet::contextapi
