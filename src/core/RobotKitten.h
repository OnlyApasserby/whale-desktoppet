#pragma once

// 小游戏：鲸鱼娘找小猫（Robot Finds Kitten 风格的地图探索）——纯玩法逻辑，
// **零 Qt 依赖**，可脱离界面单测（见 docs/MINIGAME-INTERFACE.md §10）。
//
// 玩法（对照 Robot Finds Kitten 的核心循环）：
//   * 角色在字符网格地图上四方向移动，撞到障碍物无法前进（给出提示）；
//   * 地图上散布可交互物体：走到物体所在格即「捡起 / 交互」，并播报该物体专属台词；
//   * 场景之间通过「海流」（出口格）连接，走上去即切换到下一场景（多场景探索）；
//   * 在最后一个场景找到小猫 → 通关；找不到可主动结束（按已探索进度结算）。
//
// 分层约定（与扫雷一致）：
//   * 本文件只输出「语义结果」（撞墙 / 交互了哪个物体 / 是否换场景 / 是否找到小猫），
//     立绘与台词由 View 层决定；
//   * 对局结果经 rfkGameResult() 折算为通用的 core::MiniGameResult，
//     宿主与结算服务只认通用契约，不认识找小猫的细节；
//   * **物体列表与地图都是外部资源**（`assets/maps/*.txt`），本文件只负责解析它们，
//     便于用户自行增删物体、替换台词场景 key 而不改代码。

#include "core/MiniGameTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 物体类别：决定「走到该格后如何处理」与「缺省台词场景 key」
// ---------------------------------------------------------------------------

enum class RfkKind {
    Floor,   // 地面（可走，无反应）
    Blocker, // 障碍物（不可进入）
    Toy,     // 有趣物品：好奇立绘 + 专属台词
    Junk,    // 无关杂物：嫌弃立绘 + 专属台词
    Kitten,  // 小猫：找到即通关
    Exit,    // 出口（海流）：走到即切换场景
    Player,  // 起点（鲸鱼娘出生点，地图中必须恰好一个）
};

const char *rfkKindId(RfkKind kind);                  // 稳定标识（物体表用）
bool rfkKindFromId(const std::string &id, RfkKind *out); // 解析物体表用
const char *rfkKindName(RfkKind kind);                // 显示名

// 物体表未指定台词场景 key 时的缺省值（空串 = 不播台词）
const char *rfkKindScene(RfkKind kind);

// ---------------------------------------------------------------------------
// 物体定义表（外部资源 `assets/maps/kitten_objects.txt`）
//
// 单行格式：`glyph|id|名称|类别|台词场景key|显示文本`
//   * glyph   ：地图中使用的单字符（区分大小写）
//   * id      ：稳定标识（测试与日志用）
//   * 名称    ：展示用（撞到障碍物时的提示文案）
//   * 类别    ：blocker / toy / junk / kitten / exit / player / floor
//   * 场景key ：可空；空则用 rfkKindScene(类别) 的缺省值
//   * 显示文本：可空；空则直接显示 glyph（支持中文单字，如「猫」）
// 以 ';' 开头的行与空行为注释；格式不合法（缺列 / 类别未知 / glyph 非单字符）的行跳过，
// **不让单行错误使整张表失效**。
// ---------------------------------------------------------------------------

struct RfkObjectDef {
    char glyph = '.';
    std::string id;
    std::string name;
    RfkKind kind = RfkKind::Floor;
    std::string scene;   // 台词场景 key（可空）
    std::string display; // 单元格显示文本（可空 → 用 glyph）
};

class RfkObjectTable {
public:
    // 解析物体表文本，返回成功解析的条目数
    static RfkObjectTable parse(const std::string &content);

    void add(const RfkObjectDef &def); // 同 glyph 后者覆盖前者
    const RfkObjectDef *find(char glyph) const;

    std::size_t size() const { return m_defs.size(); }
    const std::vector<RfkObjectDef> &defs() const { return m_defs; }

private:
    std::vector<RfkObjectDef> m_defs;
};

// 内置兜底物体表：外部资源缺失时仍可玩（优雅降级，与台词库缺失的处理一致）
RfkObjectTable rfkDefaultObjectTable();

