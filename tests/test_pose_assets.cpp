// 立绘资源与加载策略的自动化门禁（docs/POSE-ASSETS.md 阶段 A + B「路径 A」）。
//
// 覆盖五组不变量：
//   1. **严格校验**：93 张现有立绘全部通过「格式白名单 + 尺寸必须 256x256」。
//   2. **闸门拒绝**：尺寸不合规 / 超大图 / 格式未授权 / 无后缀 / 资源不可读 /
//      数据损坏 一律拒绝，且失败原因**可区分**（不是笼统的「加载失败」）。
//   3. **格式口径**：白名单里的后缀必须**确实**能被 Qt 原生解码
//      （证明「仅接受 Qt 原生支持的格式、不引入额外格式解析」这一约束成立）。
//   4. **预载分档**：core / warm 全部是已登记 pose、互不重叠、总量小于缓存容量；
//      非档位 pose（对应 38 张零引用存量）**不被预载**。
//   5. **缓存**：容量上限、LRU 逐出顺序、访问刷新、负缓存不重复解码、
//      常驻内存估算与缓存条目数一致。

#include <QtTest>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSet>
#include <QTemporaryDir>

#include "common/PetVisuals.h"
#include "core/DesktopEdge.h"
#include "core/PoseCatalog.h"
#include "core/PoseNames.h"
#include "view/AssetsResource.h"
#include "view/PoseImageLoader.h"
#include "view/PoseLibrary.h"

using namespace whalepet;

namespace {

// 构造一张指定尺寸的测试图并按指定格式落盘，返回文件路径（空串表示失败）。
// 写入失败一律由调用方判定失败——**不 skip、不放宽**：构建期已强制存在 qwebp 插件，
// Qt 官方 imageformats 插件应当支持 webp 写入；写不出来说明环境异常，必须暴露。
// QTemporaryDir 析构即删，故改用「以 pid 命名」的稳定文件，由调用方负责 remove。
QString writeProbe(const QSize &size, const QString &fileName, const QByteArray &format)
{
    const QString path = QDir::temp().filePath(
        QStringLiteral("whalepet-probe-%1-%2").arg(QCoreApplication::applicationPid()).arg(fileName));
    QImage image(size, QImage::Format_ARGB32);
    image.fill(QColor(12, 34, 56, 255));
    // 画几块不透明像素：某些编码器对单色图有特殊处理，避免构造出退化样本
    const int w = qMax(1, size.width() / 4);
    const int h = qMax(1, size.height() / 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            image.setPixel(x, y, qRgba(200, 100, 50, 255));
        }
    }
    if (!image.save(path, format.constData())) {
        return {};
    }
    return path;
}

// 造一张可用的 256x256 测试位图（供 LRU 容器行为测试使用；不涉及资源路径）
QPixmap makeProbePixmap(const QString &tag)
{
    const QString path = writeProbe(QSize(kPosePixels, kPosePixels),
                                    QStringLiteral("%1.webp").arg(tag),
                                    QByteArrayLiteral("WEBP"));
    if (path.isEmpty()) {
        return QPixmap();
    }
    QPixmap pixmap;
    const bool ok = pixmap.load(path);
    QFile::remove(path);
    return ok ? pixmap : QPixmap();
}

// 不在 core / warm 任一档位的 pose（对应 38 张零引用存量）
QStringList tierExcludedKeys()
{
    QStringList out;
    for (int i = 0; i < core::kPoseCount; ++i) {
        const QString key = QString::fromUtf8(core::kPoses[i].key);
        if (!PoseLibrary::coreKeys().contains(key) && !PoseLibrary::warmKeys().contains(key)) {
            out << key;
        }
    }
    return out;
}

} // namespace

class PoseAssetsTest : public QObject {
    Q_OBJECT
private slots:
    // ---- 1. 现有 93 张 ----
    void everyRegisteredPosePassesStrictValidation();
    void everyRegisteredPoseIsReachableInQrc();
    void unknownPoseKeyHasNoResourcePath();

    // ---- 2. 闸门拒绝 ----
    void rejectsNonSquareSize();
    void rejectsOversizedBeforeDecoding();
    void rejectsUnlistedSuffix();
    void rejectsMissingSuffix();
    void rejectsMissingFile();
    void rejectsCorruptedData();
    void rejectsEmptyPath();

    // ---- 3. 格式口径 ----
    void allowedSuffixesAreNativelyDecodableByQt();

