#include "gamestate/UnityRuntime.h"

namespace whalepet::gamestate {

namespace {

const char *const kMonoModules[] = { "mono-2.0-bdwgc.dll", "mono.dll" };
constexpr std::size_t kMonoModuleCount = sizeof(kMonoModules) / sizeof(kMonoModules[0]);

const char *const kIl2CppModule = "GameAssembly.dll";

// 全量名单（Mono 名单 + IL2CPP），仅供提示/诊断。
const char *const kAllModules[] = { "mono-2.0-bdwgc.dll", "mono.dll", "GameAssembly.dll" };
constexpr std::size_t kAllModuleCount = sizeof(kAllModules) / sizeof(kAllModules[0]);

} // namespace

const char *const *unityMonoModuleNames(std::size_t *count)
{
    if (count != nullptr) {
        *count = kMonoModuleCount;
    }
    return kMonoModules;
}

const char *unityIl2CppModuleName()
{
    return kIl2CppModule;
}

const char *const *unityAllModuleNames(std::size_t *count)
{
    if (count != nullptr) {
        *count = kAllModuleCount;
    }
    return kAllModules;
}

UnityBackend detectUnityBackend(IGameMemoryReader *reader)
{
    UnityBackend result;
    if (reader == nullptr || !reader->attached()) {
        return result;
    }
    for (std::size_t i = 0; i < kMonoModuleCount; ++i) {
        if (reader->moduleBase(kMonoModules[i]) != 0) {
            result.engine = "unity-mono";
            result.module = kMonoModules[i];
            return result;
        }
    }
    if (reader->moduleBase(kIl2CppModule) != 0) {
        result.engine = "unity-il2cpp";
        result.module = kIl2CppModule;
    }
    return result;
}

} // namespace whalepet::gamestate