// ---------------------------------------------------------------------------
// 地图（场景）
// ---------------------------------------------------------------------------

struct RfkCell {
    char glyph = '.';          // 原始字符
    RfkKind kind = RfkKind::Floor;
    std::string objectId;
    std::string name;          // 展示名（障碍物提示用）
    std::string scene;         // 台词场景 key（可空）
    std::string display;       // 单元格显示文本
    bool visited = false;      // 角色是否到过（进度统计）
    bool consumed = false;     // 物品是否已被交互（交互后显示为空地）
};

struct RfkRoom {
    int width = 0;
    int height = 0;
    std::vector<RfkCell> cells; // 行优先：index = y * width + x
    int startIndex = 0;         // '@' 起点
    int exitIndex = -1;         // '>' 出口（-1 = 本场景无出口）

    int cellCount() const { return width * height; }
};

// 解析一张地图文本。失败时返回 false 并把原因写入 error（UTF-8）。
// 约定：
//   * 以 ';' 开头的行与全空白行为注释；
//   * 短行右侧按空地补齐，故各行列数不必相等；
//   * 必须恰好有一个起点（player 类别），否则视为配置错误。
bool rfkParseRoom(const std::string &text, const RfkObjectTable &table, RfkRoom &out,
                  std::string *error);

// ---------------------------------------------------------------------------
// 难度：决定需要穿越几个场景（越难场景越多）
// ---------------------------------------------------------------------------

enum class RfkDifficulty {
    Shallow, // 浅滩
    Coral,   // 珊瑚湾
    Abyss,   // 深海遗迹
};

struct RfkDifficultyDef {
    RfkDifficulty difficulty;
    const char *id;   // 稳定标识（落库 / 纪录分桶）
    const char *name; // 显示名
    int rooms;        // 该难度需要穿越的场景数
    const char *description;
};

inline constexpr RfkDifficultyDef kRfkDifficulties[] = {
    {RfkDifficulty::Shallow, "shallow", "浅滩", 1, "1 个场景，小猫就藏在脚边"},
    {RfkDifficulty::Coral, "coral", "珊瑚湾", 2, "穿过海流，在 2 个场景里寻找小猫"},
    {RfkDifficulty::Abyss, "abyss", "深海遗迹", 3, "3 个场景的迷宫，最考验耐心"},
};
inline constexpr int kRfkDifficultyCount =
    static_cast<int>(sizeof(kRfkDifficulties) / sizeof(kRfkDifficulties[0]));

inline const RfkDifficultyDef *rfkDifficultyDef(RfkDifficulty d)
{
    for (const RfkDifficultyDef &def : kRfkDifficulties) {
        if (def.difficulty == d) {
            return &def;
        }
    }
    return nullptr;
}

inline const char *rfkDifficultyId(RfkDifficulty d)
{
    const RfkDifficultyDef *def = rfkDifficultyDef(d);
    return def != nullptr ? def->id : "shallow";
}

inline const char *rfkDifficultyName(RfkDifficulty d)
{
    const RfkDifficultyDef *def = rfkDifficultyDef(d);
    return def != nullptr ? def->name : "浅滩";
}

inline int rfkRoomCount(RfkDifficulty d)
{
    const RfkDifficultyDef *def = rfkDifficultyDef(d);
    return def != nullptr ? def->rooms : 1;
}

// 由整数值（落库用）还原难度；越界回退浅滩
inline RfkDifficulty rfkDifficultyOfIndex(int index)
{
    if (index >= 0 && index < kRfkDifficultyCount) {
        return kRfkDifficulties[index].difficulty;
    }
    return RfkDifficulty::Shallow;
}

// 四方向（供 View 的方向键 / 按钮使用）
enum class RfkDirection { Up, Down, Left, Right };

// ---------------------------------------------------------------------------
// 一次移动的结果（View 据此决定立绘 / 台词表现）
// ---------------------------------------------------------------------------

struct RfkMove {
    bool moved = false;      // 是否成功移动（撞墙 / 越界时为 false）
    bool blocked = false;    // 本次被障碍物挡住
    std::string blockerName; // 挡路的障碍物名称（可空）