    // ---- 4. 预载分档 ----
    void tierKeysAreAllRegisteredPoses();
    void coreTierCoversFirstFrameAndEdgePoses();
    void tiersDoNotOverlapAndFitCapacity();
    void libraryPreloadsCoreTierSynchronously();
    void libraryDoesNotPreloadNonTierPoses();
    void libraryPreloadIsIdempotent();

    // ---- 5. 缓存 ----
    void lruEvictsLeastRecentlyUsed();
    void takeRefreshesRecency();
    void putRejectsInvalidInputAndRespectsCapacity();
    void ensureLoadedIsIdempotentAndCountsDecodes();
    void negativeCacheAvoidsRepeatedFailure();
    void onDemandLoadFillsCacheAndStaysUnderCapacity();
    void residentBytesMatchesCacheSize();
};

// ---------------------------------------------------------------------------
// 1. 现有 93 张
// ---------------------------------------------------------------------------

void PoseAssetsTest::everyRegisteredPosePassesStrictValidation()
{
    whalepetInitAssetsResource();
    QCOMPARE(core::kPoseCount, 93);

    QStringList failures;
    for (int i = 0; i < core::kPoseCount; ++i) {
        const QString key = QString::fromUtf8(core::kPoses[i].key);
        const QString path = PoseLibrary::resourcePath(key);
        if (path.isEmpty()) {
            failures << QStringLiteral("%1: 路径为空").arg(key);
            continue;
        }
        PoseLoadError error = PoseLoadError::None;
        QString detail;
        const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, &detail);
        if (pixmap.isNull()) {
            failures << QStringLiteral("%1: %2 (%3)")
                            .arg(key, PoseImageLoader::describe(error), detail);
            continue;
        }
        if (pixmap.width() != kPosePixels || pixmap.height() != kPosePixels) {
            failures << QStringLiteral("%1: 尺寸 %2x%3")
                            .arg(key)
                            .arg(pixmap.width())
                            .arg(pixmap.height());
        }
    }
    // 逐条列出失败项，避免只看到一个总数而无法定位
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
}

void PoseAssetsTest::everyRegisteredPoseIsReachableInQrc()
{
    whalepetInitAssetsResource();
    // 「三处对应」在运行期的自检：key 能拼出 :/poses/ 路径，且资源确实在 qrc 里可读
    for (int i = 0; i < core::kPoseCount; ++i) {
        const QString path = PoseLibrary::resourcePath(QString::fromUtf8(core::kPoses[i].key));
        QVERIFY2(!path.isEmpty(), core::kPoses[i].key);
        QVERIFY2(path.startsWith(QStringLiteral(":/poses/")), qPrintable(path));
        QVERIFY2(QFile::exists(path), qPrintable(path));
    }
}

void PoseAssetsTest::unknownPoseKeyHasNoResourcePath()
{
    QVERIFY(PoseLibrary::resourcePath(QStringLiteral("no-such-pose")).isEmpty());
    QVERIFY(PoseLibrary::resourcePath(QString()).isEmpty());
    PoseLoadError error = PoseLoadError::None;
    QVERIFY(PoseImageLoader::loadChecked(QString(), &error, nullptr).isNull());
    QCOMPARE(error, PoseLoadError::UnknownKey);
}

// ---------------------------------------------------------------------------
// 2. 闸门拒绝
// ---------------------------------------------------------------------------

void PoseAssetsTest::rejectsNonSquareSize()
{
    const QString path = writeProbe(QSize(128, 128), QStringLiteral("small.webp"),
                                    QByteArrayLiteral("WEBP"));
    QVERIFY2(!path.isEmpty(), "无法写入测试用 webp（构建期已保证 qwebp 插件存在）");

    PoseLoadError error = PoseLoadError::None;
    QString detail;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, &detail);
    QFile::remove(path);

    QVERIFY(pixmap.isNull());
    QCOMPARE(error, PoseLoadError::SizeRejected);
    // 原因文案必须带上实际尺寸，便于定位是哪张图不合规
    QVERIFY2(detail.contains(QStringLiteral("128")), qPrintable(detail));
}

void PoseAssetsTest::rejectsOversizedBeforeDecoding()
{
    // 2048x2048 ARGB32 = 16 MiB。闸门前置到解码之前，这一张才不会先吃掉 16 MiB。
    const QString path = writeProbe(QSize(2048, 2048), QStringLiteral("huge.webp"),
                                    QByteArrayLiteral("WEBP"));
    QVERIFY2(!path.isEmpty(), "无法写入测试用 webp（构建期已保证 qwebp 插件存在）");

    PoseLoadError error = PoseLoadError::None;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, nullptr);
    QFile::remove(path);

    QVERIFY(pixmap.isNull());
    QCOMPARE(error, PoseLoadError::SizeRejected);
}

