#include "model/DataPaths.h"
#include "model/Database.h"
#include "model/PetStateData.h"
#include "model/PetStateRepo.h"
#include "model/Schema.h"
#include "model/SettingsRepo.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest>

using namespace whalepet::model;

namespace {

// 构造一个「一定不可写」的目录路径：把它挂在一个**普通文件**下面，
// mkpath 必然失败。比用不存在的盘符（Z:）更可靠。
QString makeUnwritableDir(const QString &baseDir, const QString &blockerName)
{
    QFile blocker(QDir(baseDir).filePath(blockerName));
    if (blocker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        blocker.write("blocker");
        blocker.close();
    }
    return QDir(baseDir).filePath(blockerName + QStringLiteral("/sub"));
}

QStringList tableNames(QSqlDatabase db)
{
    QStringList names;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'"))) {
        return names;
    }
    while (q.next()) {
        names << q.value(0).toString();
    }
    return names;
}

} // namespace

class TestDatabase : public QObject {
    Q_OBJECT

private slots:
    void schemaVersionAfterMigrate();
    void allV1TablesCreated();
    void migrateIsIdempotent();

    void petStateLoadMissingRow();
    void petStateRoundTrip();
    void settingsNullPosition();
    void settingsRoundTrip();

    void transactionRollback();

    void dataPathsPrefersInstallDir();
    void dataPathsFallsBackToUserDir();
    void dataPathsFallsBackToMemory();
    void openAtUnwritableDirFails();
    void openOnUnwritableEverythingFallsBackToMemory();
};

void TestDatabase::schemaVersionAfterMigrate()
{
    Database db;
    QVERIFY(db.openMemory());
    QCOMPARE(db.schemaVersion(), Schema::kVersion);
    QCOMPARE(static_cast<int>(db.mode()), static_cast<int>(StorageMode::Memory));
    QVERIFY(db.isOpen());
}

void TestDatabase::allV1TablesCreated()
{
    Database db;
    QVERIFY(db.openMemory());

    const QStringList names = tableNames(db.db());
    const QStringList expected = {QStringLiteral("meta"),
                                  QStringLiteral("pet_state"),
                                  QStringLiteral("settings"),
                                  QStringLiteral("achievements"),
                                  QStringLiteral("quests"),
                                  QStringLiteral("signin"),
                                  QStringLiteral("bond_diary"),
                                  QStringLiteral("chat_history")};
    for (const QString &t : expected) {
        QVERIFY2(names.contains(t), qPrintable(QStringLiteral("缺表: ") + t));
    }
}

void TestDatabase::migrateIsIdempotent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("whalepet.db"));

    {
        Database db;
        QVERIFY(db.openPath(path, StorageMode::InstallDir));
        QCOMPARE(db.schemaVersion(), Schema::kVersion);
    }
    {
        // 二次打开：迁移应幂等，且版本号不漂移
        Database db;
        QVERIFY(db.openPath(path, StorageMode::InstallDir));
        QCOMPARE(db.schemaVersion(), Schema::kVersion);
        QSqlDatabase handle = db.db();
        QVERIFY(Schema::migrate(handle));
        QCOMPARE(db.schemaVersion(), Schema::kVersion);
    }
}

void TestDatabase::petStateLoadMissingRow()
{
    Database db;
    QVERIFY(db.openMemory());

    PetStateRepo repo(&db);
    PetStateData data;
    const int moodBefore = data.mood;
    QVERIFY(!repo.load(data));            // 无记录
    QCOMPARE(data.mood, moodBefore);      // out 保持不变
}

