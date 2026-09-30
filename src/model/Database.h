#pragma once

// SQLite 连接 + 建表/迁移 + 事务 + 降级 —— docs/DATA-MODEL.md §1/§4。
//
// 生命周期：main() 创建唯一实例（或由 PetController 持有），外部通过 repo 访问。
// 线程约定：本阶段所有调用都在 GUI 线程，未做跨线程连接池。

#include "model/DataPaths.h"

#include <QSqlDatabase>
#include <QString>

namespace whalepet::model {

class Database {
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // 自动选择目录（安装目录 → 用户目录 → 内存），并完成迁移
    bool open();

    // 指定数据目录（可注入 mode，供单测）；失败不自动降级，由调用方决定
    bool openAt(const QString &dataDir, StorageMode mode = StorageMode::UserDir);

    // 指定完整连接串（如 ":memory:" 或临时文件），供单测
    bool openPath(const QString &dbPath, StorageMode mode = StorageMode::Memory);

    // 内存库
    bool openMemory() { return openPath(QStringLiteral(":memory:"), StorageMode::Memory); }

    void close();

    bool isOpen() const { return m_open; }
    StorageMode mode() const { return m_mode; }
    QString location() const { return m_location; }
    QSqlDatabase db() const { return m_db; }

    bool beginTransaction();
    bool commit();
    bool rollback();

    // meta 表读写（schema_version、last_signin_day 等）
    QString meta(const QString &key, const QString &defaultValue = QString()) const;
    bool setMeta(const QString &key, const QString &value);

    int schemaVersion() const;

private:
    bool finalizeOpen(const QString &dbPath, StorageMode mode);

    QString m_connectionName;
    QString m_location;
    StorageMode m_mode = StorageMode::Memory;
    QSqlDatabase m_db;
    bool m_open = false;
};

} // namespace whalepet::model