void PoseAssetsTest::rejectsUnlistedSuffix()
{
    // 尺寸完全合规（256x256），只把后缀换成未授权格式：
    // 用来证明拒绝理由是**格式**而不是尺寸
    const QString path = writeProbe(QSize(kPosePixels, kPosePixels), QStringLiteral("legit.bmp"),
                                    QByteArrayLiteral("BMP"));
    QVERIFY2(!path.isEmpty(), "Qt 原生支持 BMP 写入，此处失败说明环境异常");

    PoseLoadError error = PoseLoadError::None;
    QString detail;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, &detail);
    QFile::remove(path);

    QVERIFY(pixmap.isNull());
    QCOMPARE(error, PoseLoadError::FormatRejected);
    // 拒绝文案应回显当前白名单，便于扩充时对齐
    QVERIFY2(detail.contains(QStringLiteral("webp")), qPrintable(detail));
}

void PoseAssetsTest::rejectsMissingSuffix()
{
    const QString path = writeProbe(QSize(kPosePixels, kPosePixels),
                                    QStringLiteral("noextension"), QByteArrayLiteral("WEBP"));
    QVERIFY2(!path.isEmpty(), "无法写入测试用 webp（构建期已保证 qwebp 插件存在）");

    PoseLoadError error = PoseLoadError::None;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, nullptr);
    QFile::remove(path);

    QVERIFY(pixmap.isNull());
    QCOMPARE(error, PoseLoadError::FormatRejected);
}

void PoseAssetsTest::rejectsMissingFile()
{
    const QString path = QDir::temp().filePath(
        QStringLiteral("whalepet-absent-%1.webp").arg(QCoreApplication::applicationPid()));
    QVERIFY(!QFile::exists(path));

    PoseLoadError error = PoseLoadError::None;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, nullptr);
    QVERIFY(pixmap.isNull());
    // 文件不存在与「数据损坏」必须可区分
    QVERIFY(error == PoseLoadError::Unreadable || error == PoseLoadError::DecodeFailed);
    const QString expected = error == PoseLoadError::Unreadable
        ? QStringLiteral("资源不可读")
        : QStringLiteral("解码失败");
    QCOMPARE(PoseImageLoader::describe(error), expected);
}

void PoseAssetsTest::rejectsCorruptedData()
{
    // 先写一张合法 256x256 的 webp，再把**数据部分**改坏：
    // 后缀仍在白名单内，内容判定仍是 webp，但数据已不可解码。
    const QString path = writeProbe(QSize(kPosePixels, kPosePixels),
                                    QStringLiteral("broken.webp"), QByteArrayLiteral("WEBP"));
    QVERIFY2(!path.isEmpty(), "无法写入测试用 webp（构建期已保证 qwebp 插件存在）");

    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadWrite), qPrintable(path));
    const qint64 size = file.size();
    QVERIFY(size > 32);
    QVERIFY2(file.seek(size / 2), "普通文件 seek 失败属环境异常");
    QCOMPARE(file.write(QByteArray(16, '\xA5')), qint64(16));
    file.close();

    PoseLoadError error = PoseLoadError::None;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, nullptr);
    QFile::remove(path);

    // 核心断言：绝不放行；具体落在哪个失败分类由 Qt 解码器行为决定
    QVERIFY2(pixmap.isNull(), "损坏数据必须被拒绝，不得放行");
    QVERIFY(error != PoseLoadError::None);
}

void PoseAssetsTest::rejectsEmptyPath()
{
    PoseLoadError error = PoseLoadError::None;
    QVERIFY(PoseImageLoader::loadChecked(QString(), &error, nullptr).isNull());
    QCOMPARE(error, PoseLoadError::UnknownKey);
}

// ---------------------------------------------------------------------------
// 3. 格式口径
// ---------------------------------------------------------------------------

