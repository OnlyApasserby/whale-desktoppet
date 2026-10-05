// SECURITY-REVIEW.md §极端边界测试建议 3 / 4 / 5 / 6：gamestate 只读链路的资源与数值边界。
//
//   3. 桥接文件与 socket 输入 —— `RpgMakerBridgeAdapter::read`（`readFileSnapshot` /
//      `readSocketSnapshot`）：空文件 / 超大 JSON / 超长 jsonl 末行 / 仅空行 /
//      读取中途截断替换；socket 空响应 / 畸形 JSON / 无换行超大流 /
//      每隔远小于单次超时才发 1 字节的长期流。
//      预期：输入大小与**总读取时长**受限；超限 / 超时 / 截断时安全失败并关闭连接。
//
//   4. CDP 发现与 WebSocket 生命周期 —— `CdpWebSocketClient::discoverWebSocketUrl` /
//      `connectToUrl` / `evaluate`：远程主机、非 ws/wss、userinfo、非法端口、畸形
//      `webSocketDebuggerUrl`；超大 `/json` 响应、畸形 JSON、超大 WebSocket 消息、
//      错误 / 迟到响应 id、超时断连后重复连接。
//      预期：自动发现的地址**仅限本机端点**；响应有大小上限；超时 / 断连 / 重试
//      不留下活动连接或悬挂请求。
//
//   5. Profile 数值与文件边界 —— `ProfileLoader::loadFromFile` / `loadFromJson`：
//      空 / 超大文件、截断 JSON、字段类型错误、空 / 超长 chain、`maxJumps` 负数与
//      整数边界、负数 / 小数 / 大于 uint64 的偏移、极大的 `maxBytesPerRound`。
//      预期：超范围配置在适配器启动前被拒绝；不发生整数转换溢出；失败时**不留下
//      部分生效的 profile**。
//
//   6. 内存读取地址与预算边界 —— `PointerChainResolver::resolve` / `readField`、
//      `ChainSampler::sample`、`Win32GameMemoryReader::attach/read/detach`：
//      预算恰好用满及超一字节、最大跳数、空指针、`staticRoot + offset` 与
//      `pointer + offset` 溢出、部分读取、进程退出、重复 attach/detach；
//      字段值覆盖 NaN / 无穷大 / 超出整数范围的浮点值。
//      预期：溢出地址**不被读取**；预算超限失败关闭；异常数值不触发未定义转换；
//      每轮结束句柄均释放。
//
// 关键约定：被测的桥接 socket 读与 CDP 求值都是**阻塞式**的（`waitForReadyRead` /
// `QEventLoop::exec`），因此喂数据的假服务必须跑在**独立线程**里——同线程的
// `QTcpServer` 在客户端阻塞期间不会被事件循环驱动（与 docs/pitfalls/ TRAP-P7-012
// 同源的约定）。

#include <QtTest>

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "core/GameState.h"
#include "gamestate/ChainSampler.h"
#include "gamestate/CdpWebSocketClient.h"
#include "gamestate/GameProfile.h"
#include "gamestate/PointerChainResolver.h"
#include "gamestate/RpgMakerBridgeAdapter.h"
#include "gamestate/Win32GameMemoryReader.h"

#include <QFileInfo>
#include <QProcess>
#include <QWebSocket>
#include <QWebSocketServer>

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

using namespace whalepet;
using gamestate::CdpWebSocketClient;

namespace {

constexpr qint64 kMaxSnapshotBytes = gamestate::kBridgeMaxSnapshotBytes;
constexpr int kSocketTotalTimeoutMs = gamestate::kBridgeSocketTotalTimeoutMs;

// ---------------------------------------------------------------------------
// 在独立线程里跑的脚本化 TCP 服务端
// ---------------------------------------------------------------------------
class ScriptedServer : public QThread {
    Q_OBJECT
public:
    using Handler = std::function<void(QTcpSocket *socket)>;

    ScriptedServer(Handler handler, QObject *parent = nullptr)
        : QThread(parent)
        , m_handler(std::move(handler))
    {
    }

    ~ScriptedServer() override { stop(); }

    // 启动并等待监听就绪；失败返回 false
    bool startAndWait(int timeoutMs = 5000)
    {
        start();
        QElapsedTimer timer;
        timer.start();
        while (m_port == 0 && timer.elapsed() < timeoutMs) {
            if (!isRunning() && m_port == 0) {
                return false;
            }
            QThread::msleep(2);
        }
        return m_port != 0;
    }

    quint16 port() const { return m_port; }
    QAtomicInt *connectionCount() { return &m_connections; }

    void stop()
    {
        if (isRunning() && m_loop != nullptr) {
            QMetaObject::invokeMethod(m_loop, "quit", Qt::QueuedConnection);
        }
        if (!isFinished()) {
            wait(5000);
        }
    }

protected:
    void run() override
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            m_port = 0;
            return;
        }
        m_port = server.serverPort();

        QEventLoop loop;
        m_loop = &loop;
        QObject::connect(&server, &QTcpServer::newConnection, &loop, [this, &server]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                m_connections.fetchAndAddOrdered(1);
                // handler 在工作线程内执行（阻塞它是有意的：脚本化喂数据）
                m_handler(socket);
                socket->deleteLater();
            }
        });
        m_started.fetchAndAddOrdered(1);
        loop.exec();
        m_loop = nullptr;
    }

private:
    Handler m_handler;
    quint16 m_port = 0;
    QAtomicInt m_started{ 0 };
    QAtomicInt m_connections{ 0 };
    QEventLoop *m_loop = nullptr;
};

// 在工作线程里同步发一段数据
void writeAll(QTcpSocket *socket, const QByteArray &data)
{
    socket->write(data);
    socket->waitForBytesWritten(3000);
}

