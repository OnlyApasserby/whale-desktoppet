#include "model/Database.h"

#include "model/Schema.h"

#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace whalepet::model {

namespace {
QAtomicInt g_connectionCounter(0);
}

Database::Database() = default;

Database::~Database()
{
    close();
}

bool Database::open()
{
    const DataLocation loc = DataPaths::resolve();

    if (loc.mode == StorageMode::Memory) {
        return openMemory();
    }

    if (openAt(loc.directory, loc.mode)) {
        return true;
    }

    // 目录可用但连接/建表失败：退到内存，保证程序可用（数据不落盘）
    qWarning() << "[Database] 打开" << loc.dbPath << "失败，降级为内存库（data dir fallback）";
    return openMemory();
}

bool Database::openAt(const QString &dataDir, StorageMode mode)
{
    if (dataDir.isEmpty()) {
        return false;
    }
    QDir dir(dataDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "[Database] 无法创建数据目录:" << dataDir;
        return false;
    }
    return openPath(dir.filePath(DataPaths::databaseFileName()), mode);
}

bool Database::openPath(const QString &dbPath, StorageMode mode)
{
    close();

    const bool isMemory = (dbPath == QStringLiteral(":memory:"));
    if (!isMemory) {
        const QString parent = QFileInfo(dbPath).absolutePath();
        if (!QDir().mkpath(parent)) {
            qWarning() << "[Database] 无法创建父目录:" << parent;
            return false;
        }
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qWarning() << "[Database] QSQLITE 驱动不可用（检查 sqldrivers/qsqlite 插件与 Qt 插件路径）";
        return false;
    }

    m_connectionName =
        QStringLiteral("whalepet_%1").arg(g_connectionCounter.fetchAndAddRelaxed(1) + 1);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "[Database] 打开失败:" << dbPath << m_db.lastError().text();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }

    return finalizeOpen(dbPath, mode);
}

bool Database::finalizeOpen(const QString &dbPath, StorageMode mode)
{
    if (!Schema::migrate(m_db)) {
        qWarning() << "[Database] 建表/迁移失败:" << dbPath;
        close();
        return false;
    }

    m_location = dbPath;
    m_mode = mode;
    m_open = true;
    qInfo() << "[Database] 已连接:" << dbPath << "schema_version =" << Schema::readVersion(m_db);
    return true;
}

void Database::close()
{
    if (m_connectionName.isEmpty()) {
        m_open = false;
        return;
    }

    // 必须先释放所有 QSqlQuery 引用再 removeDatabase
    if (m_db.isValid()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
    m_location.clear();
    m_open = false;
}

bool Database::beginTransaction()
{
    return m_open && m_db.transaction();
}

bool Database::commit()
{
    return m_open && m_db.commit();
}

bool Database::rollback()
{
    return m_open && m_db.rollback();
}

QString Database::meta(const QString &key, const QString &defaultValue) const
{
    if (!m_open) {
        return defaultValue;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = :k"));
    q.bindValue(QStringLiteral(":k"), key);
    if (!q.exec() || !q.next()) {
        return defaultValue;
    }
    return q.value(0).toString();
}

bool Database::setMeta(const QString &key, const QString &value)
{
    if (!m_open) {
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES(:k, :v)"));
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), value);
    if (q.exec()) {
        return true;
    }
    qWarning() << "[Database] setMeta 失败:" << key << q.lastError().text();
    return false;
}

int Database::schemaVersion() const
{
    if (!m_open) {
        return 0;
    }
    QSqlDatabase handle = m_db;
    return Schema::readVersion(handle);
}

} // namespace whalepet::model