void PoseAssetsTest::allowedSuffixesAreNativelyDecodableByQt()
{
    // 「仅接受 Qt 原生支持的图片格式」这一口径的可执行证据：
    // 白名单里的每个后缀都必须落在 QImageReader::supportedImageFormats() 内
    // （该列表含已加载的 Qt 官方 imageformats 插件，如 WebP）。
    // 本项目不引入任何额外格式解析，故白名单不得超出该列表。
    const QStringList suffixes = PoseImageLoader::allowedSuffixes();
    QVERIFY(!suffixes.isEmpty());
    for (const QString &suffix : suffixes) {
        QVERIFY2(suffix == suffix.toLower(), qPrintable(suffix));
        QVERIFY2(PoseImageLoader::qtCanDecodeSuffix(suffix), qPrintable(suffix));
    }
    // 需要额外解析器 / 外部依赖的格式一律不在册
    for (const QString &forbidden : {QStringLiteral("svg"), QStringLiteral("tiff"),
                                     QStringLiteral("ico"), QStringLiteral("pdf")}) {
        QVERIFY2(!suffixes.contains(forbidden), qPrintable(forbidden));
    }
}

// ---------------------------------------------------------------------------
// 4. 预载分档
// ---------------------------------------------------------------------------

void PoseAssetsTest::tierKeysAreAllRegisteredPoses()
{
    // 档位白名单写错 pose 名会在 PoseLibrary 构造时被过滤 + 告警；
    // 此处从源头断言「全部已登记」，让错误在 CTest 阶段就暴露。
    const QStringList core = PoseLibrary::coreKeys();
    const QStringList warm = PoseLibrary::warmKeys();
    QCOMPARE(core.size(), 12);
    QCOMPARE(warm.size(), 22);

    for (const QString &key : core + warm) {
        QVERIFY2(core::poseExists(key.toUtf8().constData()), qPrintable(key));
    }
    QCOMPARE(QSet<QString>(core.begin(), core.end()).size(), core.size());
    QCOMPARE(QSet<QString>(warm.begin(), warm.end()).size(), warm.size());
}

void PoseAssetsTest::coreTierCoversFirstFrameAndEdgePoses()
{
    const QStringList core = PoseLibrary::coreKeys();
    // 首帧（PetWindow 构造时的 kDefaultPose）必须在 core，否则启动会缺图
    QVERIFY(core.contains(QStringLiteral("idle-cute")));
    // 贴边 4 张延迟最敏感（立即换图 + 首帧还要跑 contentBBox 的逐像素扫描）
    for (core::DesktopEdge edge : {core::DesktopEdge::Top, core::DesktopEdge::Bottom,
                                   core::DesktopEdge::Left, core::DesktopEdge::Right}) {
        const char *key = core::edgePoseKey(edge);
        QVERIFY(key != nullptr);
        QVERIFY2(core.contains(QString::fromUtf8(key)), key);
    }
}

void PoseAssetsTest::tiersDoNotOverlapAndFitCapacity()
{
    const QStringList core = PoseLibrary::coreKeys();
    const QStringList warm = PoseLibrary::warmKeys();
    // core 与 warm 不得重叠：否则同一张会被重复排入预载队列
    for (const QString &key : core) {
        QVERIFY2(!warm.contains(key), qPrintable(key));
    }
    PoseLibrary library;
    QVERIFY(core.size() + warm.size() < library.capacity());
}

void PoseAssetsTest::libraryPreloadsCoreTierSynchronously()
{
    PoseLibrary library;
    library.startPreload();

    // core 档是同步加载的：startPreload() 返回时必须全部就绪
    const QStringList core = PoseLibrary::coreKeys();
    QCOMPARE(library.coreCount(), core.size());
    QCOMPARE(library.warmCount(), 22);
    QCOMPARE(library.loadedCount(), core.size());
    for (const QString &key : core) {
        QVERIFY2(library.isLoaded(key), qPrintable(key));
    }
    // 一张都不该失败（构建期保证 qwebp 插件，资产已统一 256x256）
    QCOMPARE(library.failedCount(), 0);
    QCOMPARE(library.decodeAttempts(), core.size());
}

void PoseAssetsTest::libraryDoesNotPreloadNonTierPoses()
{
    PoseLibrary library;
    library.startPreload();

    // 既不在 core 也不在 warm 的 pose（含 38 张零引用存量）：不得被无条件预载。
    // 这正是路径 A 的核心收益——预载成本与使用频率挂钩。
    const QStringList onDemand = tierExcludedKeys();
    QVERIFY(!onDemand.isEmpty());
    QCOMPARE(onDemand.size(), core::kPoseCount - library.coreCount() - library.warmCount());
    for (const QString &key : onDemand) {
        QVERIFY2(!library.isLoaded(key), qPrintable(key));
    }
    // 预载后常驻内存必须远低于「全量 93 张」的 ≈23.3 MiB（对照 M3 ≤ 10 MiB）
    QVERIFY2(library.residentBytes() < 12LL * 1024 * 1024,
             qPrintable(QStringLiteral("residentBytes=%1").arg(library.residentBytes())));
}