// ---------------------------------------------------------------------------
// 可编排的假内存：记录每次被读取的地址（用于断言「溢出地址不被读取」）
// ---------------------------------------------------------------------------
class RecordingMemoryReader final : public gamestate::IGameMemoryReader {
public:
    void reserve(std::uint64_t base, std::size_t size)
    {
        m_base = base;
        m_mem.assign(size, 0);
    }
    void putU64(std::uint64_t address, std::uint64_t value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putI32(std::uint64_t address, std::int32_t value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putF32(std::uint64_t address, float value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void putF64(std::uint64_t address, double value)
    {
        std::memcpy(&m_mem[static_cast<std::size_t>(address - m_base)], &value, sizeof(value));
    }
    void poison(std::uint64_t address) { m_poison.insert(address); }
    void setProcessAlive(bool alive) { m_alive = alive; }

    const std::vector<std::uint64_t> &reads() const { return m_reads; }
    void clearReads() { m_reads.clear(); }
    int attachCalls() const { return m_attachCalls; }
    int detachCalls() const { return m_detachCalls; }
    bool handleOpen() const { return m_handleOpen; }

    bool attach(const gamestate::GameProfile &, QString *) override
    {
        ++m_attachCalls;
        if (!m_alive) {
            return false;
        }
        m_handleOpen = true;
        return true;
    }
    void detach() override
    {
        ++m_detachCalls;
        m_handleOpen = false;
    }
    bool attached() const override { return m_handleOpen; }
    std::uint64_t moduleBase(const std::string &name) const override
    {
        return name == m_moduleName ? m_moduleBase : 0;
    }
    bool read(std::uint64_t address, void *buffer, std::size_t size) override
    {
        m_reads.push_back(address);
        if (!m_handleOpen || !m_alive || address < m_base) {
            return false;
        }
        const std::uint64_t offset = address - m_base;
        if (offset + size > m_mem.size()) {
            return false;
        }
        if (m_poison.count(address) != 0) {
            return false; // 模拟「部分读取 / 不可读页」
        }
        std::memcpy(buffer, m_mem.data() + static_cast<std::size_t>(offset), size);
        return true;
    }
    QString lastError() const override { return QStringLiteral("fake"); }
    int processId() const override { return m_handleOpen ? 4242 : 0; }

    void setModule(const std::string &name, std::uint64_t base)
    {
        m_moduleName = name;
        m_moduleBase = base;
    }

private:
    bool m_alive = true;
    bool m_handleOpen = false;
    int m_attachCalls = 0;
    int m_detachCalls = 0;
    std::uint64_t m_base = 0;
    std::vector<std::uint8_t> m_mem;
    std::vector<std::uint64_t> m_reads;
    std::map<std::string, std::uint64_t> m_modules;
    std::set<std::uint64_t> m_poison;
    std::string m_moduleName;
    std::uint64_t m_moduleBase = 0;
};

gamestate::GameFieldSpec makeField(const char *name, const char *kind,
                                   std::vector<std::uint64_t> chain)
{
    gamestate::GameFieldSpec spec;
    spec.name = name;
    spec.kind = kind;
    spec.chain = std::move(chain);
    return spec;
}

// 桥接 profile（kind=file/socket + path）
gamestate::GameProfile bridgeProfile(const QString &kind, const QString &path,
                                     const QString &format = QStringLiteral("json"))
{
    gamestate::GameProfile profile;
    profile.engine = "rpgmaker-rgss";
    QJsonObject bridge;
    bridge.insert(QStringLiteral("kind"), kind);
    bridge.insert(QStringLiteral("path"), path);
    if (!format.isEmpty()) {
        bridge.insert(QStringLiteral("format"), format);
    }
    profile.bridge = bridge;
    return profile;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

} // namespace

class GameStateBoundariesTest : public QObject {
    Q_OBJECT

private slots:
    // --- 3. 桥接文件输入 ---
    void bridgeFileRejectsEmptyOversizedAndBlankInput();
    void bridgeFileHandlesTruncatedAndReplacedInput();
    void bridgeJsonlTakesLastLineWithinLimits();
    // --- 3. 桥接 socket 输入 ---
    void bridgeSocketRejectsEmptyAndMalformed();
    void bridgeSocketRejectsUnterminatedOversizedStream();
    void bridgeSocketTimesOutOnSlowDripAndCloses();
    void bridgeSocketAcceptsFirstLineOfValidStream();
    // --- 4. CDP 发现与 WebSocket 生命周期 ---
    void cdpUrlWhitelistRejectsUntrustedEndpoints();
    void cdpDiscoveryRejectsUntrustedAndOversizedResponses();
    void cdpEvaluateIgnoresWrongIdsAndBoundsMessages();
    void cdpReconnectAfterTimeoutLeavesNoActiveConnection();
    // --- 5. Profile 数值与文件边界 ---
    void profileRejectsMalformedFiles();
    void profileRejectsOutOfRangeNumbers();
    void profileFailureLeavesNoPartialProfile();
    // --- 6. 内存读取地址与预算边界 ---
    void resolverEnforcesByteBudgetExactly();
    void resolverRejectsOverflowingAddressesWithoutReading();
    void resolverRejectsNonFiniteAndOutOfRangeFloats();
    void resolverHandlesMaxJumpsNullPointerAndPartialRead();
    void samplerRejectsOverflowingStaticRoot();
    void win32ReaderAttachDetachCycleReleasesHandles();
    void win32ReaderFailsAfterTargetProcessExits();
};

// ---------------------------------------------------------------------------
// 3. 桥接文件输入
// ---------------------------------------------------------------------------

void GameStateBoundariesTest::bridgeFileRejectsEmptyOversizedAndBlankInput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 空文件：必须明确失败（而不是把空串当 JSON 解析后「无数据可用」）
    {
        const QString path = dir.filePath(QStringLiteral("empty.json"));
        QVERIFY(writeFile(path, QByteArray()));
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY2(adapter.attach(bridgeProfile(QStringLiteral("file"), path), &error),
                 qPrintable(error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
        QVERIFY2(error.contains(QStringLiteral("为空")), qPrintable(error));
    }

    // 超过大小上限：明确失败且不把内容读进内存
    {
        const QString path = dir.filePath(QStringLiteral("huge.json"));
        QVERIFY(writeFile(path, QByteArray(kMaxSnapshotBytes + 4096, 'x')));
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(bridgeProfile(QStringLiteral("file"), path), &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
        QVERIFY2(error.contains(QStringLiteral("超过上限")), qPrintable(error));
    }

    // jsonl 只有空行 / 空白：明确失败
    {
        const QString path = dir.filePath(QStringLiteral("blank.jsonl"));
        QVERIFY(writeFile(path, QByteArray("\n   \n\t\n\n")));
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("file"), path, QStringLiteral("jsonl")), &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY2(error.contains(QStringLiteral("没有非空行")), qPrintable(error));
    }

    // 不存在的文件：明确失败
    {
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("file"), dir.filePath(QStringLiteral("nope.json"))),
            &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!error.isEmpty());
    }
}

void GameStateBoundariesTest::bridgeFileHandlesTruncatedAndReplacedInput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("state.json"));
    const QByteArray valid =
        R"({"available":true,"hp":88,"gold":1234,"level":5,"mapName":"Fort"})";
    QVERIFY(writeFile(path, valid));

    gamestate::RpgMakerBridgeAdapter adapter;
    QString error;
    QVERIFY2(adapter.attach(bridgeProfile(QStringLiteral("file"), path), &error),
             qPrintable(error));

    // 正常读一次
    {
        core::GameSample sample;
        QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
        QCOMPARE(sample.hp, 88.0);
    }

    // 读取中途被截断：安全失败（不返回陈旧数据，也不伪造）
    {
        QVERIFY(writeFile(path, valid.left(valid.size() / 2)));
        core::GameSample sample;
        sample.hp = 999.0; // 预置：确认失败时不会被「保留旧值」伪装成成功
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
        QVERIFY2(error.contains(QStringLiteral("解析失败")), qPrintable(error));
    }

    // 读取中途被替换成完全不同的内容：读到的是**新**内容
    {
        QVERIFY(writeFile(path, R"({"available":true,"hp":7,"gold":9,"level":1})"));
        core::GameSample sample;
        QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
        QCOMPARE(sample.hp, 7.0);
    }

    // 截断到只剩空白：明确失败
    {
        QVERIFY(writeFile(path, QByteArray("   \n  ")));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
    }
}

void GameStateBoundariesTest::bridgeJsonlTakesLastLineWithinLimits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("state.jsonl"));

    // 末行很长（但仍在上限内）：必须取到末行，且解析成功
    {
        const QString filler(200000, QChar('F')); // 200 KB 末行，< 1 MiB
        const QByteArray last = QStringLiteral(R"({"available":true,"hp":42,"mapName":"%1"})")
                                    .arg(filler)
                                    .toUtf8();
        QByteArray payload = "{\"available\":true,\"hp\":1}\n";
        payload += last;
        payload += '\n';
        QVERIFY(writeFile(path, payload));

        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("file"), path, QStringLiteral("jsonl")), &error));
        core::GameSample sample;
        QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
        QCOMPARE(sample.hp, 42.0);
    }

    // 末行超长（文件整体超上限）：拒绝，而不是只截一半
    {
        const QString filler(static_cast<int>(kMaxSnapshotBytes) + 8192, QChar('G'));
        const QByteArray payload =
            QStringLiteral(R"({"available":true,"hp":1,"mapName":"%1"})").arg(filler).toUtf8();
        QVERIFY(writeFile(path, payload));
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("file"), path, QStringLiteral("jsonl")), &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY2(error.contains(QStringLiteral("超过上限")), qPrintable(error));
    }
}

// ---------------------------------------------------------------------------
// 3. 桥接 socket 输入
// ---------------------------------------------------------------------------

