#include "minigame/chess/UciEngine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace whalepet {

namespace {

// 在目录（含一层子目录）内查找可执行文件；prefer 非空时优先名字含该串者。
QString findEngineExecutable(const QString &dir, const QString &prefer)
{
    QDir d(dir);
    if (!d.exists()) {
        return QString();
    }
    QStringList filters;
    filters << QStringLiteral("*.exe");
    QFileInfoList entries = d.entryInfoList(filters, QDir::Files, QDir::Name);

    const QFileInfoList subDirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &sub : subDirs) {
        entries += QDir(sub.absoluteFilePath()).entryInfoList(filters, QDir::Files, QDir::Name);
    }

    if (!prefer.isEmpty()) {
        for (const QFileInfo &fi : entries) {
            if (fi.fileName().contains(prefer, Qt::CaseInsensitive)) {
                return fi.absoluteFilePath();
            }
        }
    }
    return entries.isEmpty() ? QString() : entries.first().absoluteFilePath();
}

} // namespace

QString defaultChessEnginePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString fromEngineDir =
        findEngineExecutable(appDir + QStringLiteral("/engine"), QStringLiteral("stockfish"));
    if (!fromEngineDir.isEmpty()) {
        return fromEngineDir;
    }

    // 开发回退：沿程序目录向上查找 dummy/stockfish（仓库内本地测试用引擎）。
    QDir dir(appDir);
    for (int depth = 0; depth < 4; ++depth) {
        const QString found =
            findEngineExecutable(dir.absoluteFilePath(QStringLiteral("dummy/stockfish")),
                                 QStringLiteral("stockfish"));
        if (!found.isEmpty()) {
            return found;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

UciEngine::UciEngine(QObject *parent)
    : QObject(parent)
{
}

UciEngine::~UciEngine()
{
    stop();
}

bool UciEngine::isRunning() const
{
    return m_process != nullptr && m_process->state() == QProcess::Running;
}

bool UciEngine::start(const QString &enginePath, QString *error)
{
    stop();

    const QFileInfo info(enginePath);
    if (enginePath.isEmpty() || !info.exists() || !info.isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("引擎文件不存在：%1")
                         .arg(enginePath.isEmpty() ? QStringLiteral("(未配置)") : enginePath);
        }
        return false;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &UciEngine::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &UciEngine::onReadyReadStandardError);
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { onProcessError(); });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { onProcessFinished(); });

    m_path = info.absoluteFilePath();
    m_uciOk = false;
    m_readyOk = false;
    m_readyEmitted = false;
    m_stopping = false;
    m_buffer.clear();

    // 以引擎自身目录作为工作目录：便于引擎读取同目录的权重 / 资源文件。
    m_process->setWorkingDirectory(info.absolutePath());
    m_process->start(m_path, QStringList());
    if (!m_process->waitForStarted(5000)) {
        if (error != nullptr) {
            *error = QStringLiteral("引擎启动失败：%1").arg(m_path);
        }
        stop();
        return false;
    }

    send(QStringLiteral("uci"));
    return true;
}

void UciEngine::stop()
{
    if (m_process == nullptr) {
        return;
    }
    m_stopping = true;
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->write("quit\n");
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(1500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_path.clear();
    m_uciOk = false;
    m_readyOk = false;
    m_readyEmitted = false;
    m_buffer.clear();
}

void UciEngine::send(const QString &command)
{
    if (m_process == nullptr || m_process->state() != QProcess::Running) {
        return;
    }
    m_process->write(command.toUtf8() + '\n');
}

void UciEngine::setOption(const QString &name, const QString &value)
{
    send(QStringLiteral("setoption name %1 value %2").arg(name, value));
}

void UciEngine::newGame()
{
    send(QStringLiteral("ucinewgame"));
    send(QStringLiteral("isready"));
}

void UciEngine::setPosition(const QString &fen)
{
    // 直接下发完整 FEN：本层已维护易位权 / 过路兵 / 走子方 / 回合计数，
    // 因此无需再附带着法历史（引擎按当前局面思考即可）。
    send(QStringLiteral("position fen %1").arg(fen));
}

void UciEngine::goMoveTime(int ms)
{
    send(QStringLiteral("go movetime %1").arg(ms));
}

void UciEngine::onReadyReadStandardOutput()
{
    if (m_process == nullptr) {
        return;
    }
    m_buffer += m_process->readAllStandardOutput();
    int newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        handleLine(QString::fromUtf8(line).trimmed());
    }
}

void UciEngine::onReadyReadStandardError()
{
    if (m_process == nullptr) {
        return;
    }
    const QByteArray data = m_process->readAllStandardError();
    if (!data.isEmpty()) {
        emit logLine(QString::fromUtf8(data).trimmed());
    }
}

void UciEngine::handleLine(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }
    if (line == QStringLiteral("uciok")) {
        m_uciOk = true;
        send(QStringLiteral("isready"));
        return;
    }
    if (line == QStringLiteral("readyok")) {
        m_readyOk = true;
        if (m_uciOk && !m_readyEmitted) {
            m_readyEmitted = true; // 每次 start 只发一次，避免后续 isready 反复触发
            emit ready();
        }
        return;
    }
    if (line.startsWith(QStringLiteral("bestmove"))) {
        emit bestMove(parseBestMove(line));
        return;
    }
    emit logLine(line);
}

void UciEngine::onProcessError()
{
    if (m_stopping || m_process == nullptr) {
        return;
    }
    emit failed(m_process->errorString());
}

void UciEngine::onProcessFinished()
{
    if (m_stopping) {
        return;
    }
    emit failed(QStringLiteral("引擎进程已退出"));
}

QString UciEngine::parseBestMove(const QString &line)
{
    const QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2 || tokens.first() != QStringLiteral("bestmove")) {
        return QString();
    }
    const QString move = tokens.at(1);
    if (move == QStringLiteral("(none)") || move == QStringLiteral("0000")) {
        return QString();
    }
    return move;
}

} // namespace whalepet