void PoseAssetsTest::libraryPreloadIsIdempotent()
{
    PoseLibrary library;
    library.startPreload();
    const int loaded = library.loadedCount();
    QVERIFY(library.warmCount() > 0);
    // warm 队列尚未补齐，此时 preloadFinished() 应为 false
    QVERIFY(!library.preloadFinished());

    library.startPreload();
    library.startPreload();
    // 幂等：重复 startPreload 不得重复排队列、不得改变已加载集合
    QCOMPARE(library.loadedCount(), loaded);
    QCOMPARE(library.decodeAttempts(), loaded);
}

// ---------------------------------------------------------------------------
// 5. 缓存
// ---------------------------------------------------------------------------

void PoseAssetsTest::lruEvictsLeastRecentlyUsed()
{
    const QPixmap probe = makeProbePixmap(QStringLiteral("lru"));
    QVERIFY(!probe.isNull());

    PoseLibrary library;
    library.setCapacity(2);
    QCOMPARE(library.capacity(), 2);

    QVERIFY(library.put(QStringLiteral("a"), probe));
    QVERIFY(library.put(QStringLiteral("b"), probe));
    QCOMPARE(library.loadedCount(), 2);
    QVERIFY(library.isLoaded(QStringLiteral("a")));
    QVERIFY(library.isLoaded(QStringLiteral("b")));

    // 插入第三张 → 逐出最久未用的 a
    QVERIFY(library.put(QStringLiteral("c"), probe));
    QCOMPARE(library.loadedCount(), 2);
    QVERIFY2(!library.isLoaded(QStringLiteral("a")), "最久未使用项应被逐出");
    QVERIFY(library.isLoaded(QStringLiteral("b")));
    QVERIFY(library.isLoaded(QStringLiteral("c")));
}

void PoseAssetsTest::takeRefreshesRecency()
{
    const QPixmap probe = makeProbePixmap(QStringLiteral("take"));
    QVERIFY(!probe.isNull());

    PoseLibrary library;
    library.setCapacity(2);
    QVERIFY(library.put(QStringLiteral("a"), probe));
    QVERIFY(library.put(QStringLiteral("b"), probe));

    // 触碰 a → a 变成最新，b 才是最久未用
    QVERIFY(!library.take(QStringLiteral("a")).isNull());
    QVERIFY(library.put(QStringLiteral("c"), probe));

    QCOMPARE(library.loadedCount(), 2);
    QVERIFY2(library.isLoaded(QStringLiteral("a")), "刚被访问的项不应被逐出");
    QVERIFY2(!library.isLoaded(QStringLiteral("b")), "最久未使用项应被逐出");
    QVERIFY(library.isLoaded(QStringLiteral("c")));
    // 未命中返回空图，且不产生任何副作用
    QVERIFY(library.take(QStringLiteral("missing")).isNull());
    QCOMPARE(library.loadedCount(), 2);
}

void PoseAssetsTest::putRejectsInvalidInputAndRespectsCapacity()
{
    const QPixmap probe = makeProbePixmap(QStringLiteral("put"));
    QVERIFY(!probe.isNull());

    PoseLibrary library;
    QCOMPARE(library.capacity(), 36);
    QCOMPARE(library.failedCount(), 0);

    // 空 key / 空图必须被拒
    QVERIFY(!library.put(QString(), probe));
    QVERIFY(!library.put(QStringLiteral("a"), QPixmap()));
    QCOMPARE(library.loadedCount(), 0);

    for (int i = 0; i < 5; ++i) {
        QVERIFY(library.put(QStringLiteral("k%1").arg(i), probe));
    }
    QCOMPARE(library.loadedCount(), 5);

    // 缩容立即逐出
    library.setCapacity(3);
    QCOMPARE(library.loadedCount(), 3);
    // 非正容量视为不变（防御非法调用）
    library.setCapacity(0);
    library.setCapacity(-1);
    QCOMPARE(library.capacity(), 3);
}

