#pragma once

// ACP 显式信号源的具体实现（docs/CONTEXT-API.md §6、docs/ROADMAP-P7.md P7.5）。
//
// 把「IDE 扩展 / Agent 会话 / 文件保存与 diff 事件」这类**外部显式告知**落到一个
// 本地 **JSONL 信号文件**：外部进程（IDE 扩展 / 脚本）向文件追加一行一个 JSON 对象，
// 桌宠按采样周期轮询读取——本类因此**不引入任何新传输依赖**，也天然与主进程解耦
// （IDE 崩溃不影响桌宠；桌宠未开启时信号只是堆在文件里）。
//
// 行格式（每行一个对象）：
//   {"sourceId":"vscode","kind":"agent.turn","payload":{...},"atMs":1699999999999}
//   * kind 必填（缺失的行被忽略并告警，**不产生假信号**）；
//   * sourceId 可选（缺省用构造时注入的 id）；
//   * payload / atMs 可选。
//
// 与「推断」的关系：platform 层是推断，本类承载的是显式告知；显式信号优先于推断
// （由 viewmodel::WorkStateService 的覆盖窗口落地，见 AcpSignalRules.h）。

#include "contextapi/ISignalSource.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace whalepet::contextapi {

class AcpSignalSource : public ISignalSource {
public:
    // sourceId：本源的稳定标识（如 "acp" / "vscode"）；filePath：JSONL 信号文件路径。
    // filePath 为空或文件不存在时 available() 为 false，poll() 恒返回 false（不伪造）。
    AcpSignalSource(QString sourceId, QString filePath);

    QString id() const override;
    bool available() const override;

    // 取一条待处理信号。先清空内部已解析队列，再尝试从文件增量读取新行。
    // 无新行 / 文件不可读 → false（**绝不**为「看起来有数据」而产出假信号）。
    bool poll(qint64 nowMs, CoreSignal &out) override;

    // 运行期更换信号文件（重置读取进度与已解析队列）；空路径 = 变为不可用
    void setFilePath(const QString &filePath);

    // 已成功产出的信号条数（诊断 / 测试）
    qint64 consumedCount() const { return m_consumed; }
    const QString &filePath() const { return m_filePath; }
    // 因非法 JSON / 缺 kind 而被忽略的行数（诊断；不静默丢弃）
    qint64 ignoredLineCount() const { return m_ignored; }

private:
    // 从 m_offset 增量读取文件，按行解析并追加到 m_queue
    void ingest();
    bool parseLine(const QByteArray &line, CoreSignal &out);

    QString m_sourceId;
    QString m_filePath;
    QList<CoreSignal> m_queue; // 已解析待取走
    QByteArray m_pending;      // 尚未收到换行的尾部字节
    qint64 m_offset = 0;       // 已消费的字节偏移（文件被截断时重置为 0）
    qint64 m_consumed = 0;
    qint64 m_ignored = 0;
};

} // namespace whalepet::contextapi
