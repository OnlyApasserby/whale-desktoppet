#pragma once

// Unity 运行期后端判定（Mono / IL2CPP）与模块命名约定。
// 归属：docs/ROADMAP-ex1.md §2.6.1、§2.6.1 的「Mono / IL2CPP 判定」。
// 【只读】仅经 IGameMemoryReader::moduleBase 查询模块是否加载，不打开新句柄。

#include "gamestate/IGameMemoryReader.h"

#include <cstddef>
#include <string>

namespace whalepet::gamestate {

// Unity 运行时后端判定结果；engine 为空表示「未探测到 Unity 运行时」。
struct UnityBackend {
    std::string engine; // "unity-mono" | "unity-il2cpp" | ""
    std::string module; // 命中的模块名（Mono 为实际命中的 mono 模块名）
};

// Mono 运行时模块名（新老版本命名不同）：mono-2.0-bdwgc.dll（较新）/ mono.dll（较老）。
const char *const *unityMonoModuleNames(std::size_t *count);
// IL2CPP 运行时模块名：GameAssembly.dll。
const char *unityIl2CppModuleName();

// 在已 attach 的读取器上探测后端：优先命中 Mono 模块，其次 GameAssembly.dll。
// 未命中返回 engine 为空（调用方据此提示用户选择 unity-mono / unity-il2cpp / generic）。
UnityBackend detectUnityBackend(IGameMemoryReader *reader);

// 把已知的 Unity 模块名（诊断/提示用，含 IL2CPP）：Mono 名单 + GameAssembly.dll。
const char *const *unityAllModuleNames(std::size_t *count);

} // namespace whalepet::gamestate