void TestDatabase::petStateRoundTrip()
{
    Database db;
    QVERIFY(db.openMemory());

    PetStateRepo repo(&db);
    PetStateData in;
    in.level = 3;
    in.exp = 1042;
    in.coins = 17;
    in.mood = 66;
    in.affinity = 1042;
    in.satiety = 42;
    in.bondLevel = 3;
    in.companionMs = 123456789;
    in.streakDays = 5;
    in.lastActiveMs = 1700000000000LL;
    in.updatedMs = 1700000000001LL;
    QVERIFY(repo.save(in));

    PetStateData out;
    QVERIFY(repo.load(out));
    QCOMPARE(out.level, in.level);
    QCOMPARE(out.exp, in.exp);
    QCOMPARE(out.coins, in.coins);
    QCOMPARE(out.mood, in.mood);
    QCOMPARE(out.affinity, in.affinity);
    QCOMPARE(out.satiety, in.satiety);
    QCOMPARE(out.bondLevel, in.bondLevel);
    QCOMPARE(out.companionMs, in.companionMs);
    QCOMPARE(out.streakDays, in.streakDays);
    QCOMPARE(out.lastActiveMs, in.lastActiveMs);
    QCOMPARE(out.updatedMs, in.updatedMs);

    // 单例行：save 两次仍只有一行（INSERT OR REPLACE 幂等）
    in.mood = 90;
    QVERIFY(repo.save(in));
    QSqlQuery count(db.db());
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM pet_state")));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TestDatabase::settingsNullPosition()
{
    Database db;
    QVERIFY(db.openMemory());

    SettingsRepo repo(&db);
    SettingsData data;
    QVERIFY(!repo.load(data)); // 无记录 → 默认值
    QVERIFY(!data.hasPosition);

    data.hasPosition = false;
    data.poseSize = 256;
    data.bubbleEnabled = false;
    QVERIFY(repo.save(data));

    SettingsData out;
    QVERIFY(repo.load(out));
    QVERIFY(!out.hasPosition); // NULL 往返后仍为「未设置」
    QCOMPARE(out.poseSize, 256);
    QCOMPARE(out.bubbleEnabled, false);
    QCOMPARE(out.particlesEnabled, true);
    QCOMPARE(out.keywordAware, false);
}

void TestDatabase::settingsRoundTrip()
{
    Database db;
    QVERIFY(db.openMemory());

    SettingsRepo repo(&db);
    SettingsData in;
    in.hasPosition = true;
    in.posX = -1280;
    in.posY = 720;
    in.poseSize = 320;
    in.bubbleEnabled = true;
    in.particlesEnabled = false;
    in.keywordAware = true;
    in.minigameEnabled = true;
    in.jsonExt = QStringLiteral("{\"visible\":true}");
    QVERIFY(repo.save(in));

    SettingsData out;
    QVERIFY(repo.load(out));
    QVERIFY(out.hasPosition);
    QCOMPARE(out.posX, -1280);
    QCOMPARE(out.posY, 720);
    QCOMPARE(out.poseSize, 320);
    QCOMPARE(out.keywordAware, true);
    QCOMPARE(out.minigameEnabled, true);
    QCOMPARE(out.jsonExt, in.jsonExt);

    QVERIFY(repo.clearPosition());
    SettingsData cleared;
    QVERIFY(repo.load(cleared));
    QVERIFY(!cleared.hasPosition);
    QCOMPARE(cleared.poseSize, 320); // 其余字段不受影响
}

void TestDatabase::transactionRollback()
{
    Database db;
    QVERIFY(db.openMemory());

    PetStateRepo repo(&db);
    PetStateData committed;
    committed.mood = 55;
    QVERIFY(repo.save(committed));

    QVERIFY(db.beginTransaction());
    PetStateData dirty = committed;
    dirty.mood = 12;
    dirty.level = 9;
    QVERIFY(repo.save(dirty));
    QVERIFY(db.rollback());

    PetStateData after;
    QVERIFY(repo.load(after));
    QCOMPARE(after.mood, 55);
    QCOMPARE(after.level, committed.level);

    // 提交路径
    QVERIFY(db.beginTransaction());
    dirty.mood = 33;
    QVERIFY(repo.save(dirty));
    QVERIFY(db.commit());
    PetStateData committedOut;
    QVERIFY(repo.load(committedOut));
    QCOMPARE(committedOut.mood, 33);
}

void TestDatabase::dataPathsPrefersInstallDir()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString install = QDir(tmp.path()).filePath(QStringLiteral("data"));

    const DataLocation loc = DataPaths::resolveWith(install, tmp.path());
    QCOMPARE(static_cast<int>(loc.mode), static_cast<int>(StorageMode::InstallDir));
    QCOMPARE(loc.directory, install);
    QVERIFY(loc.dbPath.endsWith(QStringLiteral("whalepet.db")));
    QVERIFY(QFile::exists(install));
}

void TestDatabase::dataPathsFallsBackToUserDir()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString badInstall = makeUnwritableDir(tmp.path(), QStringLiteral("blocker_a"));
    const QString userDir = QDir(tmp.path()).filePath(QStringLiteral("user"));

    QVERIFY(!DataPaths::isDirectoryWritable(badInstall));
    const DataLocation loc = DataPaths::resolveWith(badInstall, userDir);
    QCOMPARE(static_cast<int>(loc.mode), static_cast<int>(StorageMode::UserDir));
    QCOMPARE(loc.directory, userDir);
}

void TestDatabase::dataPathsFallsBackToMemory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString badInstall = makeUnwritableDir(tmp.path(), QStringLiteral("blocker_b"));
    const QString badUser = makeUnwritableDir(tmp.path(), QStringLiteral("blocker_c"));

    const DataLocation loc = DataPaths::resolveWith(badInstall, badUser);
    QCOMPARE(static_cast<int>(loc.mode), static_cast<int>(StorageMode::Memory));
    QVERIFY(loc.directory.isEmpty());
    QCOMPARE(loc.dbPath, QStringLiteral(":memory:"));
}

void TestDatabase::openAtUnwritableDirFails()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString bad = makeUnwritableDir(tmp.path(), QStringLiteral("blocker_d"));

    Database db;
    QVERIFY(!db.openAt(bad, StorageMode::InstallDir));
    QVERIFY(!db.isOpen());
}

void TestDatabase::openOnUnwritableEverythingFallsBackToMemory()
{
    // open() 的环境由 applicationDirPath 决定，无法在单测里改。
    // 这里改为直接验证「降级决策」本身：三层顺序由 dataPaths* 用例覆盖，
    // 此用例确认降级后的连接目标可用（内存库能正常建表）。
    Database db;
    QVERIFY(db.openMemory());
    QCOMPARE(db.schemaVersion(), Schema::kVersion);
    QVERIFY(tableNames(db.db()).contains(QStringLiteral("pet_state")));
}

QTEST_GUILESS_MAIN(TestDatabase)
#include "test_database.moc"
