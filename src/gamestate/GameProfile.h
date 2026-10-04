#pragma once

// 游戏档案（profile）模型与加载器：离线分析产物 ↔ 运行期读取之间的唯一契约。
// 归属：docs/ROADMAP-ex1.md §2.7（Schema）、§六 6.2（接口契约）。
// 【红线】profile 只描述「读什么、怎么读」，不含任何写入 / 修改能力。

#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace whalepet::gamestate {

// 档案硬上限（加载期校验，越界即拒绝；见 SECURITY-REVIEW.md §极端边界测试建议 5）。
//   * 链深度：真实档案 ≤ 4 级；放到 16 已是数量级余量，再大只会放大误配档案的破坏面。
//   * 单轮字节预算：默认 4 KiB，1 MiB 足够任何字段集；超大值等于「没有预算」。
inline constexpr int kMaxProfileJumps = 16;
inline constexpr std::uint64_t kMaxProfileBytesPerRound = 1024ULL * 1024ULL;

// 字段类型（§2.7 fields[].kind）
enum class GameFieldKind { Unknown, Int32, Int64, Float, Double, Bool, Utf16 };

const char *gameFieldKindId(GameFieldKind kind);
GameFieldKind gameFieldKindFromId(const std::string &id);

// 引擎标识（§2.7 engine）
bool isKnownEngine(const std::string &engine);
const char *const *knownEngines(std::size_t *count);

// 字段规格（profile.fields[]）
struct GameFieldSpec {
    std::string name;                 // hp / hpMax / gold / level / posX / posY / mapName / specialScene
    std::string kind;                 // int32 | int64 | float | double | bool | utf16
    std::vector<std::uint64_t> chain; // chain[0]=静态根偏移；其后每级「先解引用再加偏移」
    std::string freq;                 // high | mid | event
};

// 游戏档案（§2.7）
struct GameProfile {
    struct Validation {
        std::uint64_t magicOffset = 0; // 相对 (moduleBase + moduleBaseOffset)
        std::uint32_t magic = 0;
        bool hasMagic = false;
        int maxJumps = 4;              // 最大解引用跳数（chain.size()-1 ≤ maxJumps）
    };

    std::string engine;
    std::string process;
    std::string module;
    std::uint64_t moduleBaseOffset = 0;
    Validation validation;
    std::vector<GameFieldSpec> fields;
    QJsonObject rpgmaker;             // cdpPort / expressions / specialScene
    QJsonObject bridge;               // kind(file|socket) / path / format
    std::uint64_t maxBytesPerRound = 4096;

    const GameFieldSpec *field(const std::string &name) const;
    bool isMemoryEngine() const;      // 走「外部只读内存」通道的引擎（Unity / generic）
};

// profile 解析 / 序列化（失败填充 *error 并返回 false，不抛异常）
class ProfileLoader {
public:
    static bool loadFromJson(const QJsonObject &obj, GameProfile *out, QString *error);
    static bool loadFromFile(const QString &path, GameProfile *out, QString *error);
    static bool saveToFile(const GameProfile &profile, const QString &path, QString *error);
};

} // namespace whalepet::gamestate
