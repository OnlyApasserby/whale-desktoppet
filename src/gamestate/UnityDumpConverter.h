#pragma once

// Unity 离线转换：把 Il2CppDumper / Cpp2IL 产出的 dump.cs「按类名+字段名」解析为
// 游戏档案（profile）。归属：docs/ROADMAP-ex1.md §2.6.1（IL2CPP 离线辅助流程）、§2.7。
//
// 【只读 / 离线】本转换器纯函数、不接触目标进程；产物 profile 只描述「读什么」。
// 静态字段数据区基址（moduleBaseOffset）由离线分析/Runtime 观测获得，本转换器只负责
// 「类名.字段名 → 偏移」的定位，避免在适配器里硬编码字段偏移。

#include "gamestate/GameProfile.h"

#include <QString>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace whalepet::gamestate {

// 一个逻辑字段到 dump.cs 中「类/字段」的映射。
struct UnityFieldMapping {
    std::string name;                // 逻辑字段名（hp / hpMax / gold / ... 与 GameSample 对齐）
    std::string kind;                // int32 | int64 | float | double | bool | utf16
    std::string cls;                 // dump.cs 类名，可带命名空间（如 "Game.Player"）
    std::string field;               // dump.cs 字段名
    std::string freq;                // high | mid | event（可选，默认 high）
    std::vector<std::uint64_t> tail; // 字段偏移之后追加的链尾（实例字段跳转/偏移，可选）
};

// profile 构造规格。
struct UnityProfileSpec {
    std::string engine = "unity-il2cpp"; // unity-mono | unity-il2cpp
    std::string process;                 // 目标进程名（如 "Game.exe"）
    std::string module;                  // 原生模块名；空则按 engine 取默认值
    std::uint64_t staticBaseOffset = 0;  // moduleBase + 本值 = 静态字段数据区基址
    GameProfile::Validation validation;  // 可选魔数校验（相对 moduleBase+moduleBaseOffset）
    std::uint64_t maxBytesPerRound = 4096;
    std::vector<UnityFieldMapping> mappings;
};

class UnityDumpConverter {
public:
    // engine 默认原生模块名：il2cpp → GameAssembly.dll；mono → mono-2.0-bdwgc.dll。
    static QString defaultModule(const std::string &engine);

    // 解析 dump.cs，产出 类名 → 字段名 → 偏移；同时以「简单类名」为键冗余登记一份。
    // 解析失败（空输入 / 未识别到任何字段）返回 false 并填 *error。
    static bool parseFieldOffsets(const QString &dumpCs,
                                  std::map<std::string, std::map<std::string, std::uint64_t>> *out,
                                  QString *error);

    // 由 dump.cs + 规格构造 GameProfile；内部经 ProfileLoader 复核，保证满足 §2.7 契约。
    // 任一映射无法在 dump.cs 中定位时返回 false 并说明「类.字段 未找到」。
    static bool buildProfile(const QString &dumpCs, const UnityProfileSpec &spec, GameProfile *out,
                             QString *error);
};

} // namespace whalepet::gamestate
