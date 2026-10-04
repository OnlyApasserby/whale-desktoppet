#include "view/PoseLibrary.h"

#include "common/PetVisuals.h"
#include "core/PoseCatalog.h"
#include "view/AssetsResource.h"

#include <QDebug>
#include <QTimer>
#include <limits>

namespace whalepet {

const QStringList &PoseLibrary::coreKeys()
{
    // core 档：**首帧 + 延迟敏感路径**，启动时同步加载，任何情况下不得有解码延迟。
    //   - idle-cute：PetWindow 构造时的默认首帧（PetWindow.cpp kDefaultPose）
    //   - pick-up  ：拖拽起始瞬间换图（延迟可感知）
    //   - 贴边 4 张：拖到边框时**立即**换图，且首次还要跑一次 contentBBox 的
    //               逐像素 alpha 扫描（65,536 次），延迟最敏感
    //   - 其余    ：状态机上下文链的常驻姿态（静息 / 深夜 / 挂机 / 工作 / 兜底）
    static const QStringList kCore{
        QStringLiteral("idle-cute"),
        QStringLiteral("waiting"),
        QStringLiteral("afk"),
        QStringLiteral("thinking"),
        QStringLiteral("curious"),
        QStringLiteral("teasing"),
        QStringLiteral("pick-up"),
        QStringLiteral("home-peek"),
        QStringLiteral("home-bottom"),
        QStringLiteral("settings-peek"),
        QStringLiteral("workbench-peek"),
        // P8 时段常驻（日间 idle-cute / 傍晚 night / 深夜 daily-pajama）与
        // 编程常驻（running）：启动即可能显示，且跨整点会**立即**换图，延迟敏感。
        // 注：P8 起深夜空闲立绘改为 daily-pajama，`sleep` 已无任何代码路径输出，
        //     故从本档移出（按需加载；避免「预载了却永不显示」的冗余）。
        QStringLiteral("night"),
        QStringLiteral("daily-pajama"),
        QStringLiteral("running"),
    };
    return kCore;
}

const QStringList &PoseLibrary::warmKeys()
{
    // warm 档：**活跃交互中可能连续切换**的姿态，启动后定时补齐。
    // 选取依据（全部有真实代码引用路径，见 docs/POSE-ASSETS.md §2.3）：
    //   - 工作态 6 张：WorkState.cpp workStatePose()，真实桌面感知会持续推进
    //   - 小游戏 5 张：GameState.cpp gameMoodPose / gameMilestonePose，游戏期间高频切换
    //   - 关键词表情 6 张：含 meme-omg（未知事件的默认兜底）与 meme-shock（濒死）
    //   - 成长 / 一次性反馈 5 张：升级、成就、任务完成
    // 刻意**不**入 warm：节日换装（一年仅几天）、分区点击反应（每次点击才触发）、
    // 长尾表情（每次聊天命中一个）——它们一律走按需加载，延迟不可感知。
    static const QStringList kWarm{
        // 工作态（WorkState.cpp workStatePose）
        QStringLiteral("work-ram"),
        QStringLiteral("work-debug"),
        QStringLiteral("work-meeting"),
        QStringLiteral("daily-gaming"),
        QStringLiteral("meme-wakuwaku"),
        QStringLiteral("work-sleep"),
        // P8 工作立绘池（work-* 共 13 张）：池每 60s 轮转一张，属
        // 「活跃工作期间连续切换」，故全部预载（其余 7 张见上）。
        QStringLiteral("work-idea"),
        QStringLiteral("work-review"),
        QStringLiteral("work-slack"),
        QStringLiteral("work-slack-phone"),
        QStringLiteral("work-celebrate"),
        QStringLiteral("work-pat"),
        // 小游戏陪玩（GameState）
        QStringLiteral("game-happy"),
        QStringLiteral("game-win"),
        QStringLiteral("game-lose"),
        QStringLiteral("game-think"),
        QStringLiteral("game-cheat"),
        // 关键词表情（含默认兜底 meme-omg）
        QStringLiteral("meme-omg"),
        QStringLiteral("meme-shock"),
        QStringLiteral("work-deadline"),
        QStringLiteral("work-boss"),
        QStringLiteral("work-deploy"),
        QStringLiteral("bold"),
        // 成长 / 一次性反馈（三连击 star 与任务成功 success 属高频交互）
        QStringLiteral("success"),
        QStringLiteral("star"),
        // 刻意**不**入 warm（一律按需加载，首次显示延迟不可感知）：
        //   - P8 预设对话的独立立绘池（meme-broke / meme-cry / meme-heart / meme-no /
        //     meme-yes）：问答为 8–15 分钟一次的低频路径；
        //   - P8 天气立绘（weather-*，5 张）：同上，且未配置彩云 key 时根本不会显示；
        //   - failure / celebrate / levelup 与长尾关键词表情：低频或极低频。
        // 这样 core 14 + warm 25 = 39 < kCacheCapacity(40)，预载完成即全部常驻、不互相逐出。
    };
    return kWarm;
}

PoseLibrary::PoseLibrary(QObject *parent)
    : QObject(parent)
{
    m_allKeys.reserve(core::kPoseCount);
    for (int i = 0; i < core::kPoseCount; ++i) {
        m_allKeys.append(QString::fromUtf8(core::kPoses[i].key));
    }
    // 档位过滤在构造期完成一次：未登记的 pose 退化为按需加载（不崩、不静默缺图）
    m_core = sanitizeTier(coreKeys(), "core");
    m_warm = sanitizeTier(warmKeys(), "warm");
}

QStringList PoseLibrary::sanitizeTier(const QStringList &declared, const char *tierName) const
{
    QStringList out;
    out.reserve(declared.size());
    for (const QString &key : declared) {
        if (!m_allKeys.contains(key)) {
            // 档位白名单写错 pose 名时**必须**可见：否则会静默少预载几张而无从察觉
            qWarning() << "[PoseLibrary] 预载档位含未登记 pose（已忽略，退化为按需加载）:"
                       << tierName << key;
            continue;
        }
        if (!out.contains(key)) {
            out.append(key);
        }
    }
    return out;
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

qint64 PoseLibrary::residentBytes() const
{
    // ARGB32 理论下限估算（与 docs/POSE-ASSETS.md 的 M3 口径一致）
    return static_cast<qint64>(m_cache.size()) * kPosePixels * kPosePixels * 4;
}

QPixmap PoseLibrary::take(const QString &key)
{
    const auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        return QPixmap();
    }
    it->lastUsed = ++m_useCounter;
    return it->pixmap;
}

bool PoseLibrary::put(const QString &key, const QPixmap &pixmap)
{
    if (key.isEmpty() || pixmap.isNull()) {
        return false;
    }
    m_failed.remove(key); // 回填即视为可用，清掉负缓存
    m_cache.insert(key, CacheEntry{pixmap, ++m_useCounter});
    evictIfNeeded();
    return true;
}

bool PoseLibrary::ensureLoaded(const QString &key)
{
    if (key.isEmpty()) {
        return false;
    }
    if (m_cache.contains(key)) {
        m_cache[key].lastUsed = ++m_useCounter;
        return true;
    }
    if (m_failed.contains(key)) {
        return false; // 负缓存命中：不再解码、不再告警
    }
    loadOne(key);
    return m_cache.contains(key);
}

void PoseLibrary::setCapacity(int newCapacity)
{
    if (newCapacity <= 0 || newCapacity == m_capacity) {
        return;
    }
    m_capacity = newCapacity;
    evictIfNeeded();
}

void PoseLibrary::evictIfNeeded()
{
    while (m_cache.size() > m_capacity) {
        QString victim;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
            if (it->lastUsed < oldest) {
                oldest = it->lastUsed;
                victim = it.key();
            }
        }
        if (victim.isEmpty()) {
            break; // 容量非正时理论上不可达；留作防御，避免死循环
        }
        m_cache.remove(victim);
    }
}

