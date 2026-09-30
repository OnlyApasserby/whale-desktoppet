#include "view/PoseLibrary.h"

#include "common/PetVisuals.h"
#include "core/PoseCatalog.h"

#include <QDebug>
#include <QTimer>

namespace whalepet {

namespace {
constexpr int kFirstBatch = 5;          // 首批同步加载张数
constexpr int kPreloadIntervalMs = 120; // 其余每张间隔
} // namespace

PoseLibrary::PoseLibrary(QObject *parent)
    : QObject(parent)
{
    m_allKeys.reserve(core::kPoseCount);
    for (int i = 0; i < core::kPoseCount; ++i) {
        m_allKeys.append(QString::fromUtf8(core::kPoses[i].key));
    }
}

QString PoseLibrary::resourcePath(const QString &key)
{
    const QByteArray utf8 = key.toUtf8();
    const char *file = core::poseFile(utf8.constData());
    if (file == nullptr) {
        return {};
    }
    return QStringLiteral(":/poses/%1.webp").arg(QString::fromUtf8(file));
}

void PoseLibrary::startPreload()
{
    if (!m_pending.isEmpty()) {
        return;
    }

    // 首批同步：保证首帧可用
    const int batch = qMin(kFirstBatch, m_allKeys.size());
    for (int i = 0; i < batch; ++i) {
        loadOne(m_allKeys.at(i));
    }

    // 其余进入后台队列
    for (int i = batch; i < m_allKeys.size(); ++i) {
        m_pending.append(m_allKeys.at(i));
    }

    if (m_pending.isEmpty()) {
        return;
    }

    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kPreloadIntervalMs);
        connect(m_timer, &QTimer::timeout, this, &PoseLibrary::onPreloadTick);
    }
    m_timer->start();
}

void PoseLibrary::onPreloadTick()
{
    if (m_cursor >= m_pending.size()) {
        if (m_timer != nullptr) {
            m_timer->stop();
        }
        return;
    }
    loadOne(m_pending.at(m_cursor));
    ++m_cursor;
}

void PoseLibrary::loadOne(const QString &key)
{
    if (m_cache.contains(key)) {
        return;
    }

    const QString path = resourcePath(key);
    if (path.isEmpty()) {
        qWarning() << "[PoseLibrary] 未登记的 pose:" << key;
        return;
    }

    QPixmap pixmap;
    if (!pixmap.load(path)) {
        // 单张失败静默跳过：不阻塞预载队列
        qWarning() << "[PoseLibrary] 立绘加载失败（跳过）:" << path;
        return;
    }
    m_cache.insert(key, pixmap);
    emit poseReady(key);
}

} // namespace whalepet
