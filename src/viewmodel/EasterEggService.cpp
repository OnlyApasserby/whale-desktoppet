#include "viewmodel/EasterEggService.h"

#include "core/CodeEasterEgg.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <string>

namespace whalepet::viewmodel {

namespace {

// 扫描上限：工作区可能很大，彩蛋不值得任何可感知的卡顿，故处处设硬边界。
constexpr int kMaxDepth = 12;      // 目录递归深度上限
constexpr int kMaxFiles = 512;     // 收集的候选文件数上限
constexpr int kMaxAttempts = 64;   // 单次最多尝试改写的文件数
constexpr int kMaxVisited = 20000; // 单次扫描最多检视的目录项数（防巨大目录树卡顿）
constexpr qint64 kMaxFileBytes = 512 * 1024; // 单文件字节上限

// 跳过的目录名（小写比较）：版本控制 / 构建产物 / 依赖 / IDE 缓存
const char *const kSkipDirs[] = {
    ".git", ".svn", ".hg", ".vs", ".idea", ".vscode", ".codebuddy",
    "node_modules", "__pycache__", "build", "out", "dist", "bin", "obj",
    "cmakefiles", ".cache", "venv", ".venv", "env",
};

bool isSkippedDir(const QString &name)
{
    const QString lower = name.toLower();
    for (const char *dir : kSkipDirs) {
        if (lower == QLatin1String(dir)) {
            return true;
        }
    }
    return false;
}

// 扩展名（含点、小写）；无扩展名返回空串
QString extensionOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix.toLower();
}

// 递归收集可注入的源码文件（有界深搜；不跟随符号链接）
void collectFiles(const QString &dir, int depth, int *visited, QStringList *out)
{
    if (depth > kMaxDepth || out->size() >= kMaxFiles || *visited >= kMaxVisited) {
        return;
    }
    const QFileInfoList entries = QDir(dir).entryInfoList(
        QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &info : entries) {
        if (out->size() >= kMaxFiles || ++(*visited) > kMaxVisited) {
            return;
        }
        if (info.isSymLink()) {
            continue; // 防环
        }
        if (info.isDir()) {
            if (!isSkippedDir(info.fileName())) {
                collectFiles(info.absoluteFilePath(), depth + 1, visited, out);
            }
            continue;
        }
        if (info.size() <= 0 || info.size() > kMaxFileBytes) {
            continue;
        }
        if (!core::isCodeEggSupportedExtension(extensionOf(info.fileName()).toStdString())) {
            continue;
        }
        out->append(info.absoluteFilePath());
    }
}

} // namespace

EasterEggService::EasterEggService(QObject *parent)
    : QObject(parent)
    , m_rng(&m_ownRng)
{
}

EasterEggService::EasterEggService(core::IRandom *rng, QObject *parent)
    : QObject(parent)
    , m_rng(rng != nullptr ? rng : &m_ownRng)
{
}

EasterEggService::~EasterEggService() = default;

void EasterEggService::setEnabled(bool on)
{
    m_enabled = on;
}

void EasterEggService::setWorkspace(const QString &dir)
{
    const QString trimmed = dir.trimmed();
    if (trimmed.isEmpty()) {
        m_workspace.clear();
        return;
    }
    const QFileInfo info(trimmed);
    if (!info.exists() || !info.isDir()) {
        // 不猜测、不回落：只告警并视作「未配置」（绝不误改安装 / 数据目录）
        qWarning() << "[EasterEggService] 工作区不可用（不存在或不是目录）:" << trimmed;
        m_workspace.clear();
        return;
    }
    m_workspace = info.absoluteFilePath();
}

bool EasterEggService::roll()
{
    return m_rng->next01() < kTriggerProbability;
}

bool EasterEggService::poke()
{
    if (!m_enabled || m_workspace.isEmpty()) {
        return false;
    }
    if (!roll()) {
        return false;
    }
    return !pickAndInject().isEmpty();
}

QString EasterEggService::lastSaying() const
{
    if (!m_hasLastSaying) {
        return QString();
    }
    std::size_t count = 0;
    const char *const *pool = core::codeEggSayings(&count);
    if (count == 0) {
        return QString();
    }
    return QString::fromUtf8(pool[m_lastSayingIndex % count]);
}

QString EasterEggService::pickAndInject()
{
    QStringList files;
    int visited = 0;
    collectFiles(m_workspace, 0, &visited, &files);
    if (files.isEmpty()) {
        return QString();
    }

    std::size_t sayingCount = 0;
    core::codeEggSayings(&sayingCount);
    if (sayingCount == 0) {
        return QString();
    }

    const int fileCount = files.size();
    const int start = m_rng->nextInt(fileCount);
    const int attempts = std::min(fileCount, kMaxAttempts);

    for (int i = 0; i < attempts; ++i) {
        const QString &path = files.at((start + i) % fileCount);

        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray bytes = in.readAll();
        in.close();
        if (bytes.contains('\0')) {
            continue; // 二进制（含 UTF-16）不碰
        }

        const std::string source(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        const auto index =
            static_cast<std::size_t>(m_rng->nextInt(static_cast<int>(sayingCount)));
        const core::CodeEggResult result =
            core::injectCodeEgg(source, extensionOf(path).toStdString(), index);
        if (!result.changed) {
            continue; // 已藏过 / 没有注释段落 / 不支持 → 换下一个
        }

        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "[EasterEggService] 无法写入:" << path << out.errorString();
            continue;
        }
        const QByteArray outBytes(result.content.data(),
                                  static_cast<int>(result.content.size()));
        if (out.write(outBytes) != outBytes.size() || !out.commit()) {
            qWarning() << "[EasterEggService] 写入失败:" << path;
            continue;
        }

        m_lastFile = path;
        m_lastSayingIndex = result.sayingIndex;
        m_hasLastSaying = true;
        qInfo() << "[EasterEggService] 藏了一句俏皮话:" << path;
        emit eggPlanted(path, lastSaying());
        return path;
    }
    return QString();
}

} // namespace whalepet::viewmodel
