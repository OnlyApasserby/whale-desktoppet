#include "gamestate/UnityMonoAdapter.h"

namespace whalepet::gamestate {

namespace {
constexpr const char *kEngine = "unity-mono";
constexpr const char *kFallbackModule = "mono-2.0-bdwgc.dll";
} // namespace

UnityMonoAdapter::UnityMonoAdapter(std::unique_ptr<IGameMemoryReader> reader)
    : UnityAdapterBase(std::move(reader), kEngine, kFallbackModule)
{
}

const char *UnityMonoAdapter::engineName()
{
    return kEngine;
}

QString UnityMonoAdapter::backendHint() const
{
    return QStringLiteral(
        "提示：未探测到 Mono 运行时（候选模块 mono-2.0-bdwgc.dll / mono.dll）。"
        "若该游戏为 IL2CPP 后端请改用 unity-il2cpp；无法判定时用 generic。");
}

} // namespace whalepet::gamestate