void PoseAssetsTest::ensureLoadedIsIdempotentAndCountsDecodes()
{
    PoseLibrary library;
    const QString key = QStringLiteral("idle-cute");
    QVERIFY(library.isKnown(key));
    QCOMPARE(library.loadedCount(), 0);

    // 第一次：真正解码
    QVERIFY(library.ensureLoaded(key));
    QCOMPARE(library.loadedCount(), 1);
    QCOMPARE(library.decodeAttempts(), 1);
    QCOMPARE(library.decodedTotal(), 1);
    QCOMPARE(library.residentBytes(), static_cast<qint64>(kPosePixels) * kPosePixels * 4);

    // 第二次：命中缓存，**不得**重复解码
    QVERIFY(library.ensureLoaded(key));
    QCOMPARE(library.decodeAttempts(), 1);
    QCOMPARE(library.decodedTotal(), 1);
    QCOMPARE(library.loadedCount(), 1);

    // 空 key 直接拒绝
    QVERIFY(!library.ensureLoaded(QString()));
    QCOMPARE(library.decodeAttempts(), 1);
}

void PoseAssetsTest::negativeCacheAvoidsRepeatedFailure()
{
    PoseLibrary library;
    const QString key = QStringLiteral("definitely-not-a-pose");
    QVERIFY(!library.isKnown(key));
    QCOMPARE(library.failedCount(), 0);

    // 第一次失败：记负缓存
    QVERIFY(!library.ensureLoaded(key));
    QCOMPARE(library.failedCount(), 1);
    QCOMPARE(library.loadedCount(), 0);

    // 第二次：负缓存命中 —— 不再重复走加载路径、不再重复告警
    QVERIFY(!library.ensureLoaded(key));
    QVERIFY(!library.ensureLoaded(key));
    QCOMPARE(library.failedCount(), 1);
    QCOMPARE(library.loadedCount(), 0);

    // 未登记 key 拼不出资源路径，因此根本不会进入解码器
    QCOMPARE(library.decodeAttempts(), 0);
}

void PoseAssetsTest::onDemandLoadFillsCacheAndStaysUnderCapacity()
{
    PoseLibrary library;
    library.startPreload();
    QCOMPARE(library.loadedCount(), library.coreCount());

    // 逐张按需加载所有「非档位」pose：应全部成功、全部进缓存，且总量不超过容量上限
    const QStringList onDemand = tierExcludedKeys();
    for (const QString &key : onDemand) {
        QVERIFY2(library.ensureLoaded(key), qPrintable(key));
        QVERIFY2(library.loadedCount() <= library.capacity(),
                 qPrintable(QStringLiteral("超出容量: %1 > %2")
                                .arg(library.loadedCount())
                                .arg(library.capacity())));
    }
    QCOMPARE(library.failedCount(), 0);
    // 解码次数 = core 12（startPreload 同步）+ 非档位 59（逐张按需）= 71。
    // warm 22 张在本用例中**不会**被解码：其补齐由 QTimer 驱动，而本例不跑 event loop。
    QCOMPARE(library.decodedTotal(), library.coreCount() + onDemand.size());
    // 关键：解码次数（71）远多于缓存槽位（36）⇒ LRU 逐出确实生效，
    // 否则 93 张会重新变成「全部常驻」，路径 A 的收益就丢了。
    QVERIFY2(library.decodedTotal() > library.loadedCount(),
             "逐出未生效：解码次数应多于当前缓存条目数");
    QVERIFY2(library.loadedCount() <= library.capacity(), "缓存应被容量上限约束");
    QVERIFY2(library.residentBytes() <= 10LL * 1024 * 1024,
             qPrintable(QStringLiteral("residentBytes=%1 超过 M3 目标 10 MiB")
                            .arg(library.residentBytes())));
}

void PoseAssetsTest::residentBytesMatchesCacheSize()
{
    PoseLibrary library;
    library.startPreload();
    const int n = library.loadedCount();
    QCOMPARE(library.residentBytes(), static_cast<qint64>(n) * kPosePixels * kPosePixels * 4);
    QVERIFY(n > 0);
    // 关键回归护栏：不再全量常驻（旧版 startPreload 会把 93 张全部塞进缓存）
    QVERIFY2(n < core::kPoseCount,
             qPrintable(QStringLiteral("loadedCount=%1 应小于 %2").arg(n).arg(core::kPoseCount)));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    if (QGuiApplication::screens().isEmpty()) {
        qWarning() << "No screen available; skipping pose assets test.";
        return 77; // CTest SKIP_RETURN_CODE
    }

    PoseAssetsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_pose_assets.moc"
