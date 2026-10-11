#include "viewmodel/WorkService.h"

#include "core/CodeEasterEgg.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cstddef>
#include <string>

namespace whalepet::viewmodel {

namespace {

// 计时节奏：1s 一跳（面板进度与「到点产出」共用）
constexpr int kTickIntervalMs = 1000;
// 会话时长兜底（非预设值 / 非法值）
constexpr int kDefaultDurationMinutes = 25;

std::string toStd(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString readResource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// 目标文件名：whalepet_work_<yyyyMMdd-HHmmss>.py；已存在则顺延 _2 / _3 …（**绝不覆盖**）
QString uniqueFilePath(const QDir &dir, const QString &stamp)
{
    const QString base = QStringLiteral("whalepet_work_") + stamp;
    QString candidate = dir.filePath(base + QStringLiteral(".py"));
    int suffix = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(base + QStringLiteral("_") + QString::number(suffix) + QStringLiteral(".py"));
        ++suffix;
    }
    return candidate;
}

} // namespace

QVector<int> WorkService::durationPresets()
{
    return {5, 15, 25, 45};
}

WorkService::WorkService(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kTickIntervalMs);
    QObject::connect(&m_timer, &QTimer::timeout, this, &WorkService::onSecondTick);
    reloadBlocks();
}

WorkService::WorkService(core::IRandom *rng, QObject *parent)
    : QObject(parent)
    , m_rng(rng)
{
    m_timer.setInterval(kTickIntervalMs);
    QObject::connect(&m_timer, &QTimer::timeout, this, &WorkService::onSecondTick);
    reloadBlocks();
}

WorkService::~WorkService() = default;

void WorkService::setEnabled(bool on)
{
    m_enabled = on;
}

void WorkService::setOutputDir(const QString &dir)
{
    m_outputDir = dir.trimmed();
}

void WorkService::setDurationMinutes(int minutes)
{
    const QVector<int> presets = durationPresets();
    m_durationMinutes = presets.contains(minutes) ? minutes : kDefaultDurationMinutes;
}

void WorkService::setBlocksRoot(const QString &root)
{
    if (root.isEmpty() || root == m_blocksRoot) {
        return;
    }
    m_blocksRoot = root.endsWith(QLatin1Char('/')) ? root.left(root.size() - 1) : root;
    reloadBlocks();
}

void WorkService::reloadBlocks()
{
    const QString index = readResource(m_blocksRoot + QStringLiteral("/index.txt"));
    m_blocks.clear();
    for (const core::PythonBlock &block : core::parseBlockIndex(toStd(index))) {
        m_blocks.push_back(block);
    }
}

int WorkService::elapsedSec() const
{
    return static_cast<int>(m_elapsedMs / 1000);
}

void WorkService::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

bool WorkService::start(const QStringList &blockIds, int durationMsOverride)
{
    if (running()) {
        emit failed(QStringLiteral("已经在工作了，先等这一轮做完吧"));
        return false;
    }

    // 输出目录：必须由用户显式指定且真实存在（绝不猜测目录）
    const QString dirText = m_outputDir.trimmed();
    if (dirText.isEmpty()) {
        emit failed(QStringLiteral("还没有设置产出目录（设置 → 工作台，或在工作台面板里选择）"));
        return false;
    }
    QDir dir(dirText);
    if (!dir.exists()) {
        emit failed(QStringLiteral("产出目录不存在：") + dirText);
        return false;
    }

    // 选中块：按索引顺序过滤（保证产出可复现），未知 id 直接忽略
    QStringList selected;
    for (const core::PythonBlock &block : m_blocks) {
        const QString id = QString::fromStdString(block.id);
        if (blockIds.contains(id)) {
            selected.push_back(id);
        }
    }
    if (selected.isEmpty()) {
        emit failed(QStringLiteral("还没有选择任何功能块"));
        return false;
    }

    m_selected = selected;
    m_totalSec = (durationMsOverride > 0) ? (durationMsOverride / 1000)
                                          : (m_durationMinutes * 60);
    m_elapsedMs = 0;
    m_eggArmed = false;
    m_timer.start();
    setState(State::Running);
    emit tick(0, m_totalSec);
    return true;
}