void GameStateBoundariesTest::bridgeSocketRejectsEmptyAndMalformed()
{
    // 空响应：服务端立即关闭
    {
        ScriptedServer server([](QTcpSocket *socket) { socket->close(); });
        QVERIFY(server.startAndWait());
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("socket"),
                          QStringLiteral("127.0.0.1:%1").arg(server.port())),
            &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
        QVERIFY(!error.isEmpty());
        server.stop();
    }

    // 只有空白 + 换行：明确失败
    {
        ScriptedServer server([](QTcpSocket *socket) {
            writeAll(socket, "   \n");
            socket->waitForDisconnected(2000);
        });
        QVERIFY(server.startAndWait());
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("socket"),
                          QStringLiteral("127.0.0.1:%1").arg(server.port())),
            &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY2(error.contains(QStringLiteral("无数据")), qPrintable(error));
        server.stop();
    }

    // 畸形 JSON：解析失败，且不算作可用读数
    {
        ScriptedServer server([](QTcpSocket *socket) {
            writeAll(socket, "{ this is not json\n");
            socket->waitForDisconnected(2000);
        });
        QVERIFY(server.startAndWait());
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(
            bridgeProfile(QStringLiteral("socket"),
                          QStringLiteral("127.0.0.1:%1").arg(server.port())),
            &error));
        core::GameSample sample;
        sample.hp = 555.0;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
        QVERIFY2(error.contains(QStringLiteral("解析失败")), qPrintable(error));
        server.stop();
    }

    // 端点格式非法：不发起连接
    {
        gamestate::RpgMakerBridgeAdapter adapter;
        QString error;
        QVERIFY(adapter.attach(bridgeProfile(QStringLiteral("socket"), QStringLiteral("nope")),
                               &error));
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY2(error.contains(QStringLiteral("host:port")), qPrintable(error));
    }
}

void GameStateBoundariesTest::bridgeSocketRejectsUnterminatedOversizedStream()
{
    // 服务端持续灌数据但**永不发换行**：必须在字节上限处被拒绝
    QAtomicInt served{ 0 };
    ScriptedServer server([&served](QTcpSocket *socket) {
        const QByteArray chunk(64 * 1024, 'z'); // 不含 '\n'
        for (int i = 0; i < 64 && socket->state() == QAbstractSocket::ConnectedState; ++i) {
            socket->write(chunk);
            if (!socket->waitForBytesWritten(1000)) {
                break;
            }
            served.fetchAndAddOrdered(1);
        }
        socket->waitForDisconnected(2000);
    });
    QVERIFY(server.startAndWait());

    gamestate::RpgMakerBridgeAdapter adapter;
    QString error;
    QVERIFY(adapter.attach(
        bridgeProfile(QStringLiteral("socket"), QStringLiteral("127.0.0.1:%1").arg(server.port())),
        &error));

    core::GameSample sample;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!adapter.read(&sample, &error));
    const qint64 elapsed = timer.elapsed();
    QVERIFY(!sample.available);
    QVERIFY2(error.contains(QStringLiteral("超过上限")), qPrintable(error));
    // 不能是「等到总时长超时」才失败——字节上限必须先行生效
    QVERIFY2(elapsed < kSocketTotalTimeoutMs,
             qPrintable(QStringLiteral("期望在字节上限处快速失败，实际耗时 %1ms").arg(elapsed)));
    server.stop();
}

void GameStateBoundariesTest::bridgeSocketTimesOutOnSlowDripAndCloses()
{
    // 每 100ms 发 1 字节（**远小于**单次 1500ms 读等待）：旧实现会永远读下去
    ScriptedServer server([](QTcpSocket *socket) {
        for (int i = 0; i < 200; ++i) {
            if (socket->state() != QAbstractSocket::ConnectedState) {
                return; // 客户端已断开 ⇒ 上层确实关闭了连接
            }
            socket->write("x");
            socket->waitForBytesWritten(500);
            QThread::msleep(100);
        }
    });
    QVERIFY(server.startAndWait());

    gamestate::RpgMakerBridgeAdapter adapter;
    QString error;
    QVERIFY(adapter.attach(
        bridgeProfile(QStringLiteral("socket"), QStringLiteral("127.0.0.1:%1").arg(server.port())),
        &error));

    core::GameSample sample;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!adapter.read(&sample, &error));
    const qint64 elapsed = timer.elapsed();
    QVERIFY(!sample.available);
    QVERIFY2(error.contains(QStringLiteral("超时")), qPrintable(error));
    // 总读取时长必须受限（不得无限等待）
    QVERIFY2(elapsed >= kSocketTotalTimeoutMs - 200,
             qPrintable(QStringLiteral("过早返回，耗时 %1ms").arg(elapsed)));
    QVERIFY2(elapsed < kSocketTotalTimeoutMs + 4000,
             qPrintable(QStringLiteral("超时未被总时长上限约束，耗时 %1ms").arg(elapsed)));
    server.stop();
}

void GameStateBoundariesTest::bridgeSocketAcceptsFirstLineOfValidStream()
{
    // 合法首行 + 后续垃圾：只取首行（协议约定一行一份快照）
    ScriptedServer server([](QTcpSocket *socket) {
        writeAll(socket, R"({"available":true,"hp":11,"gold":22,"level":3})");
        socket->write("\n");
        socket->waitForBytesWritten(1000);
        socket->write("trailing garbage without newline");
        socket->waitForBytesWritten(1000);
        socket->waitForDisconnected(2000);
    });
    QVERIFY(server.startAndWait());

    gamestate::RpgMakerBridgeAdapter adapter;
    QString error;
    QVERIFY(adapter.attach(
        bridgeProfile(QStringLiteral("socket"), QStringLiteral("127.0.0.1:%1").arg(server.port())),
        &error));
    core::GameSample sample;
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QVERIFY(sample.available);
    QCOMPARE(sample.hp, 11.0);
    QCOMPARE(sample.gold, 22LL);
    QCOMPARE(sample.level, 3);
    server.stop();
}

// ---------------------------------------------------------------------------
// 4. CDP 发现与 WebSocket 生命周期
// ---------------------------------------------------------------------------

void GameStateBoundariesTest::cdpUrlWhitelistRejectsUntrustedEndpoints()
{
    // 可信：ws / wss + 回环 + 合法端口
    for (const QString &ok : { QStringLiteral("ws://127.0.0.1:9222/devtools/page/ABC"),
                               QStringLiteral("ws://localhost:9222/x"),
                               QStringLiteral("wss://127.0.0.1:9223/x"),
                               QStringLiteral("WS://127.0.0.1:9222/x") }) {
        QString error;
        QVERIFY2(CdpWebSocketClient::isTrustedDebuggerUrl(QUrl(ok), &error),
                 qPrintable(QStringLiteral("%1 应被信任：%2").arg(ok, error)));
    }

    // 不可信：远程主机 / 非 ws 协议 / userinfo / 非法端口 / 相对地址
    const QStringList bad = {
        QStringLiteral("ws://evil.example.com:9222/devtools/page/ABC"),
        QStringLiteral("ws://192.168.1.10:9222/x"),
        QStringLiteral("wss://evil.example.com/x"),
        QStringLiteral("http://127.0.0.1:9222/x"),
        QStringLiteral("file:///C:/x"),
        QStringLiteral("ws://user@127.0.0.1:9222/x"),
        QStringLiteral("ws://127.0.0.1:0/x"),
        QStringLiteral("ws://127.0.0.1:99999/x"),
        QStringLiteral("/devtools/page/ABC"),
        QString(),
    };
    for (const QString &url : bad) {
        QString error;
        QVERIFY2(!CdpWebSocketClient::isTrustedDebuggerUrl(QUrl(url), &error),
                 qPrintable(QStringLiteral("%1 应被拒绝").arg(url)));
        QVERIFY(!error.isEmpty());
    }
}

