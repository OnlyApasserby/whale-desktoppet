// EX1.3 验收测试（离线部分）：特殊场景检测（纯逻辑）/ 桥接适配器（文件快照）/
// MV·MZ CDP 适配器（对本地 QWebSocketServer 回放 Runtime.evaluate）/ 工厂路由。
// 对应 docs/ROADMAP-ex1.md §2.6.2、§2.6.3、§2.6.2.1、§4.3、§5.2。

#include <QtTest/QtTest>

#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QWebSocketServer>

#include "core/GameState.h"
#include "gamestate/ChainSampler.h"
#include "gamestate/GameProfile.h"
#include "gamestate/RpgMakerBridgeAdapter.h"
#include "gamestate/RpgMakerCdpAdapter.h"
#include "gamestate/RpgMakerSpecialScene.h"

using namespace whalepet;
using core::GameSpecialScene;

namespace {

QJsonObject fullscreenPictureProbe()
{
    QJsonObject pic;
    pic.insert(QStringLiteral("id"), 1);
    pic.insert(QStringLiteral("n"), QStringLiteral("cg"));
    pic.insert(QStringLiteral("op"), 255);
    pic.insert(QStringLiteral("sx"), 100);
    pic.insert(QStringLiteral("sy"), 100);
    pic.insert(QStringLiteral("x"), 0);
    pic.insert(QStringLiteral("y"), 0);
    pic.insert(QStringLiteral("o"), 0);
    pic.insert(QStringLiteral("bw"), 816);
    pic.insert(QStringLiteral("bh"), 624);
    QJsonObject probe;
    probe.insert(QStringLiteral("scene"), QStringLiteral("Scene_Map"));
    probe.insert(QStringLiteral("video"), false);
    probe.insert(QStringLiteral("msg"), false);
    probe.insert(QStringLiteral("face"), QString());
    probe.insert(QStringLiteral("sw"), 816);
    probe.insert(QStringLiteral("sh"), 624);
    probe.insert(QStringLiteral("pics"), QJsonArray{ pic });
    return probe;
}

} // namespace

class RpgMakerAdaptersTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void specialSceneClassifyByPriorityAndHysteresis();
    void bridgeAdapterReadsFileSnapshot();
    void bridgeAdapterJsonlAndProbeHysteresis();
    void cdpAdapterReadsFieldsAndProbe();
    void cdpAdapterInvalidatesAfterConsecutiveFailures();
    void factoryRoutesRpgMakerEngines();

private:
    QWebSocketServer *m_server = nullptr;
    QWebSocket *m_peer = nullptr;
    QHash<QString, QJsonValue> m_replies;
    QSet<QString> m_failExpressions;
};

void RpgMakerAdaptersTest::initTestCase()
{
    m_server = new QWebSocketServer(QStringLiteral("fake-cdp"), QWebSocketServer::NonSecureMode);
    QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));
    connect(m_server, &QWebSocketServer::newConnection, this, [this]() {
        m_peer = m_server->nextPendingConnection();
        connect(m_peer, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
            const QJsonObject request = QJsonDocument::fromJson(message.toUtf8()).object();
            const int id = request.value(QStringLiteral("id")).toInt(-1);
            const QJsonObject params = request.value(QStringLiteral("params")).toObject();
            const QString expression = params.value(QStringLiteral("expression")).toString();

            QJsonObject response;
            response.insert(QStringLiteral("id"), id);
            if (m_failExpressions.contains(expression)) {
                QJsonObject error;
                error.insert(QStringLiteral("message"), QStringLiteral("boom"));
                response.insert(QStringLiteral("error"), error);
            } else {
                QJsonObject inner;
                inner.insert(QStringLiteral("type"), QStringLiteral("object"));
                inner.insert(QStringLiteral("value"), m_replies.value(expression));
                QJsonObject result;
                result.insert(QStringLiteral("result"), inner);
                response.insert(QStringLiteral("result"), result);
            }
            m_peer->sendTextMessage(
                QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
        });
    });
}