void PoseLibrary::startPreload()
{
    if (m_preloadStarted) {
        return; // 幂等：showPet 可能被重复调用
    }
    m_preloadStarted = true;

    // core 档：同步加载，保证首帧与贴边 / 拖拽等延迟敏感路径零解码延迟
    for (const QString &key : m_core) {
        loadOne(key);
    }

    // warm 档：其余进入定时补齐队列
    m_pending = m_warm;
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
    if (m_failed.contains(key)) {
        return; // 负缓存：已确认加载失败，不重复解码、不重复告警
    }

    // 静态库资源不会自动注册：库必须能**独立**完成初始化，不能依赖
    // 「main() 恰好先调过一次」这种隐式顺序（否则单测与将来的外部资源入口都会踩空）。
    whalepetInitAssetsResource();

    const QString path = resourcePath(key);
    if (path.isEmpty()) {
        m_failed.insert(key, PoseLoadError::UnknownKey);
        qWarning() << "[PoseLibrary] 未登记的 pose:" << key;
        return;
    }

    PoseLoadError error = PoseLoadError::None;
    QString detail;
    ++m_decodeAttempts;
    const QPixmap pixmap = PoseImageLoader::loadChecked(path, &error, &detail);
    if (pixmap.isNull()) {
        // 单张失败静默跳过：不阻塞预载队列。告警带**原因分类**，
        // 以便区分「未编入 qrc」「格式未授权」「数据损坏」「尺寸非 256x256」。
        m_failed.insert(key, error);
        qWarning() << "[PoseLibrary] 立绘加载失败（跳过）:" << path
                   << "reason:" << PoseImageLoader::describe(error) << detail;
        return;
    }

    put(key, pixmap);
    ++m_decodedTotal;
    emit poseReady(key);
}

} // namespace whalepet