    bool interacted = false;         // 本次踩到了可交互物体（已消费）
    RfkKind interactKind = RfkKind::Floor;
    std::string interactId;
    std::string interactName;
    std::string interactScene;       // 台词场景 key（可空 → 不播台词）

    bool sceneChanged = false; // 本次切换到下一场景
    int sceneIndex = 0;        // 切换后的场景下标（未切换时为当前下标）
    bool won = false;          // 本次找到了小猫

    int steps = 0;             // 累计步数
};

// 一局结算数据（成就 / 奖励 / 播报用）
struct RfkSummary {
    bool won = false;
    bool perfect = false; // 找到小猫且全程未撞墙（零失误）
    bool expert = false;  // 高难档（深海遗迹）
    int maxChain = 0;      // 连续顺畅移动峰值（撞墙清零）
    int steps = 0;
    int blockedCount = 0;
    int visitedCells = 0; // 已走过的格子数
    int floorCells = 0;   // 全部场景的可走格总数（>= 1）
    int roomCount = 0;
    int roomsVisited = 1;
};

// ---------------------------------------------------------------------------
// 世界：若干场景（按顺序）+ 角色位置 + 进度
// ---------------------------------------------------------------------------

class RfkWorld {
public:
    RfkWorld() = default;

    // 载入一局：roomTexts 按场景顺序给出地图文本，数量应与难度的 rooms 一致
    // （多于所需将忽略多余项）。任一场景解析失败 → 返回 false 并清空世界。
    bool load(const RfkObjectTable &table, RfkDifficulty difficulty,
              const std::vector<std::string> &roomTexts, std::string *error);

    bool loaded() const { return !m_rooms.empty(); }
    RfkDifficulty difficulty() const { return m_difficulty; }
    int roomCount() const { return static_cast<int>(m_rooms.size()); }
    int roomIndex() const { return m_room; }
    const RfkRoom &room(int index) const { return m_rooms[static_cast<std::size_t>(index)]; }
    const RfkRoom &currentRoom() const { return m_rooms[static_cast<std::size_t>(m_room)]; }

    int playerIndex() const { return m_player; }
    int playerX() const;
    int playerY() const;

    // 四方向移动（dx / dy 只能有一个非零，取值 ±1；非法参数返回空结果）
    RfkMove move(int dx, int dy);
    RfkMove moveDir(RfkDirection dir);

    bool finished() const { return m_finished; }
    bool won() const { return m_won; }
    int steps() const { return m_steps; }
    int blockedCount() const { return m_blocked; }
    int maxChain() const { return m_maxChain; }
    int visitedCells() const { return m_visited; }
    int floorCells() const { return m_floorTotal; }

    // 主动结束本局（未找到小猫 → 按已探索进度结算）
    void abandon() { m_finished = true; }

    RfkSummary summary() const;

private:
    void visit(int roomIndex, int cellIndex);

    std::vector<RfkRoom> m_rooms;
    RfkDifficulty m_difficulty = RfkDifficulty::Shallow;

    int m_room = 0;
    int m_player = 0;
    int m_steps = 0;
    int m_blocked = 0;
    int m_chain = 0;
    int m_maxChain = 0;
    int m_visited = 0;
    int m_floorTotal = 0;
    bool m_finished = false;
    bool m_won = false;
};

// 找小猫对局 → 通用结算契约（宿主 / 结算服务只认 MiniGameResult）。
inline MiniGameResult rfkGameResult(const RfkSummary &summary, RfkDifficulty difficulty,
                                    std::int64_t elapsedMs)
{
    MiniGameResult r;
    r.gameId = "kitten";
    r.difficultyId = rfkDifficultyId(difficulty);
    r.difficultyLabel = rfkDifficultyName(difficulty);
    r.won = summary.won;
    r.perfect = summary.perfect;
    r.expert = summary.expert;
    r.maxChain = summary.maxChain;
    r.progressDone = summary.visitedCells;
    r.progressTotal = summary.floorCells;
    r.elapsedMs = elapsedMs;
    return r;
}

} // namespace whalepet::core