void RpgMakerAdaptersTest::cleanupTestCase()
{
    delete m_server;
    m_server = nullptr;
}

void RpgMakerAdaptersTest::specialSceneClassifyByPriorityAndHysteresis()
{
    gamestate::RpgMakerSpecialSceneConfig config;
    config.sceneNames = { "Scene_CG" };

    QJsonObject probe = fullscreenPictureProbe();
    QCOMPARE(gamestate::RpgMakerSpecialSceneDetector::classify(probe, config),
             GameSpecialScene::Picture);

    QJsonObject video = probe;
    video.insert(QStringLiteral("video"), true);
    QCOMPARE(gamestate::RpgMakerSpecialSceneDetector::classify(video, config),
             GameSpecialScene::Video);

    QJsonObject named = probe;
    named.insert(QStringLiteral("scene"), QStringLiteral("Scene_CG"));
    named.insert(QStringLiteral("pics"), QJsonArray());
    QCOMPARE(gamestate::RpgMakerSpecialSceneDetector::classify(named, config),
             GameSpecialScene::Scene);

    QJsonObject dialogue;
    dialogue.insert(QStringLiteral("scene"), QStringLiteral("Scene_Map"));
    dialogue.insert(QStringLiteral("msg"), true);
    dialogue.insert(QStringLiteral("face"), QStringLiteral("Actor1"));
    dialogue.insert(QStringLiteral("sw"), 816);
    dialogue.insert(QStringLiteral("sh"), 624);
    QCOMPARE(gamestate::RpgMakerSpecialSceneDetector::classify(dialogue, config),
             GameSpecialScene::Dialogue);

    // 滞回：连续 3 帧成立才置位，连续 3 帧不成立才复位。
    gamestate::RpgMakerSpecialSceneDetector detector(config);
    QCOMPARE(detector.update(probe), GameSpecialScene::None);
    QCOMPARE(detector.update(probe), GameSpecialScene::None);
    QCOMPARE(detector.update(probe), GameSpecialScene::Picture);
    QJsonObject empty;
    empty.insert(QStringLiteral("sw"), 816);
    empty.insert(QStringLiteral("sh"), 624);
    QCOMPARE(detector.update(empty), GameSpecialScene::Picture);
    QCOMPARE(detector.update(empty), GameSpecialScene::Picture);
    QCOMPARE(detector.update(empty), GameSpecialScene::None);
}

void RpgMakerAdaptersTest::bridgeAdapterReadsFileSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("state.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"available":true,"hp":88,"gold":1234,"level":5,"posX":3.5,"mapName":"Fort"})");
    file.close();

    gamestate::GameProfile profile;
    profile.engine = "rpgmaker-rgss";
    QJsonObject bridge;
    bridge.insert(QStringLiteral("kind"), QStringLiteral("file"));
    bridge.insert(QStringLiteral("path"), path);
    bridge.insert(QStringLiteral("format"), QStringLiteral("json"));
    profile.bridge = bridge;

    gamestate::RpgMakerBridgeAdapter adapter;
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));
    QVERIFY(adapter.attached());

    core::GameSample sample;
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QVERIFY(sample.available);
    QCOMPARE(sample.hp, 88.0);
    QCOMPARE(sample.gold, 1234LL);
    QCOMPARE(sample.level, 5);
    QVERIFY(qAbs(sample.posX - 3.5) < 1e-6);
    QCOMPARE(QString::fromStdString(sample.mapName), QStringLiteral("Fort"));

    // 缺桥接配置必须拒绝 attach
    gamestate::GameProfile noBridge;
    noBridge.engine = "rpgmaker-rgss";
    gamestate::RpgMakerBridgeAdapter broken;
    QString brokenError;
    QVERIFY(!broken.attach(noBridge, &brokenError));
    QVERIFY(!brokenError.isEmpty());
}

