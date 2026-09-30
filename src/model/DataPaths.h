#pragma once

// 数据目录选择与降级 —— 落实 docs/DATA-MODEL.md §1、docs/ARCHITECTURE.md §8。
//
// 顺序：<安装目录>/data → <用户目录> → 内存模式（日志告警，不崩溃）。

#include <QString>

namespace whalepet::model {

enum class StorageMode {
    InstallDir, // 安装目录同级 data/
    UserDir,    // 用户目录（AppDataLocation）
    Memory      // 内存库 + 日志告警
};

// 解析结果：数据库连接目标
struct DataLocation {
    StorageMode mode = StorageMode::Memory;
    QString directory; // 库文件所在目录；内存模式为空
    QString dbPath;    // SQLite 连接串；内存模式为 ":memory:"
};

class DataPaths {
public:
    // 生产路径：安装目录 → 用户目录 → 内存
    static DataLocation resolve();

    // 可注入版本（单测用）：显式给出两个候选目录，不读 QCoreApplication
    static DataLocation resolveWith(const QString &installDataDir, const QString &userDataDir);

    // 可写性探测：**实际写入再删除**一个临时文件（不依赖权限位，兼容 UAC 虚拟化）
    static bool isDirectoryWritable(const QString &dir);

    static StorageMode pickMode(const QString &installDataDir, const QString &userDataDir);

    static QString databaseFileName() { return QStringLiteral("whalepet.db"); }
};

} // namespace whalepet::model