void GameStateBoundariesTest::cdpDiscoveryRejectsUntrustedAndOversizedResponses()
{
    // 假 `/json` 服务：返回给定的原始响应体
    const auto serve = [](const QByteArray &body) {
        auto *server = new ScriptedServer([body](QTcpSocket *socket) {
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
            response += "Content-Length: ";
            response += QByteArray::number(body.size());
            response += "\r\nConnection: close\r\n\r\n";
            response += body;
            writeAll(socket, response);
            socket->waitForDisconnected(2000);
        });
        return server;
    };

    // 远程主机 + 非 ws 协议 + userinfo + 非法端口 + 畸形 URL 全部被跳过，
    // 只有合规的本机 page 目标被采用
    {
        QJsonArray targets;
        const auto add = [&targets](const char *type, const char *url) {
            QJsonObject target;
            target.insert(QStringLiteral("type"), QString::fromLatin1(type));
            target.insert(QStringLiteral("webSocketDebuggerUrl"), QString::fromLatin1(url));
            targets.append(target);
        };
        add("page", "ws://evil.example.com:9222/devtools/page/X");
        add("page", "http://127.0.0.1:9222/devtools/page/X");
        add("page", "ws://user@127.0.0.1:9222/devtools/page/X");
        add("page", "ws://127.0.0.1:0/devtools/page/X");
        add("page", "ws://127.0.0.1:99999/devtools/page/X");
        add("page", "ws://[::1/devtools/page/X");           // 畸形 URL
        add("service_worker", "ws://127.0.0.1:9222/devtools/worker/Y");
        add("page", "ws://127.0.0.1:9222/devtools/page/GOOD");

        ScriptedServer *server = serve(QJsonDocument(targets).toJson(QJsonDocument::Compact));
        QVERIFY(server->startAndWait());
        QString url;
        QString error;
        QVERIFY2(CdpWebSocketClient::discoverWebSocketUrl(server->port(), &url, &error, 5000),
                 qPrintable(error));
        QCOMPARE(url, QStringLiteral("ws://127.0.0.1:9222/devtools/page/GOOD"));
        server->stop();
        delete server;
    }

    // 全部不可信：明确失败，并在错误里说明「已拒绝 N 个」
    {
        QJsonArray targets;
        QJsonObject target;
        target.insert(QStringLiteral("type"), QStringLiteral("page"));
        target.insert(QStringLiteral("webSocketDebuggerUrl"),
                      QStringLiteral("ws://evil.example.com:9222/devtools/page/X"));
        targets.append(target);
        ScriptedServer *server = serve(QJsonDocument(targets).toJson(QJsonDocument::Compact));
        QVERIFY(server->startAndWait());
        QString url;
        QString error;
        QVERIFY(!CdpWebSocketClient::discoverWebSocketUrl(server->port(), &url, &error, 5000));
        QVERIFY2(error.contains(QStringLiteral("不可信")), qPrintable(error));
        server->stop();
        delete server;
    }

    // 畸形 JSON / 非数组根：明确失败
    {
        ScriptedServer *server = serve("{ not json at all ");
        QVERIFY(server->startAndWait());
        QString url;
        QString error;
        QVERIFY(!CdpWebSocketClient::discoverWebSocketUrl(server->port(), &url, &error, 5000));
        QVERIFY2(error.contains(QStringLiteral("JSON")), qPrintable(error));
        server->stop();
        delete server;
    }
    {
        ScriptedServer *server = serve(R"({"webSocketDebuggerUrl":"ws://127.0.0.1:9222/x"})");
        QVERIFY(server->startAndWait());
        QString url;
        QString error;
        QVERIFY(!CdpWebSocketClient::discoverWebSocketUrl(server->port(), &url, &error, 5000));
        QVERIFY2(error.contains(QStringLiteral("JSON 数组")), qPrintable(error));
        server->stop();
        delete server;
    }

    // 超大 `/json` 响应：按大小上限中止，而不是全量读进内存
    {
        ScriptedServer *server = serve(QByteArray(CdpWebSocketClient::kMaxDiscoveryBytes + 8192,
                                                  'x'));
        QVERIFY(server->startAndWait());
        QString url;
        QString error;
        QVERIFY(!CdpWebSocketClient::discoverWebSocketUrl(server->port(), &url, &error, 8000));
        QVERIFY2(error.contains(QStringLiteral("超过上限")), qPrintable(error));
        server->stop();
        delete server;
    }
}

void GameStateBoundariesTest::cdpEvaluateIgnoresWrongIdsAndBoundsMessages()
{
    // 未连接时求值：明确失败
    {
        CdpWebSocketClient client;
        QJsonValue value;
        QString error;
        QVERIFY(!client.evaluate(QStringLiteral("1+1"), &value, &error, 200));
        QVERIFY2(error.contains(QStringLiteral("未连接")), qPrintable(error));
        QVERIFY(!client.connected());
    }

    // 连到没人监听的端口：失败且不留下「看起来已连接」的状态；失败后仍可重试
    {
        CdpWebSocketClient client;
        QString error;
        QVERIFY(!client.connectToUrl(QStringLiteral("ws://127.0.0.1:1/x"), &error, 1000));
        QVERIFY(!client.connected());
        QVERIFY(!error.isEmpty());
        QVERIFY(!client.connectToUrl(QStringLiteral("ws://127.0.0.1:2/x"), &error, 1000));
        QVERIFY(!client.connected());
    }

    // 非可信端点：连都不连（远程主机 / 非 ws / userinfo / 非法端口）
    for (const QString &url : { QStringLiteral("ws://evil.example.com:9222/x"),
                                QStringLiteral("http://127.0.0.1:9222/x"),
                                QStringLiteral("ws://user@127.0.0.1:9222/x"),
                                QStringLiteral("ws://127.0.0.1:0/x") }) {
        CdpWebSocketClient client;
        QString error;
        QVERIFY2(!client.connectToUrl(url, &error, 500), qPrintable(url));
        QVERIFY(!client.connected());
        QVERIFY(!error.isEmpty());
    }

    // 假 CDP WebSocket 服务（与被测客户端同线程：客户端内部是嵌套事件循环，
    // 因此同线程的 QWebSocketServer 能被正常驱动）
    enum class Mode { WrongIdThenCorrect, NeverReply, Oversized, Error, ExceptionDetails };
    Mode mode = Mode::NeverReply;
    QWebSocketServer server(QStringLiteral("fake-cdp"), QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QList<QWebSocket *> peers;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&server, &peers, &mode]() {
        while (QWebSocket *peer = server.nextPendingConnection()) {
            peers.append(peer);
            QObject::connect(peer, &QWebSocket::textMessageReceived, peer,
                             [peer, &mode](const QString &text) {
                                 const int id =
                                     QJsonDocument::fromJson(text.toUtf8())
                                         .object()
                                         .value(QStringLiteral("id"))
                                         .toInt(-1);
                                 const auto send = [peer](const QJsonObject &obj) {
                                     peer->sendTextMessage(QString::fromUtf8(
                                         QJsonDocument(obj).toJson(QJsonDocument::Compact)));
                                 };
                                 const auto okResponse = [id](int replyId, const QJsonValue &v) {
                                     QJsonObject inner;
                                     inner.insert(QStringLiteral("type"), QStringLiteral("number"));
                                     inner.insert(QStringLiteral("value"), v);
                                     QJsonObject result;
                                     result.insert(QStringLiteral("result"), inner);
                                     QJsonObject response;
                                     response.insert(QStringLiteral("id"), replyId);
                                     response.insert(QStringLiteral("result"), result);
                                     return response;
                                 };
                                 switch (mode) {
                                 case Mode::WrongIdThenCorrect:
                                     // 先回一个**错误 id**（引擎事件 / 过期响应），再回正确的
                                     send(okResponse(id + 1000, 999));
                                     QTimer::singleShot(200, peer,
                                                        [peer, okResponse, id]() mutable {
                                                            peer->sendTextMessage(
                                                                QString::fromUtf8(
                                                                    QJsonDocument(
                                                                        okResponse(id, 42))
                                                                        .toJson(
                                                                            QJsonDocument::Compact)));
                                                        });
                                     break;
                                 case Mode::NeverReply:
                                     break;
                                 case Mode::Oversized: {
                                     // 超大消息：客户端必须按上限中止，而不是全量收下
                                     QString huge(CdpWebSocketClient::kMaxMessageBytes + 4096,
                                                  QLatin1Char('x'));
                                     peer->sendTextMessage(huge);
                                     break;
                                 }
                                 case Mode::Error: {
                                     QJsonObject error;
                                     error.insert(QStringLiteral("message"),
                                                  QStringLiteral("boom"));
                                     QJsonObject response;
                                     response.insert(QStringLiteral("id"), id);
                                     response.insert(QStringLiteral("error"), error);
                                     send(response);
                                     break;
                                 }
                                 case Mode::ExceptionDetails: {
                                     QJsonObject details;
                                     details.insert(QStringLiteral("text"),
                                                    QStringLiteral("ReferenceError"));
                                     QJsonObject result;
                                     result.insert(QStringLiteral("exceptionDetails"), details);
                                     QJsonObject response;
                                     response.insert(QStringLiteral("id"), id);
                                     response.insert(QStringLiteral("result"), result);
                                     send(response);
                                     break;
                                 }
                                 }
                             });
        }
    });

    const QString url = QStringLiteral("ws://127.0.0.1:%1/devtools/page/T").arg(server.serverPort());

    // 错误 / 迟到 id 被忽略，正确 id 的响应仍被接受
    {
        mode = Mode::WrongIdThenCorrect;
        CdpWebSocketClient client;
        QString error;
        QVERIFY2(client.connectToUrl(url, &error, 3000), qPrintable(error));
        QJsonValue value;
        QVERIFY2(client.evaluate(QStringLiteral("40+2"), &value, &error, 3000), qPrintable(error));
        QCOMPARE(value.toInt(), 42); // 绝不是被忽略前那条 999
    }

    // 永不回应：超时失败，且不留下悬挂请求
    {
        mode = Mode::NeverReply;
        CdpWebSocketClient client;
        QString error;
        QVERIFY2(client.connectToUrl(url, &error, 3000), qPrintable(error));
        QJsonValue value;
        QVERIFY(!client.evaluate(QStringLiteral("1+1"), &value, &error, 400));
        QVERIFY2(error.contains(QStringLiteral("超时")), qPrintable(error));
    }

    // 超大消息：按上限中止，并断开连接（不留活动连接）
    {
        mode = Mode::Oversized;
        CdpWebSocketClient client;
        QString error;
        QVERIFY2(client.connectToUrl(url, &error, 3000), qPrintable(error));
        QVERIFY(client.connected());
        QJsonValue value;
        QVERIFY(!client.evaluate(QStringLiteral("1+1"), &value, &error, 8000));
        QVERIFY2(error.contains(QStringLiteral("超过上限")), qPrintable(error));
        QVERIFY2(!client.connected(), "超大消息后必须断开，不得留下活动连接");
        // 中止后仍可重新连接并正常工作
        mode = Mode::WrongIdThenCorrect;
        QVERIFY2(client.connectToUrl(url, &error, 3000), qPrintable(error));
        QVERIFY(client.evaluate(QStringLiteral("40+2"), &value, &error, 3000));
        QCOMPARE(value.toInt(), 42);
        client.close();
    }

    // 引擎报错 / 求值异常：一律如实失败（不伪造数据）
    {
        mode = Mode::Error;
        CdpWebSocketClient client;
        QString error;
        QVERIFY2(client.connectToUrl(url, &error, 3000), qPrintable(error));
        QJsonValue value;
        QVERIFY(!client.evaluate(QStringLiteral("1+1"), &value, &error, 3000));
        QVERIFY2(error.contains(QStringLiteral("boom")), qPrintable(error));
        client.close();

        mode = Mode::ExceptionDetails;
        CdpWebSocketClient client2;
        QVERIFY2(client2.connectToUrl(url, &error, 3000), qPrintable(error));
        QVERIFY(!client2.evaluate(QStringLiteral("1+1"), &value, &error, 3000));
        QVERIFY2(error.contains(QStringLiteral("exceptionDetails")), qPrintable(error));
        client2.close();
    }

    qDeleteAll(peers);
    peers.clear();
    server.close();
}

