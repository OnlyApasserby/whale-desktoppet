#pragma once

// FlickerService（EX6 随机闪现）：以**随机间隔**发出一次「闪现」请求
// （桌宠瞬时换一个屏幕位置，无连续位移；docs/PRESENTATION.md §3.2）。
//
// 口径与边界：
//   * 默认**关**（侵入性功能默认关，与 code_egg_enabled 同口径，见 docs/SETTINGS.md §2）；
//   * 单次定时器 + 每轮**重新随机抽取**间隔（与 RecycleBinService 同构，不做周期抖动累积）；
//   * 「该不该动」由宿主经 setCanFlicker() 注入窄回调判定（拖拽中 / 面板占用 / 工作忙 /
//     深夜 / 小游戏可见 → 本轮**不发射**，直接进入下一轮随机间隔）；
//   * 只负责「调度 + 门槛」，**不含任何几何与界面逻辑**（落点抽样在视图层，见
//     core/RandomFlickerRules.h）；可 headless 单测。

#include "core/IRandom.h"

#include <QObject>

#include <functional>

class QTimer;

namespace whalepet::viewmodel {

class FlickerService : public QObject {
    Q_OBJECT
public:
    static constexpr int kMinIntervalMs = 2 * 60 * 1000; // 随机间隔下限（2 分钟）
    static constexpr int kMaxIntervalMs = 8 * 60 * 1000; // 随机间隔上限（8 分钟）

    explicit FlickerService(QObject *parent = nullptr);
    ~FlickerService() override;

    // 注入随机源（可测性：概率事件必须可测，见 core/IRandom.h）；为空回落内部 SystemRandom
    void setRandom(core::IRandom *rng);

    // 启动 / 停止随机调度（幂等）。start() **不**立即闪现：首次也要等一个随机周期。
    void start();
    void stop();
    bool running() const;

    // 随机间隔区间（要求 0 < minMs <= maxMs；非法输入被忽略，保持原区间）
    void setIntervalRange(int minMs, int maxMs);
    int minIntervalMs() const { return m_minMs; }
    int maxIntervalMs() const { return m_maxMs; }

    // 触发门槛（宿主注入；为空 = 始终允许）。返回 false 时本轮不发射。
    void setCanFlicker(std::function<bool()> hook);

    // 立即触发一次（跳过等待与门槛；供单测与将来的主动入口使用）
    void flickerNow();

    // 最近一次已排定的等待时长（ms；未启动为 0）——诊断与只读状态能力用
    int lastDelayMs() const { return m_lastDelayMs; }

signals:
    void flickerRequested();

private:
    void scheduleNext();

    QTimer *m_timer = nullptr;
    int m_minMs = kMinIntervalMs;
    int m_maxMs = kMaxIntervalMs;
    int m_lastDelayMs = 0;
    std::function<bool()> m_canFlicker;
    core::SystemRandom m_systemRng;
    core::IRandom *m_rng = &m_systemRng; // 非拥有
};

} // namespace whalepet::viewmodel
