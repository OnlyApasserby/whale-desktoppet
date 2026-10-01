#include "core/RobotKitten.h"

#include <algorithm>

namespace whalepet::core {

namespace {

// 去掉首尾空白（含 CR，兼容 Windows 换行），与 LineTable 的口径一致
std::string trim(const std::string &s)
{
    const char *ws = " \t\r\n";
    const std::size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

// 按 '|' 切分（保留空段，便于「可空列」）
std::vector<std::string> splitPipe(const std::string &line)
{
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (true) {
        const std::size_t sep = line.find('|', pos);
        if (sep == std::string::npos) {
            parts.push_back(line.substr(pos));
            break;
        }
        parts.push_back(line.substr(pos, sep - pos));
        pos = sep + 1;
    }
    return parts;
}

// 逐行回调：通用地处理「空行 / ';' 注释 / trim」（物体表与地图共用）
template <typename Fn>
void forEachMeaningfulLine(const std::string &content, Fn &&fn)
{
    std::size_t pos = 0;
    while (pos <= content.size()) {
        const std::size_t nl = content.find('\n', pos);
        const std::string raw = (nl == std::string::npos) ? content.substr(pos)
                                                          : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;

        const std::string line = trim(raw);
        if (line.empty() || line[0] == ';') {
            continue;
        }
        fn(line);
    }
}

// 地图专用的逐行回调：**保留行首 / 行尾的空格**——空格与 '.' 一样表示可走地面。
// 若在这里做 trim，行首空格会被吃掉使**整行左移**、行尾空格会被吃掉使该行变短，
// 两种情况都会让地图的实际列位置与解析结果错位（实测「隐形墙」的成因之一，
// 见 docs/traps-P6.md TRAP-P6-006）。只剥离 Windows 换行残留的 '\r'；
// 全空白行跳过；首个非空白字符为 ';' 的行视为注释（允许缩进写注释）。
template <typename Fn>
void forEachMapLine(const std::string &content, Fn &&fn)
{
    std::size_t pos = 0;
    while (pos <= content.size()) {
        const std::size_t nl = content.find('\n', pos);
        std::string line = (nl == std::string::npos) ? content.substr(pos)
                                                     : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue; // 全空白行
        }
        if (line[first] == ';') {
            continue; // 注释行
        }
        fn(line);
    }
}

void setError(std::string *error, const std::string &message)
{
    if (error != nullptr) {
        *error = message;
    }
}

// 空白与 '.' 是最常见的「地面」写法。物体表漏写时按地面兜底，
// 避免用户自定义物体表时整张地图不可用；其它未定义字符仍然报错（拼写错误要能暴露）。
const RfkObjectDef &floorFallback()
{
    static const RfkObjectDef kFloor{'.', "floor", "地面", RfkKind::Floor, "", ""};
    return kFloor;
}

} // namespace

// ---------------------------------------------------------------------------
// 类别
// ---------------------------------------------------------------------------

const char *rfkKindId(RfkKind kind)
{
    switch (kind) {
    case RfkKind::Floor:
        return "floor";
    case RfkKind::Blocker:
        return "blocker";
    case RfkKind::Toy:
        return "toy";
    case RfkKind::Junk:
        return "junk";
    case RfkKind::Kitten:
        return "kitten";
    case RfkKind::Exit:
        return "exit";
    case RfkKind::Player:
        return "player";
    }
    return "floor";
}

bool rfkKindFromId(const std::string &id, RfkKind *out)
{
    static const struct {
        const char *id;
        RfkKind kind;
    } kTable[] = {
        {"floor", RfkKind::Floor},   {"blocker", RfkKind::Blocker},
        {"toy", RfkKind::Toy},       {"junk", RfkKind::Junk},
        {"kitten", RfkKind::Kitten}, {"exit", RfkKind::Exit},
        {"player", RfkKind::Player},
    };
    for (const auto &item : kTable) {
        if (id == item.id) {
            if (out != nullptr) {
                *out = item.kind;
            }
            return true;
        }
    }
    return false;
}

const char *rfkKindName(RfkKind kind)
{
    switch (kind) {
    case RfkKind::Floor:
        return "地面";
    case RfkKind::Blocker:
        return "障碍物";
    case RfkKind::Toy:
        return "物品";
    case RfkKind::Junk:
        return "杂物";
    case RfkKind::Kitten:
        return "小猫";
    case RfkKind::Exit:
        return "出口";
    case RfkKind::Player:
        return "鲸鱼娘";
    }
    return "地面";
}

const char *rfkKindScene(RfkKind kind)
{
    switch (kind) {
    case RfkKind::Kitten:
        return "kitten.found";
    case RfkKind::Blocker:
        return "kitten.blocked";
    case RfkKind::Toy:
        return "kitten.item";
    case RfkKind::Junk:
        return "kitten.junk";
    case RfkKind::Exit:
        return "kitten.scene";
    case RfkKind::Floor:
    case RfkKind::Player:
        return "";
    }
    return "";
}

// ---------------------------------------------------------------------------
// 物体表
// ---------------------------------------------------------------------------

void RfkObjectTable::add(const RfkObjectDef &def)
{
    for (RfkObjectDef &existing : m_defs) {
        if (existing.glyph == def.glyph) {
            existing = def; // 同 glyph 后者覆盖（便于在文件末尾微调）
            return;
        }
    }
    m_defs.push_back(def);
}

const RfkObjectDef *RfkObjectTable::find(char glyph) const
{
    for (const RfkObjectDef &def : m_defs) {
        if (def.glyph == glyph) {
            return &def;
        }
    }
    return nullptr;
}

RfkObjectTable RfkObjectTable::parse(const std::string &content)
{
    RfkObjectTable table;
    forEachMeaningfulLine(content, [&table](const std::string &line) {
        const std::vector<std::string> parts = splitPipe(line);
        if (parts.size() < 4) {
            return; // 缺列：跳过该行，不让单行错误使整张表失效
        }
        const std::string glyph = trim(parts[0]);
        if (glyph.size() != 1) {
            return; // glyph 必须是单字符
        }
        RfkObjectDef def;
        def.glyph = glyph[0];
        def.id = trim(parts[1]);
        def.name = trim(parts[2]);
        if (def.id.empty() || def.name.empty()) {
            return;
        }
        if (!rfkKindFromId(trim(parts[3]), &def.kind)) {
            return; // 类别未知
        }
        if (parts.size() > 4) {
            def.scene = trim(parts[4]);
        }
        if (parts.size() > 5) {
            def.display = trim(parts[5]);
        }
        table.add(def);
    });
    return table;
}

RfkObjectTable rfkDefaultObjectTable()
{
    // 外部资源缺失时的兜底：保证「能玩」而不是「没反应」。
    RfkObjectTable table;
    table.add({'.', "floor", "地面", RfkKind::Floor, "", ""});
    table.add({'@', "player", "鲸鱼娘", RfkKind::Player, "", "鲸"});
    table.add({'#', "wall", "礁石", RfkKind::Blocker, "kitten.blocked", ""});
    table.add({'>', "exit", "海流", RfkKind::Exit, "kitten.scene", "门"});
    table.add({'k', "kitten", "小猫", RfkKind::Kitten, "kitten.found", "猫"});
    table.add({'o', "shell", "扇贝", RfkKind::Toy, "kitten.shell", "贝"});
    table.add({'b', "bottle", "漂流瓶", RfkKind::Junk, "kitten.bottle", "瓶"});
    return table;
}

// ---------------------------------------------------------------------------
// 地图解析
// ---------------------------------------------------------------------------

bool rfkParseRoom(const std::string &text, const RfkObjectTable &table, RfkRoom &out,
                  std::string *error)
{
    std::vector<std::string> rows;
    // 地图行**不做 trim**：行首空格代表地面，trim 会让整行左移造成列错位
    forEachMapLine(text, [&rows](const std::string &line) { rows.push_back(line); });
    if (rows.empty()) {
        setError(error, "地图为空（至少需要一行）");
        return false;
    }

    int width = 0;
    for (const std::string &row : rows) {
        width = std::max(width, static_cast<int>(row.size()));
    }
    if (width <= 0) {
        setError(error, "地图宽度为 0");
        return false;
    }

    RfkRoom room;
    room.width = width;
    room.height = static_cast<int>(rows.size());
    room.cells.assign(static_cast<std::size_t>(room.cellCount()), RfkCell{});

    int startCount = 0;
    for (int y = 0; y < room.height; ++y) {
        const std::string &row = rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < width; ++x) {
            // 短行右侧按空地补齐
            const char glyph = (x < static_cast<int>(row.size())) ? row[static_cast<std::size_t>(x)]
                                                                 : '.';
            const RfkObjectDef *def = table.find(glyph);
            if (def == nullptr && (glyph == '.' || glyph == ' ')) {
                def = &floorFallback();
            }
            if (def == nullptr) {
                setError(error, "地图第 " + std::to_string(y + 1) + " 行第 "
                                    + std::to_string(x + 1) + " 列出现未定义的字符 '" + glyph
                                    + "'（请先在物体表中登记）");
                return false;
            }

            const int index = y * width + x;
            RfkCell &cell = room.cells[static_cast<std::size_t>(index)];
            cell.glyph = glyph;
            cell.kind = def->kind;
            cell.objectId = def->id;
            cell.name = def->name;
            cell.scene = def->scene.empty() ? rfkKindScene(def->kind) : def->scene;
            cell.display = def->display.empty() ? std::string(1, glyph) : def->display;

            if (def->kind == RfkKind::Player) {
                ++startCount;
                room.startIndex = index;
            } else if (def->kind == RfkKind::Exit && room.exitIndex < 0) {
                room.exitIndex = index;
            }
        }
    }