void GameStateBoundariesTest::cdpReconnectAfterTimeoutLeavesNoActiveConnection()
{
    // 必须用**真实的 QWebSocketServer**：QWebSocket 需要完成 HTTP Upgrade 握手，
    // 裸 TCP 服务端只会让握手本身超时（测不到「连上之后」的断连/超时路径）。
    enum class Mode { NeverReply, CloseAfterHandshake };
    Mode mode = Mode::NeverReply;

    QWebSocketServer server(QStringLiteral("fake-cdp-silent"), QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QList<QWebSocket *> peers;
    QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&server, &peers, &mode]() {
        while (QWebSocket *peer = server.nextPendingConnection()) {
            peers.append(peer);
            if (mode == Mode::CloseAfterHandshake) {
                peer->close(); // 模拟引擎崩溃：握手成功后立刻断开
            }
            // NeverReply：保持连接但永不回一句话 ⇒ 求值必然超时
        }
    });

    const QString url =
        QStringLiteral("ws://127.0.0.1:%1/devtools/page/SILENT").arg(server.serverPort());

    // 反复「连接 → 超时 → 关闭」：每轮之后都不得留下活动连接或悬挂请求
    for (int round = 0; round < 3; ++round) {
        CdpWebSocketClient client;
        QString error;
        QVERIFY2(client.connectToUrl(url, &error, 3000), qPrintable(error));
        QVERIFY(client.connected());

        QJsonValue value;
        QVERIFY(!client.evaluate(QStringLiteral("1+1"), &value, &error, 400));
        QVERIFY2(error.contains(QStringLiteral("超时")), qPrintable(error));
        // 超时后不得留下悬挂请求：再求值仍然只是「超时」，不会错拿上一轮的结果
        QVERIFY(!client.evaluate(QStringLiteral("2+2"), &value, &error, 400));
        QVERIFY2(error.contains(QStringLiteral("超时")), qPrintable(error));
        QVERIFY2(!client.lastError().isEmpty(), "超时必须留下可诊断的错误文本");

        client.close();
        QVERIFY(!client.connected());
        client.close(); // close() 幂等
        QVERIFY(!client.connected());
    }

    // 握手后被对端断开：不得把「已断开」误报成「仍连接」，也不得崩
    mode = Mode::CloseAfterHandshake;
    for (int round = 0; round < 3; ++round) {
        CdpWebSocketClient client;
        QString error;
        // 连接可能成功（随后被断开）或直接失败，两种都必须干净收场
        if (client.connectToUrl(url, &error, 3000)) {
            QJsonValue value;
            QVERIFY(!client.evaluate(QStringLiteral("1+1"), &value, &error, 2000));
        }
        QVERIFY2(!client.connected(), "对端断开后不得仍报告为已连接");
        client.close();
        QVERIFY(!client.connected());
    }

    qDeleteAll(peers);
    peers.clear();
    server.close();
}

// ---------------------------------------------------------------------------
// 5. Profile 数值与文件边界
// ---------------------------------------------------------------------------

