#include "model/DataPaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace whalepet::model {

bool DataPaths::isDirectoryWritable(const QString &dir)
{
    if (dir.isEmpty()) {
        return false;
    }

    QDir d(dir);
    if (!d.exists() && !d.mkpath(QStringLiteral("."))) {
        return false;
    }

    // 真实写入探测：权限位/ACL 都能被这一步覆盖，且能识别 UAC 虚拟化目录
    QFile probe(d.filePath(QStringLiteral(".whalepet_write_probe")));
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool wrote = (probe.write("1") == 1);
    probe.close();
    probe.remove();
    return wrote;
}

StorageMode DataPaths::pickMode(const QString &installDataDir, const QString &userDataDir)
{
    if (isDirectoryWritable(installDataDir)) {
        return StorageMode::InstallDir;
    }
    if (isDirectoryWritable(userDataDir)) {
        qWarning() << "[DataPaths] 安装目录不可写，降级到用户目录:" << userDataDir;
        return StorageMode::UserDir;
    }
    qWarning() << "[DataPaths] data dir fallback: 安装目录与用户目录均不可写，改用内存库（数据不落盘）";
    return StorageMode::Memory;
}

DataLocation DataPaths::resolveWith(const QString &installDataDir, const QString &userDataDir)
{
    DataLocation loc;
    loc.mode = pickMode(installDataDir, userDataDir);

    switch (loc.mode) {
    case StorageMode::InstallDir:
        loc.directory = installDataDir;
        break;
    case StorageMode::UserDir:
        loc.directory = userDataDir;
        break;
    case StorageMode::Memory:
        loc.directory.clear();
        break;
    }

    loc.dbPath = loc.directory.isEmpty()
                     ? QStringLiteral(":memory:")
                     : QDir(loc.directory).filePath(databaseFileName());
    return loc;
}

DataLocation DataPaths::resolve()
{
    const QString installDataDir =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));

    QString userDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (userDir.isEmpty()) {
        // 极端环境（无用户目录）：交给 pickMode 判为不可写 → 内存降级
        qWarning() << "[DataPaths] 无法获取用户数据目录（AppDataLocation 为空）";
    }

    return resolveWith(installDataDir, userDir);
}

} // namespace whalepet::model
