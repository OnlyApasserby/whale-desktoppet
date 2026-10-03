#include "gamestate/UnityIl2CppAdapter.h"

#include "gamestate/UnityRuntime.h"

namespace whalepet::gamestate {

namespace {
constexpr const char *kEngine = "unity-il2cpp";
} // namespace

UnityIl2CppAdapter::UnityIl2CppAdapter(std::unique_ptr<IGameMemoryReader> reader)
    : UnityAdapterBase(std::move(reader), kEngine, unityIl2CppModuleName())
{
}

const char *UnityIl2CppAdapter::engineName()
{
    return kEngine;
}

QString UnityIl2CppAdapter::backendHint() const
{
    return QStringLiteral(
        "提示：未探测到 GameAssembly.dll。若该游戏为 Mono 后端请改用 unity-mono；"
        "若 global-metadata 被加密/裁剪，请按 docs/UNITY-SOP.md 降级到 generic 或重新导出。");
}

} // namespace whalepet::gamestate