void GameStateBoundariesTest::profileRejectsMalformedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    gamestate::GameProfile profile;
    QString error;

    // 空文件
    {
        const QString path = dir.filePath(QStringLiteral("empty.json"));
        QVERIFY(writeFile(path, QByteArray()));
        QVERIFY(!gamestate::ProfileLoader::loadFromFile(path, &profile, &error));
        QVERIFY(!error.isEmpty());
    }

    // 截断 JSON
    {
        const QString path = dir.filePath(QStringLiteral("truncated.json"));
        QVERIFY(writeFile(path, R"({"engine":"generic","process":"a.exe")"));
        QVERIFY(!gamestate::ProfileLoader::loadFromFile(path, &profile, &error));
        QVERIFY2(error.contains(QStringLiteral("解析失败")), qPrintable(error));
    }

    // 根节点不是对象
    {
        const QString path = dir.filePath(QStringLiteral("array.json"));
        QVERIFY(writeFile(path, R"([1,2,3])"));
        QVERIFY(!gamestate::ProfileLoader::loadFromFile(path, &profile, &error));
    }

    // 不存在的路径
    QVERIFY(!gamestate::ProfileLoader::loadFromFile(dir.filePath(QStringLiteral("nope.json")),
                                                    &profile, &error));
    QVERIFY(!error.isEmpty());

    // 字段类型错误：fields 不是数组 / 元素不是对象 / name 为空 / kind 非法
    const auto rejectJson = [&](const QByteArray &json) {
        error.clear();
        return !gamestate::ProfileLoader::loadFromJson(
            QJsonDocument::fromJson(json).object(), &profile, &error);
    };
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe","fields":{}})"));
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe","fields":[1]}"));
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "fields":[{"name":"","kind":"int32","chain":["0x1"]}]})"));
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "fields":[{"name":"hp","kind":"int32","chain":"0x1"}]})"));
    // 内存通道缺 chain：必须在适配器启动前被拒绝
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "fields":[{"name":"hp","kind":"int32"}]})"));
    // 超长 chain（跳数超过 maxJumps）
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "validation":{"maxJumps":2},
        "fields":[{"name":"hp","kind":"int32","chain":["0x1","0x2","0x3","0x4"]}]})"));
    // 空 chain 数组（元素缺失）
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "fields":[{"name":"hp","kind":"int32","chain":[]}]})"));
}

void GameStateBoundariesTest::profileRejectsOutOfRangeNumbers()
{
    gamestate::GameProfile profile;
    QString error;
    const auto rejectJson = [&](const QByteArray &json) {
        error.clear();
        return !gamestate::ProfileLoader::loadFromJson(
            QJsonDocument::fromJson(json).object(), &profile, &error);
    };
    const QByteArray prefix = R"({"engine":"generic","process":"a.exe","module":"a.exe",)";
    const QByteArray suffix = R"("fields":[{"name":"hp","kind":"int32","chain":["0x1"]}]})";

    // maxJumps：负数 / 小数 / 超上限
    QVERIFY(rejectJson(prefix + R"("validation":{"maxJumps":-1},)" + suffix));
    QVERIFY(rejectJson(prefix + R"("validation":{"maxJumps":1.5},)" + suffix));
    QVERIFY(rejectJson(prefix
                       + R"("validation":{"maxJumps":)" + QByteArray::number(1000000) + R"(},)"
                       + suffix));
    // 上限边界本身允许
    {
        error.clear();
        QVERIFY(gamestate::ProfileLoader::loadFromJson(
            QJsonDocument::fromJson(prefix
                                    + R"("validation":{"maxJumps":)"
                                    + QByteArray::number(gamestate::kMaxProfileJumps) + R"(},)"
                                    + suffix)
                .object(),
            &profile, &error));
        QCOMPARE(profile.validation.maxJumps, gamestate::kMaxProfileJumps);
    }

    // 偏移：负数 / 小数 / 超过 uint64
    const auto badChain = [&](const QByteArray &chainValue) {
        return rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",)"
                          R"("fields":[{"name":"hp","kind":"int32","chain":[)"
                          + chainValue + R"(]}]})");
    };
    QVERIFY(badChain("-1"));
    QVERIFY(badChain("1.5"));
    QVERIFY(badChain("1e30"));   // > uint64
    QVERIFY(badChain("18446744073709551616")); // 恰为 2^64
    QVERIFY(badChain(R"("not-a-number")"));
    QVERIFY(badChain("[]"));

    // moduleBaseOffset / magicOffset / magic 同样受约束
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "moduleBaseOffset":-8,"fields":[{"name":"hp","kind":"int32","chain":["0x1"]}]})"));
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "validation":{"magicOffset":1e30},"fields":[{"name":"hp","kind":"int32","chain":["0x1"]}]})"));
    QVERIFY(rejectJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
        "validation":{"magic":1e30},"fields":[{"name":"hp","kind":"int32","chain":["0x1"]}]})"));

    // maxBytesPerRound：0 / 负数 / 小数 / 极大值
    const auto badBudget = [&](const QByteArray &value) {
        return rejectJson(prefix + R"("maxBytesPerRound":)" + value + "," + suffix);
    };
    QVERIFY(badBudget("0"));
    QVERIFY(badBudget("-4096"));
    QVERIFY(badBudget("4096.5"));
    QVERIFY(badBudget("1e30"));
    QVERIFY(badBudget(QString::number(gamestate::kMaxProfileBytesPerRound + 1).toUtf8()));
    // 上限边界本身允许
    {
        error.clear();
        QVERIFY(gamestate::ProfileLoader::loadFromJson(
            QJsonDocument::fromJson(prefix
                                    + R"("maxBytesPerRound":)"
                                    + QByteArray::number(gamestate::kMaxProfileBytesPerRound)
                                    + "," + suffix)
                .object(),
            &profile, &error));
        QCOMPARE(profile.maxBytesPerRound, gamestate::kMaxProfileBytesPerRound);
    }
}

void GameStateBoundariesTest::profileFailureLeavesNoPartialProfile()
{
    // 失败时输出档案必须**原封不动**（不得留下「部分生效」的 profile）
    gamestate::GameProfile profile;
    profile.engine = "sentinel-engine";
    profile.maxBytesPerRound = 12345;
    profile.fields.push_back(makeField("sentinel", "int32", { 0xABCD }));
    const gamestate::GameProfile before = profile;

    QString error;
    // 中途失败：第二个字段的 chain 非法 ⇒ 第一个字段也不得生效
    QVERIFY(!gamestate::ProfileLoader::loadFromJson(
        QJsonDocument::fromJson(R"({"engine":"generic","process":"a.exe","module":"a.exe",
            "fields":[{"name":"ok","kind":"int32","chain":["0x1"]},
                      {"name":"bad","kind":"int32","chain":[-5]}]})")
            .object(),
        &profile, &error));
    QCOMPARE(QString::fromStdString(profile.engine), QString::fromStdString(before.engine));
    QCOMPARE(profile.maxBytesPerRound, before.maxBytesPerRound);
    QCOMPARE(profile.fields.size(), before.fields.size());
    QCOMPARE(profile.fields.front().chain.front(), before.fields.front().chain.front());
    QVERIFY(!error.isEmpty());
}

// ---------------------------------------------------------------------------
// 6. 内存读取地址与预算边界
// ---------------------------------------------------------------------------

void GameStateBoundariesTest::resolverEnforcesByteBudgetExactly()
{
    const std::uint64_t base = 0x50000000ULL;
    RecordingMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.attach({}, nullptr);
    reader.putU64(base + 0x100, base + 0x200);
    reader.putI32(base + 0x210, 1234);

    gamestate::PointerChainResolver resolver(&reader);
    QString error;
    gamestate::GameFieldValue value;

    // 单级字段读 int32：恰好 4 字节
    resolver.setMaxBytesPerRound(4);
    resolver.resetRoundBudget();
    QVERIFY2(resolver.readField(makeField("hp", "int32", { 0x210 }), base, &value, &error),
             qPrintable(error));
    QCOMPARE(value.integer, 1234LL);
    QCOMPARE(resolver.bytesThisRound(), std::uint64_t(4));

    // 预算已用满：再来一次必须失败
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x210 }), base, &value, &error));
    QVERIFY2(error.contains(QStringLiteral("预算")), qPrintable(error));
    QCOMPARE(resolver.bytesThisRound(), std::uint64_t(4)); // 失败不计入消耗

    // 超一字节的预算：4 字节请求放行，5 字节预算不足
    resolver.setMaxBytesPerRound(3);
    resolver.resetRoundBudget();
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x210 }), base, &value, &error));

    // 两级链（8 字节指针 + 4 字节值 = 12）：预算 11 失败、12 通过
    resolver.setMaxBytesPerRound(11);
    resolver.resetRoundBudget();
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x100, 0x10 }), base, &value, &error));
    resolver.setMaxBytesPerRound(12);
    resolver.resetRoundBudget();
    QVERIFY2(resolver.readField(makeField("hp", "int32", { 0x100, 0x10 }), base, &value, &error),
             qPrintable(error));
    QCOMPARE(value.integer, 1234LL);
    QCOMPARE(resolver.bytesThisRound(), std::uint64_t(12));

    // utf16 字段固定 128 字节：预算 127 失败、128 通过
    resolver.setMaxBytesPerRound(127);
    resolver.resetRoundBudget();
    QVERIFY(!resolver.readField(makeField("name", "utf16", { 0x300 }), base, &value, &error));
    resolver.setMaxBytesPerRound(128);
    resolver.resetRoundBudget();
    QVERIFY2(resolver.readField(makeField("name", "utf16", { 0x300 }), base, &value, &error),
             qPrintable(error));
}