void RpgMakerAdaptersTest::bridgeAdapterJsonlAndProbeHysteresis()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("state.jsonl"));

    const QJsonObject frame = fullscreenPictureProbe();
    const QByteArray line1 = "{\"available\":true,\"hp\":1,\"specialScene\":9}\n";
    QByteArray line2 = QJsonDocument(QJsonObject{
        { QStringLiteral("available"), true },
        { QStringLiteral("hp"), 2 },
        { QStringLiteral("probe"), frame },
    }).toJson(QJsonDocument::Compact);
    line2.append('\n');

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(line1);
    file.write(line2);
    file.close();

    gamestate::GameProfile profile;
    profile.engine = "rpgmaker-rgss";
    QJsonObject bridge;
    bridge.insert(QStringLiteral("kind"), QStringLiteral("file"));
    bridge.insert(QStringLiteral("path"), path);
    bridge.insert(QStringLiteral("format"), QStringLiteral("jsonl"));
    bridge.insert(QStringLiteral("probe"), true);
    profile.bridge = bridge;

    gamestate::RpgMakerBridgeAdapter adapter;
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));

    core::GameSample sample;
    // 取最后一行（含 probe），滞回需连续 3 帧
    QVERIFY(adapter.read(&sample, &error));
    QCOMPARE(sample.hp, 2.0);
    QCOMPARE(sample.specialScene, 0);
    QVERIFY(adapter.read(&sample, &error));
    QCOMPARE(sample.specialScene, 0);
    QVERIFY(adapter.read(&sample, &error));
    QCOMPARE(sample.specialScene, static_cast<int>(GameSpecialScene::Picture));
}

void RpgMakerAdaptersTest::cdpAdapterReadsFieldsAndProbe()
{
    m_failExpressions.clear();
    m_replies.clear();
    m_replies.insert(QStringLiteral("exprHp"), 123);
    m_replies.insert(QStringLiteral("exprGold"), 456);
    m_replies.insert(QStringLiteral("exprMap"), QStringLiteral("Fort"));
    m_replies.insert(QStringLiteral("exprProbe"), QJsonValue(fullscreenPictureProbe()));

    gamestate::GameProfile profile;
    profile.engine = "rpgmaker-mv";
    QJsonObject expressions;
    expressions.insert(QStringLiteral("hp"), QStringLiteral("exprHp"));
    expressions.insert(QStringLiteral("gold"), QStringLiteral("exprGold"));
    expressions.insert(QStringLiteral("mapName"), QStringLiteral("exprMap"));
    QJsonObject specialScene;
    specialScene.insert(QStringLiteral("expression"), QStringLiteral("exprProbe"));
    specialScene.insert(QStringLiteral("sceneNames"), QJsonArray{ QStringLiteral("Scene_CG") });
    QJsonObject rpgmaker;
    rpgmaker.insert(QStringLiteral("wsUrl"),
                    QStringLiteral("ws://127.0.0.1:%1/").arg(m_server->serverPort()));
    rpgmaker.insert(QStringLiteral("expressions"), expressions);
    rpgmaker.insert(QStringLiteral("specialScene"), specialScene);
    profile.rpgmaker = rpgmaker;

    gamestate::RpgMakerCdpAdapter adapter;
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));
    QVERIFY(adapter.attached());

    core::GameSample sample;
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QVERIFY(sample.available);
    QCOMPARE(sample.hp, 123.0);
    QCOMPARE(sample.gold, 456LL);
    QCOMPARE(QString::fromStdString(sample.mapName), QStringLiteral("Fort"));
    // 探测为全屏图片：滞回需 3 帧
    QCOMPARE(sample.specialScene, 0);
    QVERIFY(adapter.read(&sample, &error));
    QVERIFY(adapter.read(&sample, &error));
    QCOMPARE(sample.specialScene, static_cast<int>(GameSpecialScene::Picture));
}

