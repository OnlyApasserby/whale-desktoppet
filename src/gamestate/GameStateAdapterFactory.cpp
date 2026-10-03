#include "gamestate/GenericChainAdapter.h"
#include "gamestate/IGameStateAdapter.h"
#include "gamestate/RpgMakerBridgeAdapter.h"
#include "gamestate/RpgMakerCdpAdapter.h"
#include "gamestate/UnityIl2CppAdapter.h"
#include "gamestate/UnityMonoAdapter.h"

namespace whalepet::gamestate {

std::unique_ptr<IGameStateAdapter> createGameStateAdapter(const GameProfile &profile, QString *error)
{
    // Unity 引擎细分到具体后端适配器（§2.6.1）；其余内存引擎共用通用指针链底座。
    if (profile.engine == UnityMonoAdapter::engineName()) {
        return std::make_unique<UnityMonoAdapter>();
    }
    if (profile.engine == UnityIl2CppAdapter::engineName()) {
        return std::make_unique<UnityIl2CppAdapter>();
    }
    // RPG Maker（§2.6.2 / §2.6.3）：MV/MZ 优先 CDP，其次桥接回退；RGSS 走桥接。
    if (RpgMakerCdpAdapter::engineMatches(profile.engine)) {
        if (RpgMakerCdpAdapter::supports(profile)) {
            return std::make_unique<RpgMakerCdpAdapter>();
        }
        if (RpgMakerBridgeAdapter::supports(profile)) {
            return std::make_unique<RpgMakerBridgeAdapter>();
        }
        if (error != nullptr) {
            *error = QStringLiteral(
                "rpgmaker-mv/mz 需要 CDP 端点（rpgmaker.cdpPort/wsUrl）或桥接（bridge）");
        }
        return nullptr;
    }
    if (profile.engine == "rpgmaker-rgss") {
        if (RpgMakerBridgeAdapter::supports(profile)) {
            return std::make_unique<RpgMakerBridgeAdapter>();
        }
        if (error != nullptr) {
            *error = QStringLiteral("rpgmaker-rgss 需要 profile.bridge 提供只读状态快照");
        }
        return nullptr;
    }
    if (profile.isMemoryEngine()) {
        return std::make_unique<GenericChainAdapter>();
    }
    if (error != nullptr) {
        *error = QStringLiteral("尚未支持的 engine：%1").arg(QString::fromStdString(profile.engine));
    }
    return nullptr;
}

} // namespace whalepet::gamestate