void GameStateBoundariesTest::resolverRejectsOverflowingAddressesWithoutReading()
{
    const std::uint64_t base = 0x60000000ULL;
    RecordingMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.attach({}, nullptr);
    // 一个「野」指针：加上偏移必然溢出 uint64
    reader.putU64(base + 0x100, 0xFFFFFFFFFFFFFF00ULL);

    gamestate::PointerChainResolver resolver(&reader);
    QString error;
    std::uint64_t address = 0;
    gamestate::GameFieldValue value;

    // 1) staticRoot + chain[0] 溢出
    reader.clearReads();
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0xFFFFFFFFFFFFFFF0ULL }), 0x100,
                              &address, &error));
    QVERIFY2(error.contains(QStringLiteral("溢出")), qPrintable(error));
    QCOMPARE(reader.reads().size(), std::size_t(0)); // 溢出前不得发起任何读取

    // 2) pointer + chain[i] 溢出（第一跳读指针成功，第二跳地址溢出）
    reader.clearReads();
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0x100, 0x200 }), base, &address, &error));
    QVERIFY2(error.contains(QStringLiteral("溢出")), qPrintable(error));
    QCOMPARE(reader.reads().size(), std::size_t(1)); // 只读了第一跳的指针，未读溢出地址
    QCOMPARE(reader.reads().front(), base + 0x100);

    // 3) readField 路径同样不得读溢出地址
    reader.clearReads();
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x100, 0x200 }), base, &value, &error));
    QVERIFY2(error.contains(QStringLiteral("溢出")), qPrintable(error));
    for (std::uint64_t readAt : reader.reads()) {
        QVERIFY2(readAt < 0xFFFFFFFFFFFFFF00ULL, "绝不允许读取溢出后的地址");
    }

    // 4) 魔数偏移溢出
    gamestate::GameProfile::Validation validation;
    validation.hasMagic = true;
    validation.magicOffset = 0xFFFFFFFFFFFFFFF0ULL;
    validation.magic = 0x11223344;
    reader.clearReads();
    QVERIFY(!resolver.verifyMagic(0x100, validation, &error));
    QVERIFY2(error.contains(QStringLiteral("溢出")), qPrintable(error));
    QCOMPARE(reader.reads().size(), std::size_t(0));
}

void GameStateBoundariesTest::resolverRejectsNonFiniteAndOutOfRangeFloats()
{
    const std::uint64_t base = 0x70000000ULL;
    RecordingMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.attach({}, nullptr);

    const float nanF = std::numeric_limits<float>::quiet_NaN();
    const float infF = std::numeric_limits<float>::infinity();
    const double infD = std::numeric_limits<double>::infinity();
    const double hugeD = 1e300;   // 远超 int64
    const double negHugeD = -1e300;
    const double okD = -1234.75;  // 正常值

    reader.putF32(base + 0x10, nanF);
    reader.putF32(base + 0x20, infF);
    reader.putF32(base + 0x30, -infF);
    reader.putF64(base + 0x40, infD);
    reader.putF64(base + 0x50, hugeD);
    reader.putF64(base + 0x60, negHugeD);
    reader.putF64(base + 0x70, okD);

    gamestate::PointerChainResolver resolver(&reader);
    QString error;
    gamestate::GameFieldValue value;

    // NaN / ±Inf / 超范围：一律明确失败（float/double → 整数是 UB，必须先判定）
    for (std::uint64_t offset : { std::uint64_t(0x10), std::uint64_t(0x20), std::uint64_t(0x30) }) {
        error.clear();
        QVERIFY(!resolver.readField(makeField("f", "float", { offset }), base, &value, &error));
        QVERIFY(!error.isEmpty());
    }
    for (std::uint64_t offset : { std::uint64_t(0x40), std::uint64_t(0x50),
                                  std::uint64_t(0x60) }) {
        error.clear();
        QVERIFY(!resolver.readField(makeField("d", "double", { offset }), base, &value, &error));
        QVERIFY(!error.isEmpty());
    }

    // 范围内的正常值照常读出（负小数也必须能正确截断为整数）
    error.clear();
    QVERIFY2(resolver.readField(makeField("d", "double", { 0x70 }), base, &value, &error),
             qPrintable(error));
    QVERIFY(qAbs(value.number - okD) < 1e-9);
    QCOMPARE(value.integer, -1234LL);
}

void GameStateBoundariesTest::resolverHandlesMaxJumpsNullPointerAndPartialRead()
{
    const std::uint64_t base = 0x80000000ULL;
    RecordingMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.attach({}, nullptr);
    reader.putU64(base + 0x100, base + 0x200);
    reader.putU64(base + 0x300, 0); // 空指针
    reader.poison(base + 0x400);      // 不可读（部分读取）

    gamestate::PointerChainResolver resolver(&reader);
    QString error;
    std::uint64_t address = 0;
    gamestate::GameFieldValue value;

    // 跳数边界：恰好等于上限通过，超一被拒
    resolver.setMaxJumps(1);
    QVERIFY2(resolver.resolve(makeField("hp", "int32", { 0x100, 0x0 }), base, &address, &error),
             qPrintable(error));
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0x100, 0x0, 0x0 }), base, &address,
                              &error));
    QVERIFY2(error.contains(QStringLiteral("上限")), qPrintable(error));

    // 空指针：明确失败
    resolver.setMaxJumps(4);
    error.clear();
    QVERIFY(!resolver.resolve(makeField("hp", "int32", { 0x300, 0x0 }), base, &address, &error));
    QVERIFY2(error.contains(QStringLiteral("空指针")), qPrintable(error));

    // 部分读取（不可读页）：失败关闭，不返回陈旧数据
    error.clear();
    value.number = 42.0;
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x400 }), base, &value, &error));
    QVERIFY(!error.isEmpty());

    // 进程已退出：读取一律失败，且不会崩
    reader.setProcessAlive(false);
    reader.detach();
    error.clear();
    QVERIFY(!reader.attached());
    QVERIFY(!resolver.readField(makeField("hp", "int32", { 0x210 }), base, &value, &error));
    QVERIFY(!error.isEmpty());

    // 重复 attach/detach：句柄成对释放
    const int attachesBefore = reader.attachCalls();
    const int detachesBefore = reader.detachCalls();
    for (int i = 0; i < 5; ++i) {
        reader.setProcessAlive(true);
        QVERIFY(reader.attach({}, nullptr));
        QVERIFY(reader.handleOpen());
        reader.detach();
        QVERIFY(!reader.handleOpen());
    }
    QCOMPARE(reader.attachCalls() - attachesBefore, 5);
    QCOMPARE(reader.detachCalls() - detachesBefore, 5);
    QVERIFY(!reader.handleOpen());

    // 析构前最后一次 detach 必须把句柄关掉
    reader.attach({}, nullptr);
    QVERIFY(reader.handleOpen());
    reader.detach();
    QVERIFY(!reader.handleOpen());
}

