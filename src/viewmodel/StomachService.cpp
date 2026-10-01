#include "viewmodel/StomachService.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace whalepet::viewmodel {

namespace {

const char *kStomachDirName = "stomach";

// 递归复制（文件/文件夹）；dst 必须尚不存在
bool copyRecursively(const QString &src, const QString &dst)
{
    const QFileInfo info(src);
    if (info.isDir()) {
        if (!QDir().mkpath(dst)) {
            return false;
        }
        const QFileInfoList entries = QDir(src).entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            if (!copyRecursively(entry.absoluteFilePath(),
                                 dst + QLatin1Char('/') + entry.fileName())) {
                return false;
            }
        }
        return true;
    }
    if (QFile::exists(dst)) {
        return false;
    }
    return QFile::copy(src, dst);
}

// 递归删除（文件/文件夹）
bool removeRecursively(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        return QDir(path).removeRecursively();
    }
    return QFile::remove(path);
}

// 目标目录下的不冲突文件名：重名时追加 " (n)"，绝不覆盖既有条目
QString uniqueTarget(const QString &dirPath, const QString &fileName)
{
    QDir dir(dirPath);
    QString candidate = dir.filePath(fileName);
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    const QString ext = dot > 0 ? fileName.mid(dot) : QString();
    for (int i = 1; i < 100000; ++i) {
        candidate = dir.filePath(QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(ext));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

// 移动单个条目：优先 rename（同盘零拷贝），跨盘/失败回退为「复制 + 删除」
bool moveEntry(const QString &src, const QString &dstDir, QString *error)
{
    const QFileInfo info(src);
    if (!info.exists()) {
        if (error != nullptr) {
            *error = QStringLiteral("源不存在: %1").arg(src);
        }
        return false;
    }
    const QString target = uniqueTarget(dstDir, info.fileName());
    if (target.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("无法生成目标名: %1").arg(info.fileName());
        }
        return false;
    }
    if (QFile::rename(src, target)) {
        return true;
    }
    if (copyRecursively(src, target) && removeRecursively(src)) {
        return true;
    }
    removeRecursively(target); // 清理半成品，保持源文件不动
    if (error != nullptr) {
        *error = QStringLiteral("移动失败: %1 -> %2").arg(src, target);
    }
    return false;
}

} // namespace

StomachService::StomachService(QObject *parent)
    : QObject(parent)
    , m_stomachPath(QDir(QCoreApplication::applicationDirPath())
                        .filePath(QString::fromLatin1(kStomachDirName)))
{
}

StomachService::~StomachService() = default;

QString StomachService::stomachPath() const
{
    return m_stomachPath;
}

bool StomachService::ensureStomachDir()
{
    if (m_stomachPath.isEmpty()) {
        return false;
    }
    QDir dir(m_stomachPath);
    if (dir.exists()) {
        return true;
    }
    if (!dir.mkpath(QStringLiteral("."))) {
        qWarning() << "[StomachService] 无法创建 stomach 目录（安装目录不可写？）:" << m_stomachPath;
        return false;
    }
    return true;
}

int StomachService::ingest(const QStringList &paths)
{
    if (paths.isEmpty() || !ensureStomachDir()) {
        return 0;
    }
    int count = 0;
    for (const QString &path : paths) {
        QString error;
        if (moveEntry(path, m_stomachPath, &error)) {
            ++count;
        } else {
            qWarning() << "[StomachService] 入胃失败:" << error;
        }
    }
    if (count > 0) {
        emit ingested(count);
    }
    return count;
}

int StomachService::emptyToTrash()
{
    if (!ensureStomachDir()) {
        return 0;
    }
    // 只取顶层条目：文件夹整体移入回收站（Windows 下由 Qt 递归交给系统处理）
    const QFileInfoList entries = QDir(m_stomachPath).entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    QStringList paths;
    paths.reserve(entries.size());
    for (const QFileInfo &entry : entries) {
        paths << entry.absoluteFilePath();
    }
    return trashEntries(paths);
}

int StomachService::trashEntries(const QStringList &paths)
{
    int count = 0;
    for (const QString &path : paths) {
        if (QFile::moveToTrash(path)) {
            ++count;
        } else {
            qWarning() << "[StomachService] 移入回收站失败:" << path;
        }
    }
    if (count > 0) {
        emit trashed(count);
    }
    return count;
}

void StomachService::start(int intervalMs)
{
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::CoarseTimer);
        connect(m_timer, &QTimer::timeout, this, [this]() { emptyToTrash(); });
    }
    m_timer->start(intervalMs > 0 ? intervalMs : kCheckIntervalMs);
}

void StomachService::stop()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

bool StomachService::running() const
{
    return m_timer != nullptr && m_timer->isActive();
}

} // namespace whalepet::viewmodel
