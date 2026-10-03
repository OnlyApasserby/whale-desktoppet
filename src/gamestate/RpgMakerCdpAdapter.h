#pragma once

// RPG Maker MV / MZ 适配器（方案 A：本地 CDP 只读求值）。
// 归属：docs/ROADMAP-ex1.md §2.6.2。
// 【只读】仅发 `Runtime.evaluate(returnByValue)`；表达式来自 profile.rpgmaker.expressions
//         与内置只读探测（§2.6.2.1）。引擎无响应时置失效，不崩溃。

#include "gamestate/CdpWebSocketClient.h"
#include "gamestate/GameProfile.h"
#include "gamestate/IGameStateAdapter.h"
#include "gamestate/RpgMakerSpecialScene.h"

#include <QString>

#include <map>
#include <memory>
#include <string>

namespace whalepet::gamestate {

class RpgMakerCdpAdapter final : public IGameStateAdapter {
public:
    RpgMakerCdpAdapter();
    ~RpgMakerCdpAdapter() override;

    static bool engineMatches(const std::string &engine);
    // 具备可用 CDP 端点（rpgmaker.wsUrl 或 rpgmaker.cdpPort）。
    static bool supports(const GameProfile &profile);

    // §2.6.2.1 内置只读探测表达式（返回 {scene,video,msg,face,sw,sh,pics[]}）。
    static QString builtinProbeExpression();

    bool attach(const GameProfile &profile, QString *error) override;
    void detach() override;
    bool attached() const override;
    bool read(core::GameSample *out, QString *error) override;

    bool invalidated() const { return m_invalidated; }
    int consecutiveFailures() const { return m_failures; }
    // 失效后显式重连并清零计数（§4.3）。
    bool reconnect(QString *error);

private:
    bool connectCdp(QString *error);
    void markFailure(const QString &error);
    static void applyField(const std::string &name, const QJsonValue &value, core::GameSample *out);

    GameProfile m_profile;
    std::unique_ptr<CdpWebSocketClient> m_cdp;
    RpgMakerSpecialSceneDetector m_sceneDetector;
    std::map<std::string, QString> m_expressions;
    QString m_probeExpression;
    bool m_attached = false;
    bool m_invalidated = false;
    int m_failures = 0;
};

} // namespace whalepet::gamestate