void WorkService::cancel()
{
    if (!running()) {
        return;
    }
    m_timer.stop();
    m_elapsedMs = 0;
    m_selected.clear();
    m_eggArmed = false;
    setState(State::Idle);
}

bool WorkService::produceNow()
{
    return produce();
}

void WorkService::onSecondTick()
{
    if (!running()) {
        return;
    }
    m_elapsedMs += kTickIntervalMs;
    emit tick(elapsedSec(), m_totalSec);
    if (m_totalSec > 0 && elapsedSec() >= m_totalSec) {
        produce();
    }
}

bool WorkService::onPoke()
{
    if (!running() || !m_enabled || m_eggArmed) {
        return false;
    }
    std::size_t sayingCount = 0;
    core::codeEggSayings(&sayingCount);
    if (sayingCount == 0) {
        return false;
    }
    // 每次会话**至多一次**：标记后本会话不再触发（P10 口径，见附录 D.4）
    m_eggArmed = true;
    emit stateChanged();
    return true;
}

bool WorkService::produce()
{
    if (!running()) {
        return false;
    }

    // 逐块读取正文（缺失即失败：不产出半成品脚本）
    std::vector<core::PythonBlock> blocks;
    std::vector<std::string> bodies;
    QStringList titles;
    for (const QString &id : m_selected) {
        const QString path = m_blocksRoot + QStringLiteral("/") + id + QStringLiteral(".txt");
        const QString body = readResource(path);
        if (body.isEmpty()) {
            m_timer.stop();
            setState(State::Idle);
            emit failed(QStringLiteral("功能块资源缺失：") + id);
            return false;
        }
        for (const core::PythonBlock &block : m_blocks) {
            if (QString::fromStdString(block.id) == id) {
                blocks.push_back(block);
                titles.push_back(QString::fromStdString(block.title));
                break;
            }
        }
        bodies.push_back(toStd(body));
    }
    if (blocks.empty()) {
        m_timer.stop();
        setState(State::Idle);
        emit failed(QStringLiteral("没有可用的功能块内容"));
        return false;
    }

    // 俏皮话（仅当开关开启、会话内被戳过、且本会话未插入过）
    std::string eggSaying;
    if (m_enabled && m_eggArmed) {
        std::size_t sayingCount = 0;
        const char *const *sayings = core::codeEggSayings(&sayingCount);
        if (sayings != nullptr && sayingCount > 0) {
            core::IRandom *rng = (m_rng != nullptr) ? m_rng : &m_ownRng;
            const int raw = rng->nextInt(static_cast<int>(sayingCount));
            const std::size_t index = static_cast<std::size_t>(raw) % sayingCount;
            eggSaying = sayings[index];
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    std::vector<std::string> header;
    header.push_back("WhalePet 工作台 · 自动生成（P10）");
    header.push_back(toStd(QStringLiteral("生成时间：") + now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    header.push_back(toStd(QStringLiteral("工作时长：") + QString::number(m_durationMinutes) + QStringLiteral(" 分钟")));
    header.push_back(toStd(QStringLiteral("功能块：") + titles.join(QStringLiteral("、"))));
    header.push_back("用法：python <本文件> <功能名> [参数…]；不带参数会打印可用功能");

    const QString script = QString::fromStdString(
        core::assembleScript(blocks, bodies, header, eggSaying));

    QDir dir(m_outputDir.trimmed());
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString path = uniqueFilePath(dir, stamp);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_timer.stop();
        setState(State::Idle);
        emit failed(QStringLiteral("无法写入产出目录：") + path);
        return false;
    }
    const QByteArray bytes = script.toUtf8();
    const bool wrote = (file.write(bytes) == bytes.size());
    const bool committed = wrote && file.commit();
    if (!committed) {
        m_timer.stop();
        setState(State::Idle);
        emit failed(QStringLiteral("写入失败（已放弃，不产生半成品）：") + path);
        return false;
    }

    const bool withEgg = !eggSaying.empty();
    m_lastOutputFile = path;
    m_timer.stop();
    m_elapsedMs = 0;
    m_selected.clear();
    m_eggArmed = false;
    setState(State::Idle);
    emit finished(path, withEgg);
    return true;
}

} // namespace whalepet::viewmodel