void GameStateBoundariesTest::samplerRejectsOverflowingStaticRoot()
{
    const std::uint64_t base = 0x90000000ULL;
    RecordingMemoryReader reader;
    reader.reserve(base, 0x1000);
    reader.setModule("fake.dll", base);
    reader.attach({}, nullptr);
    reader.putU64(base + 0x100, base + 0x200);
    reader.putI32(base + 0x210, 42);

    gamestate::GameProfile profile;
    profile.engine = "generic";
    profile.module = "fake.dll";
    profile.moduleBaseOffset = 0; // 正常档案
    profile.fields.push_back(makeField("hp", "int32", { 0x210 }));

    gamestate::ChainSampler sampler(&reader, &profile);
    core::GameSample sample;
    QString error;
    QVERIFY2(sampler.sample(&sample, &error), qPrintable(error));
    QCOMPARE(sample.hp, 42.0);

    // 静态根溢出：moduleBase + moduleBaseOffset 绕回 ⇒ 明确失败，不读任何地址
    // （`max` 是 windows.h 的宏，必须用 `(ns::max)()` 形式绕开）
    profile.moduleBaseOffset = (std::numeric_limits<std::uint64_t>::max)() - base + 1;
    sampler.reset();
    reader.clearReads();
    QVERIFY(!sampler.sample(&sample, &error));
    QVERIFY(!sample.available);
    QVERIFY2(error.contains(QStringLiteral("溢出")), qPrintable(error));
    QCOMPARE(reader.reads().size(), std::size_t(0));
}

// ---------------------------------------------------------------------------
// 6. Win32GameMemoryReader：真实 attach / detach / 进程退出
// ---------------------------------------------------------------------------

void GameStateBoundariesTest::win32ReaderAttachDetachCycleReleasesHandles()
{
    QVERIFY(gamestate::Win32GameMemoryReader::isSupported());
    gamestate::Win32GameMemoryReader reader;

    // 非法参数：空 process
    {
        gamestate::GameProfile profile;
        QString error;
        QVERIFY(!reader.attach(profile, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!reader.attached());
    }

    // 不存在的进程：失败且不留下句柄
    {
        gamestate::GameProfile profile;
        profile.process = "whalepet-no-such-process-9c1f.exe";
        QString error;
        QVERIFY(!reader.attach(profile, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!reader.attached());
        QCOMPARE(reader.processId(), 0);
    }

    // attach 到**自身**（x64）：真实句柄 + 真实模块表
    gamestate::GameProfile self;
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    self.process = exeName.toStdString();
    self.module = exeName.toStdString();

    QString error;
    if (!reader.attach(self, &error)) {
        // 32 位 / 受保护进程等环境限制：如实跳过并写明原因，不伪装通过
        QSKIP(qPrintable(QStringLiteral("无法 attach 到自身（%1）：%2").arg(exeName, error)));
    }
    QVERIFY(reader.attached());
    QVERIFY(reader.processId() > 0);
    const std::uint64_t imageBase = reader.moduleBase(self.module);
    QVERIFY2(imageBase != 0, "自身模块基址必须可解析");

    // 读自身模块头（PE 签名 'MZ'）
    {
        unsigned char dos[2] = { 0, 0 };
        QVERIFY2(reader.read(imageBase, dos, sizeof(dos)), qPrintable(reader.lastError()));
        QCOMPARE(dos[0], static_cast<unsigned char>('M'));
        QCOMPARE(dos[1], static_cast<unsigned char>('Z'));
    }

    // 非法读取参数：空地址 / 空缓冲 / 零长度 / 未映射地址
    {
        unsigned char buffer[4] = {};
        QVERIFY(!reader.read(0, buffer, sizeof(buffer)));
        QVERIFY(!reader.read(imageBase, nullptr, 4));
        QVERIFY(!reader.read(imageBase, buffer, 0));
        QVERIFY(!reader.read(0x1, buffer, sizeof(buffer))); // 未映射低地址
    }

#if defined(Q_OS_WIN)
    // 反复 attach/detach：句柄数不得单调增长（每轮结束句柄均释放）
    const auto handleCount = []() -> DWORD {
        DWORD count = 0;
        GetProcessHandleCount(GetCurrentProcess(), &count);
        return count;
    };
    const DWORD before = handleCount();
    for (int i = 0; i < 8; ++i) {
        QVERIFY2(reader.attach(self, &error), qPrintable(error));
        QVERIFY(reader.attached());
        reader.detach();
        QVERIFY(!reader.attached());
    }
    const DWORD after = handleCount();
    QVERIFY2(after <= before + 2,
             qPrintable(QStringLiteral("句柄泄漏：%1 → %2").arg(before).arg(after)));
#endif

    // detach 幂等；detach 后读取必须失败
    reader.detach();
    reader.detach();
    QVERIFY(!reader.attached());
    {
        unsigned char buffer[2] = {};
        QVERIFY(!reader.read(imageBase, buffer, sizeof(buffer)));
        QCOMPARE(reader.moduleBase(self.module), std::uint64_t(0));
    }
}

void GameStateBoundariesTest::win32ReaderFailsAfterTargetProcessExits()
{
    QVERIFY(gamestate::Win32GameMemoryReader::isSupported());

    // 用**本测试可执行文件的副本**作为子进程：名字唯一，`findProcessId` 不会误匹配他人
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString childName =
        QStringLiteral("whalepet-boundary-child-%1.exe").arg(QCoreApplication::applicationPid());
    const QString childPath = dir.filePath(childName);
    if (!QFile::copy(QCoreApplication::applicationFilePath(), childPath)) {
        QSKIP("无法复制测试可执行文件作为子进程（跳过「进程退出」实测）");
    }

    QProcess child;
    child.setProgram(childPath);
    child.setArguments({ QStringLiteral("--boundary-child-sleep"), QStringLiteral("60000") });
    child.start();
    if (!child.waitForStarted(15000)) {
        QSKIP(qPrintable(QStringLiteral("子进程无法启动（跳过「进程退出」实测）：%1")
                             .arg(QString::fromLocal8Bit(child.readAllStandardError()))));
    }

    gamestate::GameProfile profile;
    profile.process = childName.toStdString();
    profile.module = childName.toStdString();

    gamestate::Win32GameMemoryReader reader;
    QString error;

    // 轮询等待子进程出现在进程表里（最多 ~10s）
    bool attached = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        if (reader.attach(profile, &error) && reader.processId() == child.processId()) {
            attached = true;
            break;
        }
        QThread::msleep(50);
    }
    if (!attached) {
        child.kill();
        child.waitForFinished(5000);
        QSKIP(qPrintable(QStringLiteral("未能 attach 到子进程（跳过「进程退出」实测）：%1")
                             .arg(error)));
    }
    QVERIFY(reader.attached());

    const std::uint64_t imageBase = reader.moduleBase(profile.module);
    QVERIFY(imageBase != 0);
    unsigned char dos[2] = { 0, 0 };
    QVERIFY2(reader.read(imageBase, dos, sizeof(dos)), qPrintable(reader.lastError()));
    QCOMPARE(dos[0], static_cast<unsigned char>('M'));

    // 杀掉子进程：后续读取必须失败（而不是崩或返回陈旧数据）
    child.kill();
    QVERIFY(child.waitForFinished(10000));

    // 给系统一点时间回收目标进程
    timer.restart();
    bool readFailed = false;
    while (timer.elapsed() < 10000) {
        if (!reader.read(imageBase, dos, sizeof(dos))) {
            readFailed = true;
            break;
        }
        QThread::msleep(50);
    }
    QVERIFY2(readFailed, "目标进程退出后读取必须失败");

    // detach 仍必须干净完成
    reader.detach();
    QVERIFY(!reader.attached());
    QCOMPARE(reader.processId(), 0);
}

int main(int argc, char *argv[])
{
    // 子进程模式：被测的 Win32 读取器需要一个「可被 attach 的第三方进程」，
    // 用本 exe 的副本 + 该参数即可（见 win32ReaderFailsAfterTargetProcessExits）。
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--boundary-child-sleep")) {
        QThread::msleep(60000); // 被 attach 的靶进程：睡够 60s 等父进程杀掉自己
        return 0;
    }
    QCoreApplication app(argc, argv);
    GameStateBoundariesTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_gamestate_boundaries.moc"
