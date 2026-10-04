#pragma once

// 立绘库：**按需加载 + 容量受限的 LRU 缓存**（docs/POSE-ASSETS.md 阶段 B「路径 A」）。
//
// 与旧版的差异（旧版 = 启动后无条件全量预载 93 张并永久常驻）：
//   * **分档预载**：core 档启动同步加载（首帧与延迟敏感路径），warm 档启动后定时补齐，
//     其余姿态**不预载**、首次显示时由 PoseView 按需加载并回填本缓存。
//   * **容量上限**：缓存条目超过 kCacheCapacity 后按「最久未使用」逐出，
//     不再出现 93 张（≈23.3 MiB）永久常驻。
//   * **负缓存**：单张加载失败只记录一次（PoseLoadError 分类），不重复解码、
//     不重复打日志；资源被回填后自动清除负缓存。
//   * **严格校验**：所有加载都经 PoseImageLoader（格式白名单 + 尺寸必须 256x256）。
//
// 行为等价性：PoseView 在库未命中时本就会按需即时加载单张，故「不预载」只影响
// 首次显示的一点点解码延迟，不影响正确性。
//
// 档位白名单是**代码内的显式声明**，作用是让预载成本与使用频率挂钩；
// docs/POSE-ASSETS.md S1 规划的 poses.json 索引落地后，白名单改由索引的
// preload 字段驱动，届时本文件的两个白名单随之退役。

#include "view/PoseImageLoader.h"

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QTimer;

namespace whalepet {

class PoseLibrary : public QObject {
    Q_OBJECT
public:
    explicit PoseLibrary(QObject *parent = nullptr);

    // 启动预载：core 档同步 + warm 档定时补齐。幂等（重复调用无副作用）。
    void startPreload();

    // 纯查询，不更新 LRU 时间戳
    bool isLoaded(const QString &key) const { return m_cache.contains(key); }
    // key 是否是已登记的 pose（用于判断能否回填缓存）
    bool isKnown(const QString &key) const { return m_allKeys.contains(key); }

    // 取一张已缓存的立绘；未命中返回空 QPixmap。**命中会更新 LRU 时间戳。**
    QPixmap take(const QString &key);

    // 回填一张调用方已加载好的立绘（按需加载路径用），受容量上限约束。
    // 同时清除该 key 的负缓存。返回是否入得去。
    bool put(const QString &key, const QPixmap &pixmap);

    // **按需确保**某张立绘已加载并进入缓存；已缓存直接返回 true（并更新 LRU）。
    // 内部与预载队列走**完全相同**的严格校验 + 负缓存路径，
    // 因此 PoseView 的按需加载可以直接委托到这里，不必自己解码。
    bool ensureLoaded(const QString &key);

    // 调整容量上限并立即逐出（newCapacity <= 0 视为不变）
    void setCapacity(int newCapacity);

    // 由 pose key 拼出资源路径（全工程唯一拼接点）；未登记返回空串
    static QString resourcePath(const QString &key);

    // ---- 诊断（此前只有声明、零调用；路径 A 起可供单测与设置面板使用）----
    int loadedCount() const { return m_cache.size(); }
    int totalCount() const { return m_allKeys.size(); }
    int failedCount() const { return m_failed.size(); }
    int capacity() const { return m_capacity; }
    // 已过滤（去未登记项）后的实际 core / warm 档数量
    int coreCount() const { return m_core.size(); }
    int warmCount() const { return m_warm.size(); }
    // 常驻立绘像素内存估算（ARGB32 理论下限，字节）——对应 POSE-ASSETS.md 的 M3
    qint64 residentBytes() const;
    // 累计真正触发过解码的次数（负缓存命中不计入）。用于验证「失败不重复解码」。
    int decodeAttempts() const { return m_decodeAttempts; }
    // 累计解码成功的张数
    int decodedTotal() const { return m_decodedTotal; }
    bool preloadFinished() const { return !m_preloadStarted || m_cursor >= m_pending.size(); }

    // 档位**声明值**（未经「是否已登记」过滤），供单测校验其全部存在于 core::kPoses
    static const QStringList &coreKeys();
    static const QStringList &warmKeys();

signals:
    // 某张立绘补齐完成（PoseView 用它兑现「等待中」的切换请求）
    void poseReady(const QString &key);

private:
    void loadOne(const QString &key);
    void onPreloadTick();
    void evictIfNeeded();
    // 把声明档位过滤为「已登记且去重」的列表；未登记项告警后忽略（退化为按需加载）
    QStringList sanitizeTier(const QStringList &declared, const char *tierName) const;

    struct CacheEntry {
        QPixmap pixmap;
        quint64 lastUsed = 0; // 越大越新；逐出时选最小者
    };

    QHash<QString, CacheEntry> m_cache;
    QHash<QString, PoseLoadError> m_failed; // 负缓存：失败原因分类，避免重复解码与重复告警
    QStringList m_allKeys;                  // 全部 pose key（kPoses 顺序）
    QStringList m_core;                     // 过滤后的 core 档
    QStringList m_warm;                     // 过滤后的 warm 档
    QStringList m_pending;                  // warm 档待补齐队列
    int m_cursor = 0;
    int m_capacity = kCacheCapacity;
    quint64 m_useCounter = 0;
    int m_decodeAttempts = 0; // 累计解码尝试次数（负缓存命中不计入）
    int m_decodedTotal = 0;    // 累计解码成功张数
    bool m_preloadStarted = false;
    QTimer *m_timer = nullptr;

    // 缓存条目上限。36 × 256KB ≈ 9.0 MiB，对照 POSE-ASSETS.md 的 M3 目标 ≤ 10 MiB。
    // 取 36 而非更小值：core + warm 已超过 32 张，容量过小会与 warm 档逐出互相
    // 打架，反而制造「刚预载就被逐出、又要重新解码」的抖动。
    static constexpr int kCacheCapacity = 36;
    // warm 档补齐节奏。保持旧版的 120ms/张：单张解码远小于 16ms 帧预算，
    // 且 warm 档已从 88 张降到 22 张，总耗时由 ≈10.6s 降到 ≈2.6s。
    static constexpr int kPreloadIntervalMs = 120;
};

} // namespace whalepet