    if (startCount != 1) {
        setError(error, "地图必须恰好有一个起点（player 类别），当前 " + std::to_string(startCount)
                            + " 个");
        return false;
    }

    out = room;
    return true;
}

// ---------------------------------------------------------------------------
// 世界
// ---------------------------------------------------------------------------

bool RfkWorld::load(const RfkObjectTable &table, RfkDifficulty difficulty,
                    const std::vector<std::string> &roomTexts, std::string *error)
{
    const int needed = rfkRoomCount(difficulty);
    if (static_cast<int>(roomTexts.size()) < needed) {
        setError(error, "场景数不足：难度「" + std::string(rfkDifficultyName(difficulty))
                            + "」需要 " + std::to_string(needed) + " 个场景，实际 "
                            + std::to_string(roomTexts.size()) + " 个");
        return false;
    }

    std::vector<RfkRoom> rooms;
    rooms.reserve(static_cast<std::size_t>(needed));
    int floorTotal = 0;
    for (int i = 0; i < needed; ++i) {
        RfkRoom room;
        std::string parseError;
        if (!rfkParseRoom(roomTexts[static_cast<std::size_t>(i)], table, room, &parseError)) {
            setError(error, "场景 " + std::to_string(i + 1) + " 解析失败：" + parseError);
            return false;
        }
        for (const RfkCell &cell : room.cells) {
            if (cell.kind != RfkKind::Blocker) {
                ++floorTotal;
            }
        }
        rooms.push_back(std::move(room));
    }

    // 载入成功后才整体替换，避免半成品状态
    m_rooms = std::move(rooms);
    m_difficulty = difficulty;
    m_room = 0;
    m_player = m_rooms[0].startIndex;
    m_steps = 0;
    m_blocked = 0;
    m_chain = 0;
    m_maxChain = 0;
    m_visited = 0;
    m_floorTotal = floorTotal > 0 ? floorTotal : 1;
    m_finished = false;
    m_won = false;

    // 所有场景的格子都复位（上面的局部 RfkRoom 是新建的，天然干净）
    visit(0, m_player);
    return true;
}

