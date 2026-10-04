#pragma once

// RecycleBinService（2026-10-04 立绘激活 18 · sweep）：
// 以**随机间隔轮询** Windows 回收站，检测到非空时通知上层展示 sweep 立绘并发送清理提醒。
//
// 边界与降级（与 docs/README.md §5.3 口径一致）：
//   * 仅在 Windows 上调用 SHQueryRecycleBin（shell32）；查询失败 / 非 Windows 一律视为
//     「未知」——不伪造「非空」、不触发提醒；
//   * 采用**边沿触发**：仅当「由空（或未知）→ 非空」时才发 recycleBinNotEmpty，避免周期性
//     刷屏；回收站被清空后再次变非空会重新提醒；
//   * 轮询间隔在 [kMinIntervalMs, kMaxIntervalMs] 内**每次重新随机抽取**。
//
// 零界面依赖：仅 Qt6::Core + Win32 shell API，可 headless 使用。

#include <QObject>

#include <cstdint>

class QTimer;

namespace whalepet::viewmodel {

// 一次回收站查询的结果
struct RecycleBinInfo {
    bool available = false; // 查询是否成功（false = 非 Windows / 调用失败 → 视为未知）
    int itemCount = 0;      // 条目数
    qint64 sizeBytes = 0;   // 占用字节数
    bool isEmpty() const { return itemCount <= 0; }
};

class RecycleBinService : public QObject {
    Q_OBJECT
public:
    static constexpr int kMinIntervalMs = 5 * 60 * 1000;  // 随机轮询下限（5 分钟）
    static constexpr int kMaxIntervalMs = 10 * 60 * 1000; // 随机轮询上限（10 分钟）

    explicit RecycleBinService(QObject *parent = nullptr);
    ~RecycleBinService() override;

    // 启动 / 停止后台随机轮询（幂等）。start() 会立即检查一次，随后进入随机周期。
    void start();
    void stop();
    bool running() const;

    // 随机轮询区间（要求 0 < minMs <= maxMs；非法输入被忽略）
    void setIntervalRange(int minMs, int maxMs);

    // 立即检查一次（不受轮询周期约束）；返回回收站是否非空。
    // 非空时**无条件**发出 recycleBinNotEmpty（手动检查即用户明确意图）。
    bool checkNow();

    // 只读查询（不触发任何信号）
    RecycleBinInfo query() const;

    // 最近一次查询结果（start() 前为默认「未知」）
    const RecycleBinInfo &lastInfo() const { return m_last; }

signals:
    // 每次轮询完成（available=false 表示查询失败 / 平台不支持）
    void checked(bool available, int itemCount, qint64 sizeBytes);
    // 检测到回收站非空（轮询为**边沿触发**；checkNow 为无条件触发）
    void recycleBinNotEmpty(int itemCount, qint64 sizeBytes);

private:
    void poll();          // 一次周期轮询（边沿触发）
    void scheduleNext();  // 抽取下一个随机间隔并启动单次定时器

    QTimer *m_timer = nullptr;
    int m_minMs = kMinIntervalMs;
    int m_maxMs = kMaxIntervalMs;
    RecycleBinInfo m_last;
    // 0 = 未知 / 1 = 已知空 / 2 = 已知非空（用于边沿判定）
    int m_state = 0;
};

} // namespace whalepet::viewmodel
