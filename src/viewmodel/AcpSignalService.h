#pragma once

// ACP 显式信号编排（docs/CONTEXT-API.md §6、docs/ROADMAP-P7.md P7.5）：
//   定时轮询 contextapi::ISignalSource → 按 AcpSignalRules 映射 → 广播**覆盖性**工作态。
//
// 职责边界：只做「轮询 + 映射 + 广播」，不碰界面；是否采用由 WorkStateService 的覆盖窗口决定。
// 与 platform 推断的关系：本服务产出的是**显式告知**，优先级高于推断。
//
// source 可空：为空 / 不可用时不产生任何信号（不伪造），定时器照常运行（等文件出现）。

#include "contextapi/ISignalSource.h"
#include "contextapi/acp/AcpSignalRules.h"
#include "core/WorkState.h"

#include <QObject>

class QTimer;

namespace whalepet::viewmodel {

class AcpSignalService : public QObject {
    Q_OBJECT
public:
    explicit AcpSignalService(QObject *parent = nullptr);
    ~AcpSignalService() override;

    // 注入信号源（**不接管所有权**）
    void setSource(contextapi::ISignalSource *source);
    contextapi::ISignalSource *source() const { return m_source; }

    void setIntervalMs(int intervalMs);
    int intervalMs() const;

    void start();
    void stop();
    bool running() const;

    // 立即轮询一轮（测试 / 开启后立刻生效）；返回本轮**映射成功**的信号数
    int pollNow();

    // 直接投喂一条信号（由 AcpClient 的 ACP 事件驱动）——
    // 与轮询来源走**同一条**映射与广播路径，因此下游（覆盖窗口）行为完全一致。
    void submitSignal(const contextapi::CoreSignal &signal);

    qint64 signalCount() const { return m_signalCount; }
    qint64 overrideCount() const { return m_overrideCount; }

signals:
    // 收到一条（已识别的）显式信号
    void signalReceived(const contextapi::CoreSignal &signal);
    // 需要把工作态作为覆盖性输入交给 WorkStateService::applyExternalState
    void workStateOverride(whalepet::core::WorkState state, double confidence, qint64 atMs,
                           qint64 holdMs);

private:
    void onTimeout();

    contextapi::ISignalSource *m_source = nullptr;
    QTimer *m_timer = nullptr;
    qint64 m_signalCount = 0;
    qint64 m_overrideCount = 0;
};

} // namespace whalepet::viewmodel