void RpgMakerAdaptersTest::cdpAdapterInvalidatesAfterConsecutiveFailures()
{
    m_replies.clear();
    m_failExpressions.clear();
    m_failExpressions.insert(QStringLiteral("exprFail"));

    gamestate::GameProfile profile;
    profile.engine = "rpgmaker-mz";
    QJsonObject expressions;
    expressions.insert(QStringLiteral("hp"), QStringLiteral("exprFail"));
    QJsonObject rpgmaker;
    rpgmaker.insert(QStringLiteral("wsUrl"),
                    QStringLiteral("ws://127.0.0.1:%1/").arg(m_server->serverPort()));
    rpgmaker.insert(QStringLiteral("expressions"), expressions);
    profile.rpgmaker = rpgmaker;

    gamestate::RpgMakerCdpAdapter adapter;
    QString error;
    QVERIFY2(adapter.attach(profile, &error), qPrintable(error));

    for (int i = 0; i < gamestate::kGameInvalidateAfterFailures; ++i) {
        core::GameSample sample;
        QVERIFY(!adapter.read(&sample, &error));
        QVERIFY(!sample.available);
    }
    QVERIFY(adapter.invalidated());
    QCOMPARE(adapter.consecutiveFailures(), gamestate::kGameInvalidateAfterFailures);

    // 失效后即便服务端恢复也拒绝继续读，需显式重连
    m_failExpressions.clear();
    m_replies.insert(QStringLiteral("exprFail"), 77);
    core::GameSample sample;
    QVERIFY(!adapter.read(&sample, &error));
    QVERIFY2(adapter.reconnect(&error), qPrintable(error));
    QVERIFY2(adapter.read(&sample, &error), qPrintable(error));
    QCOMPARE(sample.hp, 77.0);
    QVERIFY(!adapter.invalidated());
}

void RpgMakerAdaptersTest::factoryRoutesRpgMakerEngines()
{
    QString error;

    gamestate::GameProfile mvCdp;
    mvCdp.engine = "rpgmaker-mv";
    QJsonObject cdp;
    cdp.insert(QStringLiteral("cdpPort"), 9222);
    mvCdp.rpgmaker = cdp;
    auto cdpAdapter = gamestate::createGameStateAdapter(mvCdp, &error);
    QVERIFY(cdpAdapter != nullptr);
    QVERIFY(dynamic_cast<gamestate::RpgMakerCdpAdapter *>(cdpAdapter.get()) != nullptr);

    gamestate::GameProfile rgss;
    rgss.engine = "rpgmaker-rgss";
    QJsonObject bridge;
    bridge.insert(QStringLiteral("kind"), QStringLiteral("file"));
    bridge.insert(QStringLiteral("path"), QStringLiteral("x.json"));
    bridge.insert(QStringLiteral("format"), QStringLiteral("json"));
    rgss.bridge = bridge;
    auto bridgeAdapter = gamestate::createGameStateAdapter(rgss, &error);
    QVERIFY(bridgeAdapter != nullptr);
    QVERIFY(dynamic_cast<gamestate::RpgMakerBridgeAdapter *>(bridgeAdapter.get()) != nullptr);

    // MV 无 CDP 端点时回退到桥接
    gamestate::GameProfile mvBridge;
    mvBridge.engine = "rpgmaker-mv";
    mvBridge.bridge = bridge;
    auto fallback = gamestate::createGameStateAdapter(mvBridge, &error);
    QVERIFY(fallback != nullptr);
    QVERIFY(dynamic_cast<gamestate::RpgMakerBridgeAdapter *>(fallback.get()) != nullptr);

    // 两者都缺 → 明确报错
    gamestate::GameProfile mvBare;
    mvBare.engine = "rpgmaker-mv";
    error.clear();
    QVERIFY(gamestate::createGameStateAdapter(mvBare, &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(RpgMakerAdaptersTest)
#include "test_rpgmaker_adapters.moc"