int RfkWorld::playerX() const
{
    if (!loaded()) {
        return 0;
    }
    return m_player % currentRoom().width;
}

int RfkWorld::playerY() const
{
    if (!loaded()) {
        return 0;
    }
    return m_player / currentRoom().width;
}

void RfkWorld::visit(int roomIndex, int cellIndex)
{
    if (roomIndex < 0 || roomIndex >= roomCount()) {
        return;
    }
    RfkRoom &room = m_rooms[static_cast<std::size_t>(roomIndex)];
    if (cellIndex < 0 || cellIndex >= room.cellCount()) {
        return;
    }
    RfkCell &cell = room.cells[static_cast<std::size_t>(cellIndex)];
    if (!cell.visited) {
        cell.visited = true;
        ++m_visited;
    }
}

RfkMove RfkWorld::move(int dx, int dy)
{
    RfkMove mv;
    if (!loaded() || m_finished) {
        return mv;
    }
    // 只接受四方向单位移动（斜向 / 原地 / 越界步长一律忽略）
    if ((dx == 0 && dy == 0) || (dx != 0 && dy != 0) || dx < -1 || dx > 1 || dy < -1 || dy > 1) {
        return mv;
    }

    RfkRoom &room = m_rooms[static_cast<std::size_t>(m_room)];
    const int x = m_player % room.width;
    const int y = m_player / room.width;
    const int nx = x + dx;
    const int ny = y + dy;

    // 越界视为撞到场景边界（与撞墙同一种反馈）
    if (nx < 0 || ny < 0 || nx >= room.width || ny >= room.height) {
        ++m_blocked;
        m_chain = 0;
        mv.blocked = true;
        mv.steps = m_steps;
        return mv;
    }

    const int target = ny * room.width + nx;
    RfkCell &cell = room.cells[static_cast<std::size_t>(target)];
    if (cell.kind == RfkKind::Blocker) {
        ++m_blocked;
        m_chain = 0;
        mv.blocked = true;
        mv.blockerName = cell.name;
        mv.steps = m_steps;
        return mv;
    }

    m_player = target;
    ++m_steps;
    ++m_chain;
    if (m_chain > m_maxChain) {
        m_maxChain = m_chain;
    }
    mv.moved = true;
    mv.steps = m_steps;
    mv.sceneIndex = m_room;
    visit(m_room, target);

    switch (cell.kind) {
    case RfkKind::Exit:
        // 走到海流 → 进入下一场景（已是最后场景时按普通地板处理）
        if (m_room + 1 < roomCount()) {
            ++m_room;
            m_player = m_rooms[static_cast<std::size_t>(m_room)].startIndex;
            visit(m_room, m_player);
            mv.sceneChanged = true;
            mv.sceneIndex = m_room;
        }
        break;

    case RfkKind::Kitten:
        m_won = true;
        m_finished = true;
        mv.won = true;
        mv.interacted = true;
        mv.interactKind = cell.kind;
        mv.interactId = cell.objectId;
        mv.interactName = cell.name;
        mv.interactScene = cell.scene;
        break;

    case RfkKind::Toy:
    case RfkKind::Junk:
        // 物品只消费一次：踩过之后变成空地，避免来回刷台词
        if (!cell.consumed) {
            cell.consumed = true;
            mv.interacted = true;
            mv.interactKind = cell.kind;
            mv.interactId = cell.objectId;
            mv.interactName = cell.name;
            mv.interactScene = cell.scene;
        }
        break;

    default:
        break;
    }

    return mv;
}

RfkMove RfkWorld::moveDir(RfkDirection dir)
{
    switch (dir) {
    case RfkDirection::Up:
        return move(0, -1);
    case RfkDirection::Down:
        return move(0, 1);
    case RfkDirection::Left:
        return move(-1, 0);
    case RfkDirection::Right:
        return move(1, 0);
    }
    return RfkMove{};
}

RfkSummary RfkWorld::summary() const
{
    RfkSummary s;
    s.won = m_won;
    s.perfect = m_won && m_blocked == 0;
    s.expert = (m_difficulty == RfkDifficulty::Abyss);
    s.maxChain = m_maxChain;
    s.steps = m_steps;
    s.blockedCount = m_blocked;
    s.visitedCells = m_visited;
    s.floorCells = m_floorTotal;
    s.roomCount = roomCount();
    s.roomsVisited = m_room + 1;
    return s;
}

} // namespace whalepet::core
