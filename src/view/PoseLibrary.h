#pragma once

// 立绘库：按需惰性加载 93 张 webp 资源。
//
// 策略（docs/PRESENTATION.md §2）：启动先同步载入首批少量立绘保证首帧，其余以定时器
// 逐张后台补齐；单张加载失败静默跳过，不阻塞、不弹错。

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

    // 启动预载：首批同步 + 其余定时补齐
    void startPreload();

    bool isLoaded(const QString &key) const { return m_cache.contains(key); }
    QPixmap pixmap(const QString &key) const { return m_cache.value(key); }

    static QString resourcePath(const QString &key);

    int loadedCount() const { return m_cache.size(); }
    int totalCount() const { return m_allKeys.size(); }
    bool preloadFinished() const { return m_cursor >= m_pending.size(); }

signals:
    // 某张立绘后台补齐完成（PoseView 用它兑现「等待中」的切换请求）
    void poseReady(const QString &key);

private:
    void loadOne(const QString &key);
    void onPreloadTick();

    QHash<QString, QPixmap> m_cache;
    QStringList m_allKeys;    // 全部 pose key（kPoses 顺序）
    QStringList m_pending;    // 待后台加载的 key
    int m_cursor = 0;
    QTimer *m_timer = nullptr;
};

} // namespace whalepet
