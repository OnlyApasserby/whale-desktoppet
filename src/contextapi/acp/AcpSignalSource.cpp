#include "contextapi/acp/AcpSignalSource.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <utility>

namespace whalepet::contextapi {

namespace {

// 未换行结尾的尾部字节上限：异常输入（如外部进程写了一半）不应无限占用内存。
constexpr int kMaxPendingBytes = 1024 * 1024;

} // namespace

AcpSignalSource::AcpSignalSource(QString sourceId, QString filePath)
    : m_sourceId(std::move(sourceId))
    , m_filePath(std::move(filePath))
{
}

QString AcpSignalSource::id() const
{
    return m_sourceId;
}

bool AcpSignalSource::available() const
{
    if (m_filePath.isEmpty()) {
        return false;
    }
    const QFileInfo info(m_filePath);
    return info.exists() && info.isFile() && info.isReadable();
}

void AcpSignalSource::setFilePath(const QString &filePath)
{
    if (m_filePath == filePath) {
        return;
    }
    m_filePath = filePath;
    m_offset = 0;
    m_pending.clear();
    m_queue.clear();
    qInfo() << "[AcpSignalSource] 信号文件已切换:" << m_filePath;
}

bool AcpSignalSource::poll(qint64 nowMs, CoreSignal &out)
{
    Q_UNUSED(nowMs);

    if (m_queue.isEmpty()) {
        ingest();
    }
    if (m_queue.isEmpty()) {
        return false;
    }

    out = m_queue.takeFirst();
    ++m_consumed;
    return true;
}

void AcpSignalSource::ingest()
{
    if (!available()) {
        return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[AcpSignalSource] 无法读取信号文件:" << m_filePath << file.errorString();
        return;
    }

    // 文件被截断 / 轮转（size 回退）：丢弃待拼尾部并从头再读，避免错位解析
    if (file.size() < m_offset) {
        qInfo() << "[AcpSignalSource] 信号文件被截断，从头读取:" << m_filePath;
        m_offset = 0;
        m_pending.clear();
    }

    if (!file.seek(m_offset)) {
        qWarning() << "[AcpSignalSource] seek 失败:" << m_filePath;
        return;
    }
    m_pending.append(file.readAll());
    m_offset = file.pos();
    file.close();

    // 逐行切分：完整行解析入队，末尾无换行片段留待下次
    int start = 0;
    while (true) {
        const int nl = m_pending.indexOf('\n', start);
        if (nl < 0) {
            break;
        }
        const QByteArray line = m_pending.mid(start, nl - start).trimmed();
        start = nl + 1;
        if (line.isEmpty()) {
            continue;
        }
        CoreSignal signal;
        if (parseLine(line, signal)) {
            m_queue.append(signal);
        }
    }
    m_pending.remove(0, start);

    if (m_pending.size() > kMaxPendingBytes) {
        qWarning() << "[AcpSignalSource] 未换行尾部超过上限，已丢弃（疑似非法输入）:" << m_filePath;
        m_pending.clear();
        ++m_ignored;
    }
}

bool AcpSignalSource::parseLine(const QByteArray &line, CoreSignal &out)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        ++m_ignored;
        qWarning() << "[AcpSignalSource] 忽略非法信号行:" << err.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString kind = obj.value(QStringLiteral("kind")).toString();
    if (kind.isEmpty()) {
        ++m_ignored;
        qWarning() << "[AcpSignalSource] 忽略缺少 kind 的信号行（不产生假信号）";
        return false;
    }

    out.sourceId = obj.value(QStringLiteral("sourceId")).toString(m_sourceId);
    out.kind = kind;
    out.payload = obj.value(QStringLiteral("payload")).toObject();
    out.atMs = static_cast<qint64>(obj.value(QStringLiteral("atMs")).toDouble(0.0));
    return true;
}

} // namespace whalepet::contextapi
