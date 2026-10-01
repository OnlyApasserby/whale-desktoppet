// 小游戏「鲸鱼娘找小猫」纯逻辑单测：
//   物体表解析 / 地图解析与校验 / 四方向移动与撞墙 / 物体交互与一次性消费 /
//   场景切换 / 通关判定与结算快照 / 主动结束 / 通用结算折算 / 难度表。
// 约定同其它 core 用例：零 Qt UI 依赖（QTEST_GUILESS_MAIN）。
// 参考：docs/MINIGAME-INTERFACE.md §10。

#include "core/LineTable.h"
#include "core/RobotKitten.h"

#include <QtTest>

#include <QFile>
#include <QString>

#include <initializer_list>
#include <string>
#include <vector>

using namespace whalepet::core;

namespace {

// 测试用物体表：覆盖「可选列留空 → 用类别缺省台词场景 key」与注释行
const char *kTestTable =
    "; 测试用物体表\n"
    "\n"
    ".|floor|海床|floor|\n"
    "@|player|鲸鱼娘|player||鲸\n"
    "#|wall|礁石|blocker|kitten.blocked|\n"
    "k|kitten|小猫|kitten|kitten.found|猫\n"
    ">|exit|海流|exit|kitten.scene|门\n"
    "o|shell|扇贝|toy|kitten.shell|贝\n"
    "b|bottle|漂流瓶|junk||\n"; // scene 留空 → 类别缺省 kitten.junk

RfkObjectTable testTable()
{
    return RfkObjectTable::parse(kTestTable);
}

std::vector<std::string> rooms(std::initializer_list<const char *> texts)
{
    std::vector<std::string> out;
    out.reserve(texts.size());
    for (const char *text : texts) {
        out.emplace_back(text);
    }
    return out;
}

// 四方向 BFS：判断场景内 from 能否走到 to（用于校验随包迷宫的目标可达性）
bool reachable(const RfkRoom &room, int from, int to)
{
    if (from < 0 || to < 0 || from >= room.cellCount() || to >= room.cellCount()) {
        return false;
    }
    std::vector<char> seen(static_cast<std::size_t>(room.cellCount()), 0);
    std::vector<int> queue;
    queue.push_back(from);
    seen[static_cast<std::size_t>(from)] = 1;
    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};
    while (!queue.empty()) {
        const int current = queue.back();
        queue.pop_back();
        if (current == to) {
            return true;
        }
        const int x = current % room.width;
        const int y = current / room.width;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k];
            const int ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= room.width || ny >= room.height) {
                continue;
            }
            const int next = ny * room.width + nx;
            if (room.cells[static_cast<std::size_t>(next)].kind == RfkKind::Blocker
                || seen[static_cast<std::size_t>(next)] != 0) {
                continue;
            }
            seen[static_cast<std::size_t>(next)] = 1;
            queue.push_back(next);
        }
    }
    return false;
}

// 读取随包资源（编译期由 CMake 传入目录，与运行期 qrc 同源）
std::string readAsset(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return file.readAll().toStdString();
}

QString mapsDir()
{
    return QString::fromLatin1(WHALEPET_MAPS_DIR);
}

} // namespace

class TestKitten : public QObject {
    Q_OBJECT

private slots:
    void objectTableParsesAndSkipsBadLines();
    void defaultTableIsPlayable();
    void roomParsingPadsShortRowsAndRejectsErrors();
    void roomKeepsLeadingSpacesAsFloor();
    void loadingRequiresEnoughRooms();
    void movesAndBlocks();
    void interactsWithObjectsOnlyOnce();
    void exitSwitchesScene();
    void kittenEndsGameAndSummary();
    void abandonEndsWithoutWin();
    void resultConversionAndGrade();
    void difficultyTableMatchesSpec();
    void bundledMapsArePlayable();
    void bundledLinesCoverObjectScenes();
};

void TestKitten::objectTableParsesAndSkipsBadLines()
{
    const std::string text =
        "; 注释行\n"
        "\n"
        "@|player|鲸鱼娘|player||鲸\n"
        "#|wall|礁石|blocker|kitten.blocked|\n"
        "k|kitten|小猫|kitten|kitten.found|猫\n"
        "oo|bad|glyph 两个字符|toy||\n"   // glyph 非单字符 → 跳过
        "z|zombie|类别未知|monster||\n"  // 类别未知 → 跳过
        "q|short|缺列\n"                  // 列数不足 → 跳过
        "|empty|glyph 为空|toy||\n"       // glyph 为空 → 跳过
        "o|shell|扇贝|toy|kitten.shell|贝\n"
        "o|shell2|扇贝二号|toy|kitten.shell2|贝\n"; // 同 glyph 后者覆盖

    const RfkObjectTable table = RfkObjectTable::parse(text);
    QCOMPARE(static_cast<int>(table.size()), 4); // @ / # / k / o

    const RfkObjectDef *player = table.find('@');
    QVERIFY(player != nullptr);
    QVERIFY(player->kind == RfkKind::Player);
    QVERIFY(player->display == "鲸");

    const RfkObjectDef *o = table.find('o');
    QVERIFY(o != nullptr);
    QVERIFY(o->id == "shell2"); // 覆盖生效
    QVERIFY(o->kind == RfkKind::Toy);
    QVERIFY(o->scene == "kitten.shell2");
    QVERIFY(o->display == "贝");

    QVERIFY(table.find('x') == nullptr);

    // 类别 id ↔ 枚举互为逆映射
    QVERIFY(std::string(rfkKindId(RfkKind::Junk)) == "junk");
    RfkKind kind = RfkKind::Floor;
    QVERIFY(rfkKindFromId("kitten", &kind));
    QVERIFY(kind == RfkKind::Kitten);
    QVERIFY(!rfkKindFromId("monster", &kind));

    // 类别缺省台词场景 key（规格：见 docs/MINIGAME-INTERFACE.md §10）
    QVERIFY(std::string(rfkKindScene(RfkKind::Kitten)) == "kitten.found");
    QVERIFY(std::string(rfkKindScene(RfkKind::Blocker)) == "kitten.blocked");
    QVERIFY(std::string(rfkKindScene(RfkKind::Toy)) == "kitten.item");
    QVERIFY(std::string(rfkKindScene(RfkKind::Junk)) == "kitten.junk");
    QVERIFY(std::string(rfkKindScene(RfkKind::Exit)) == "kitten.scene");
    QVERIFY(std::string(rfkKindScene(RfkKind::Floor)).empty());
}

void TestKitten::defaultTableIsPlayable()
{
    // 外部物体表资源缺失时的兜底：必须仍能解析一张最小地图
    const RfkObjectTable table = rfkDefaultObjectTable();
    QVERIFY(table.find('@') != nullptr);
    QVERIFY(table.find('k') != nullptr && table.find('k')->kind == RfkKind::Kitten);
    QVERIFY(table.find('#') != nullptr && table.find('#')->kind == RfkKind::Blocker);
    QVERIFY(table.find('>') != nullptr && table.find('>')->kind == RfkKind::Exit);

    RfkRoom room;
    std::string error;
    QVERIFY2(rfkParseRoom("###\n#@#\n#k#\n###", table, room, &error), error.c_str());
    QCOMPARE(room.width, 3);
    QCOMPARE(room.height, 4);
    QVERIFY(room.cells[static_cast<std::size_t>(room.startIndex)].kind == RfkKind::Player);

    RfkWorld world;
    QVERIFY(world.load(table, RfkDifficulty::Shallow, rooms({"###\n#@#\n#k#\n###"}), &error));
    QVERIFY(world.moveDir(RfkDirection::Down).won);
}

void TestKitten::roomParsingPadsShortRowsAndRejectsErrors()
{
    const RfkObjectTable table = testTable();
    RfkRoom room;
    std::string error;

    // 短行右侧补空地：第 2 行 "#.#" 只有 3 列，其余按 floor 补齐
    QVERIFY2(rfkParseRoom("#####\n#@k#\n#.#\n#####", table, room, &error), error.c_str());
    QCOMPARE(room.width, 5);
    QCOMPARE(room.height, 4);
    QCOMPARE(room.cellCount(), 20);
    QVERIFY(room.cells[static_cast<std::size_t>(2 * 5 + 3)].kind == RfkKind::Floor);
    QVERIFY(room.cells[static_cast<std::size_t>(2 * 5 + 4)].kind == RfkKind::Floor);
    QVERIFY2(room.startIndex == 1 * 5 + 1, "起点应为 '@' 所在格");
    QVERIFY(room.exitIndex < 0); // 本图没有出口

    // 未知字符 → 报错并给出原因
    std::string unknownError;
    QVERIFY(!rfkParseRoom("#####\n#@Z#\n#####", table, room, &unknownError));
    QVERIFY(!unknownError.empty());

    // 缺起点 / 多起点 → 报错
    std::string noStartError;
    QVERIFY(!rfkParseRoom("#####\n#...#\n#####", table, room, &noStartError));
    std::string twoStartError;
    QVERIFY(!rfkParseRoom("#####\n#@@.#\n#####", table, room, &twoStartError));

    // 只有注释 / 空行 → 报错
    std::string emptyError;
    QVERIFY(!rfkParseRoom("; 只有注释\n\n", table, room, &emptyError));

    // 注释行不影响地图内容
    RfkRoom commented;
    QVERIFY(rfkParseRoom("; 场景说明\n#####\n#@..#\n#####\n; 结束", table, commented, &error));
    QCOMPARE(commented.height, 3);

    // 物体表漏写地面字符时，'.' 与空格仍按地面兜底（用户自定义物体表漏项不至于整图不可用）
    const RfkObjectTable minimal =
        RfkObjectTable::parse("@|player|鲸鱼娘|player||\n#|wall|礁石|blocker||\n");
    QCOMPARE(static_cast<int>(minimal.size()), 2);
    RfkRoom fallback;
    QVERIFY(rfkParseRoom("###\n#@#\n#.#\n###", minimal, fallback, &error));
    QVERIFY(fallback.cells[static_cast<std::size_t>(2 * 3 + 1)].kind == RfkKind::Floor);
    QVERIFY(rfkParseRoom("###\n#@#\n# #\n###", minimal, fallback, &error));
    QVERIFY(fallback.cells[static_cast<std::size_t>(2 * 3 + 1)].kind == RfkKind::Floor);
}

// 地图行**不做 trim**：空格与 '.' 一样是合法地面。
// 若行首空格被吃掉，整行会左移一格 → 视觉地图与判定数据错位（「隐形墙」的成因之一）。
void TestKitten::roomKeepsLeadingSpacesAsFloor()
{
    const RfkObjectTable table = testTable();
    RfkRoom room;
    std::string error;

    // 第 2 行以空格开头：'@' 必须落在第 2 列（index = 1*4+1），而不是第 1 列
    QVERIFY2(rfkParseRoom("####\n @.k\n#  #\n####", table, room, &error), error.c_str());
    QCOMPARE(room.width, 4);
    QCOMPARE(room.height, 4);
    QCOMPARE(room.startIndex, 1 * 4 + 1);
    QVERIFY(room.cells[static_cast<std::size_t>(1 * 4 + 0)].kind == RfkKind::Floor);
    QVERIFY(room.cells[static_cast<std::size_t>(1 * 4 + 2)].kind == RfkKind::Floor);

    // 行尾空格同样保留：列宽按「所见即所得」计入
    RfkRoom tail;
    QVERIFY2(rfkParseRoom("###\n#@ \n###", table, tail, &error), error.c_str());
    QCOMPARE(tail.width, 3);
    QCOMPARE(tail.startIndex, 1 * 3 + 1);

    // 全空白行与（可缩进的）';' 注释行仍被忽略
    RfkRoom commented;
    QVERIFY2(rfkParseRoom("; 说明\n   \n   ; 缩进注释\n###\n#@#\n###", table, commented, &error),
             error.c_str());
    QCOMPARE(commented.height, 3);
}

void TestKitten::loadingRequiresEnoughRooms()
{
    const RfkObjectTable table = testTable();
    RfkWorld world;
    std::string error;

    // 珊瑚湾需要 2 个场景，只给 1 个 → 失败且世界保持未载入
    QVERIFY(!world.load(table, RfkDifficulty::Coral, rooms({"#####\n#@..#\n#####"}), &error));
    QVERIFY(!error.empty());
    QVERIFY(!world.loaded());

    // 场景内容非法（缺起点）→ 失败
    std::string badError;
    QVERIFY(!world.load(table, RfkDifficulty::Shallow, rooms({"#####\n#...#\n#####"}), &badError));
    QVERIFY(!world.loaded());

    // 合法：浅滩 1 个场景
    QVERIFY(world.load(table, RfkDifficulty::Shallow, rooms({"#####\n#@..#\n#####"}), &error));
    QVERIFY(world.loaded());
    QCOMPARE(world.roomCount(), 1);
    QCOMPARE(world.roomIndex(), 0);
    QCOMPARE(world.playerX(), 1);
    QCOMPARE(world.playerY(), 1);
    QCOMPARE(world.steps(), 0);
    QCOMPARE(world.blockedCount(), 0);
    QCOMPARE(world.visitedCells(), 1); // 起点已算作走过
    QVERIFY(!world.finished());
}

void TestKitten::movesAndBlocks()
{
    const RfkObjectTable table = testTable();
    RfkWorld world;
    std::string error;
    QVERIFY(world.load(table, RfkDifficulty::Shallow, rooms({"#####\n#@o.#\n#####"}), &error));

    // 向右 → 踩到扇贝（toy）：移动 + 交互 + 专属台词场景
    const RfkMove toShell = world.moveDir(RfkDirection::Right);
    QVERIFY(toShell.moved);
    QVERIFY(!toShell.blocked);
    QVERIFY(toShell.interacted);
    QVERIFY(toShell.interactKind == RfkKind::Toy);
    QVERIFY(toShell.interactName == "扇贝");
    QVERIFY(toShell.interactScene == "kitten.shell");
    QCOMPARE(world.steps(), 1);
    QCOMPARE(world.maxChain(), 1);
    QCOMPARE(world.playerX(), 2);

    // 向左 → 回到起点（地面，无交互）
    const RfkMove back = world.moveDir(RfkDirection::Left);
    QVERIFY(back.moved);
    QVERIFY(!back.interacted);
    QCOMPARE(world.steps(), 2);

    // 向上 → 撞礁石：不移动、给出障碍物名称、当前连击清零（峰值保留）
    const RfkMove up = world.moveDir(RfkDirection::Up);
    QVERIFY(up.blocked);
    QVERIFY(!up.moved);
    QVERIFY(up.blockerName == "礁石");
    QCOMPARE(world.blockedCount(), 1);
    QCOMPARE(world.steps(), 2); // 撞墙不计步数
    QCOMPARE(world.maxChain(), 2);

    // 贴着左边界再往左 → 同样是墙（地图外墙）
    const RfkMove leftEdge = world.moveDir(RfkDirection::Left);
    QVERIFY(leftEdge.blocked);
    QCOMPARE(world.blockedCount(), 2);

    // 斜向 / 原地 / 越界步长 → 一律忽略（不移动不报错）
    QVERIFY(!world.move(1, 1).moved);
    QVERIFY(!world.move(1, 1).blocked);
    QVERIFY(!world.move(0, 0).moved);
    QVERIFY(!world.move(2, 0).moved);
    QCOMPARE(world.steps(), 2);
    QCOMPARE(world.blockedCount(), 2);

    // 连续顺畅移动：撞墙后连击归零，连走两步后峰值仍为 2
    QVERIFY(world.moveDir(RfkDirection::Right).moved);
    QVERIFY(world.moveDir(RfkDirection::Right).moved);
    QCOMPARE(world.maxChain(), 2);

    // 地图外是「越界」而非墙：无外墙的地图上撞边界同样只会被挡回，不会走出地图
    RfkWorld edge;
    QVERIFY(edge.load(table, RfkDifficulty::Shallow, rooms({"...\n.@.\n..."}), &error));
    QVERIFY(edge.moveDir(RfkDirection::Left).moved);
    const RfkMove outside = edge.moveDir(RfkDirection::Left);
    QVERIFY(outside.blocked);
    QVERIFY(!outside.moved);
    QCOMPARE(edge.blockedCount(), 1);
    QCOMPARE(edge.playerX(), 0); // 仍在最左列，没有走出地图
}

void TestKitten::interactsWithObjectsOnlyOnce()
{
    const RfkObjectTable table = testTable();
    RfkWorld world;
    std::string error;
    QVERIFY(world.load(table, RfkDifficulty::Shallow, rooms({"#####\n#@ob#\n#####"}), &error));

    // 扇贝 → 漂流瓶（junk）：类别不同，且杂物可用类别缺省台词
    QVERIFY(world.moveDir(RfkDirection::Right).interacted);
    const RfkMove toJunk = world.moveDir(RfkDirection::Right);
    QVERIFY(toJunk.interacted);
    QVERIFY(toJunk.interactKind == RfkKind::Junk);
    QVERIFY(toJunk.interactName == "漂流瓶");
    QVERIFY(toJunk.interactScene == "kitten.junk"); // scene 留空 → 类别缺省

    // 原路返回再回来：物品已被消费，不再重复触发（避免来回刷台词）
    QVERIFY(world.moveDir(RfkDirection::Left).moved);
    const RfkMove back = world.moveDir(RfkDirection::Left);
    QVERIFY(back.moved);
    QVERIFY(!back.interacted); // 扇贝已消费

    QVERIFY(world.moveDir(RfkDirection::Right).moved);
    const RfkMove again = world.moveDir(RfkDirection::Right);
    QVERIFY(again.moved);
    QVERIFY(!again.interacted); // 漂流瓶已消费
}

void TestKitten::exitSwitchesScene()
{
    const RfkObjectTable table = testTable();
    RfkWorld world;
    std::string error;
    QVERIFY(world.load(table, RfkDifficulty::Coral,
                       rooms({"#####\n#@.>#\n#####", "#####\n#.@.#\n#####"}), &error));

    QVERIFY(world.moveDir(RfkDirection::Right).moved);
    const RfkMove throughExit = world.moveDir(RfkDirection::Right);
    QVERIFY(throughExit.moved);
    QVERIFY(throughExit.sceneChanged);
    QVERIFY(!throughExit.interacted); // 出口不是可交互物品
    QCOMPARE(world.roomIndex(), 1);
    QCOMPARE(throughExit.sceneIndex, 1);
    // 切换后角色落在新场景的起点上
    QCOMPARE(world.playerIndex(), world.room(1).startIndex);
    QCOMPARE(world.playerX(), 2);
    QCOMPARE(world.playerY(), 1);
    // 进度按全部场景累计：起点 + 中间格 + 出口 + 新场景起点
    QVERIFY(world.floorCells() > 0);
    QCOMPARE(world.visitedCells(), 4);

    // 最后一个场景里的出口不再切换（已无后续场景）
    RfkWorld single;
    QVERIFY(single.load(table, RfkDifficulty::Shallow, rooms({"#####\n#@.>#\n#####"}), &error));
    QVERIFY(single.moveDir(RfkDirection::Right).moved);
    const RfkMove last = single.moveDir(RfkDirection::Right);
    QVERIFY(last.moved);
    QVERIFY(!last.sceneChanged);
    QCOMPARE(single.roomIndex(), 0);
}

void TestKitten::kittenEndsGameAndSummary()
{
    const RfkObjectTable table = testTable();
    RfkWorld world;
    std::string error;
    QVERIFY(world.load(table, RfkDifficulty::Shallow, rooms({"#####\n#@.k#\n#####"}), &error));

    QVERIFY(world.moveDir(RfkDirection::Right).moved);
    const RfkMove found = world.moveDir(RfkDirection::Right);
    QVERIFY(found.moved);
    QVERIFY(found.won);
    QVERIFY(found.interacted);
    QVERIFY(found.interactKind == RfkKind::Kitten);
    QVERIFY(found.interactName == "小猫");
    QVERIFY(found.interactScene == "kitten.found");

    QVERIFY(world.finished());
    QVERIFY(world.won());

    // 结束后不再接受任何操作
    const RfkMove after = world.moveDir(RfkDirection::Left);
    QVERIFY(!after.moved);
    QVERIFY(!after.blocked);
    QVERIFY(!after.won);

    const RfkSummary s = world.summary();
    QVERIFY(s.won);
    QVERIFY(s.perfect); // 全程未撞墙
    QVERIFY(!s.expert); // 浅滩不算高难档
    QCOMPARE(s.steps, 2);
    QCOMPARE(s.maxChain, 2);
    QCOMPARE(s.blockedCount, 0);
    QCOMPARE(s.roomCount, 1);
    QCOMPARE(s.floorCells, 3); // '@' + '.' + 'k'
    QCOMPARE(s.visitedCells, 3);
    QCOMPARE(s.roomsVisited, 1);
}

void TestKitten::abandonEndsWithoutWin()
{
    const RfkObjectTable table = testTable();
    RfkWorld world;
    std::string error;
    QVERIFY(world.load(table, RfkDifficulty::Shallow, rooms({"#####\n#@.k#\n#####"}), &error));

    // 撞一次墙再放弃：不算零失误
    QVERIFY(world.moveDir(RfkDirection::Up).blocked);
    QVERIFY(world.moveDir(RfkDirection::Right).moved);
    world.abandon();

    QVERIFY(world.finished());
    QVERIFY(!world.won());

    const RfkSummary s = world.summary();
    QVERIFY(!s.won);
    QVERIFY(!s.perfect);
    QCOMPARE(s.blockedCount, 1);
    QCOMPARE(s.visitedCells, 2); // 起点 + 走过的地面（撞墙不算）

    // 放弃后不再移动
    QVERIFY(!world.moveDir(RfkDirection::Right).moved);
}

void TestKitten::resultConversionAndGrade()
{
    RfkSummary s;
    s.won = true;
    s.perfect = true;
    s.expert = true;
    s.maxChain = 7;
    s.steps = 30;
    s.visitedCells = 12;
    s.floorCells = 20;

    const MiniGameResult r = rfkGameResult(s, RfkDifficulty::Abyss, 4321);
    QVERIFY(r.gameId == "kitten");
    QVERIFY(r.difficultyId == "abyss");
    QVERIFY(r.difficultyLabel == "深海遗迹");
    QVERIFY(r.won);
    QVERIFY(r.perfect);
    QVERIFY(r.expert);
    QCOMPARE(r.maxChain, 7);
    QCOMPARE(r.progressDone, 12);
    QCOMPARE(r.progressTotal, 20);
    QCOMPARE(static_cast<int>(r.elapsedMs), 4321);
    QCOMPARE(gameGrade(r), GameGrade::Win); // 通关优先

    // 未找到小猫：进度过半 → 及格档；不足半数 → 失败档
    MiniGameResult half;
    half.progressDone = 10;
    half.progressTotal = 20;
    QCOMPARE(gameGrade(half), GameGrade::Draw);

    MiniGameResult less;
    less.progressDone = 9;
    less.progressTotal = 20;
    QCOMPARE(gameGrade(less), GameGrade::Lose);
}

void TestKitten::difficultyTableMatchesSpec()
{
    QCOMPARE(kRfkDifficultyCount, 3);
    // 三个难度分别需要穿越 1 / 2 / 3 个场景
    QCOMPARE(rfkRoomCount(RfkDifficulty::Shallow), 1);
    QCOMPARE(rfkRoomCount(RfkDifficulty::Coral), 2);
    QCOMPARE(rfkRoomCount(RfkDifficulty::Abyss), 3);

    QVERIFY(std::string(rfkDifficultyId(RfkDifficulty::Shallow)) == "shallow");
    QVERIFY(std::string(rfkDifficultyId(RfkDifficulty::Coral)) == "coral");
    QVERIFY(std::string(rfkDifficultyId(RfkDifficulty::Abyss)) == "abyss");
    QVERIFY(std::string(rfkDifficultyName(RfkDifficulty::Coral)) == "珊瑚湾");

    // 落库整数值 ↔ 难度：越界一律回退浅滩
    QVERIFY(rfkDifficultyOfIndex(0) == RfkDifficulty::Shallow);
    QVERIFY(rfkDifficultyOfIndex(1) == RfkDifficulty::Coral);
    QVERIFY(rfkDifficultyOfIndex(2) == RfkDifficulty::Abyss);
    QVERIFY(rfkDifficultyOfIndex(3) == RfkDifficulty::Shallow);
    QVERIFY(rfkDifficultyOfIndex(-1) == RfkDifficulty::Shallow);

    // 未知难度取值也不崩溃（回退浅滩）
    const auto unknown = static_cast<RfkDifficulty>(42);
    QCOMPARE(rfkRoomCount(unknown), 1);
    QVERIFY(std::string(rfkDifficultyName(unknown)) == "浅滩");
}

// 随包资源的自洽性校验（与运行期加载的是同一批文件）：
//   * 物体表可解析；
//   * 每个难度的场景数、地图解析、以及「起点 → 出口 / 小猫」的可达性；
// 这能拦住手绘迷宫时的低级错误（例如小猫被墙围死、非末场景忘了放出口）。
void TestKitten::bundledMapsArePlayable()
{
    bool ok = false;
    const std::string tableText = readAsset(mapsDir() + QStringLiteral("/kitten_objects.txt"), &ok);
    QVERIFY2(ok, qPrintable(QStringLiteral("物体表打不开：%1/kitten_objects.txt").arg(mapsDir())));
    const RfkObjectTable table = RfkObjectTable::parse(tableText);
    QVERIFY(table.size() >= 10); // 地面 / 玩家 / 墙 / 出口 / 小猫 + 8 个物件

    const char *const prefix[] = {"easy", "normal", "expert"};
    for (int d = 0; d < kRfkDifficultyCount; ++d) {
        const RfkDifficulty difficulty = kRfkDifficulties[d].difficulty;
        const int roomTotal = rfkRoomCount(difficulty);

        std::vector<std::string> texts;
        for (int i = 1; i <= roomTotal; ++i) {
            const QString path = mapsDir() + QStringLiteral("/kitten_%1_%2.txt")
                                                   .arg(QString::fromLatin1(prefix[d]))
                                                   .arg(i);
            bool fileOk = false;
            const std::string text = readAsset(path, &fileOk);
            QVERIFY2(fileOk, qPrintable(QStringLiteral("地图打不开：%1").arg(path)));
            texts.push_back(text);
        }

        RfkWorld world;
        std::string error;
        QVERIFY2(world.load(table, difficulty, texts, &error), error.c_str());
        QCOMPARE(world.roomCount(), roomTotal);

        for (int i = 0; i < roomTotal; ++i) {
            const RfkRoom &room = world.room(i);
            QVERIFY(room.startIndex >= 0);
            if (i == roomTotal - 1) {
                // 最后一个场景必须有小猫，且必须从起点可达
                int kitten = -1;
                for (int c = 0; c < room.cellCount(); ++c) {
                    if (room.cells[static_cast<std::size_t>(c)].kind == RfkKind::Kitten) {
                        kitten = c;
                        break;
                    }
                }
                QVERIFY2(kitten >= 0,
                         qPrintable(QStringLiteral("难度「%1」的最后一个场景没有小猫")
                                        .arg(QString::fromUtf8(rfkDifficultyName(difficulty)))));
                QVERIFY2(reachable(room, room.startIndex, kitten),
                         "小猫不可达（迷宫设计错误）");
            } else {
                // 非末场景必须有出口，且出口可达
                QVERIFY2(room.exitIndex >= 0, "非末场景缺少出口（>）");
                QVERIFY2(reachable(room, room.startIndex, room.exitIndex),
                         "出口不可达（迷宫设计错误）");
            }
        }
    }
}

// 台词库覆盖：物体表声明的每个台词场景 key（含类别缺省）都必须在 kitten.txt 中有台词，
// 避免出现「走到物件上却一句话也不说」的静默降级。
void TestKitten::bundledLinesCoverObjectScenes()
{
    bool ok = false;
    const std::string tableText = readAsset(mapsDir() + QStringLiteral("/kitten_objects.txt"), &ok);
    QVERIFY(ok);
    const RfkObjectTable table = RfkObjectTable::parse(tableText);

    const std::string linesText =
        readAsset(QString::fromLatin1(WHALEPET_LINES_DIR) + QStringLiteral("/kitten.txt"), &ok);
    QVERIFY2(ok, "台词库打不开：assets/lines/kitten.txt");
    LineTable lines;
    QVERIFY(lines.loadFromText(linesText) > 0);

    for (const RfkObjectDef &def : table.defs()) {
        const std::string scene = def.scene.empty() ? rfkKindScene(def.kind) : def.scene;
        if (scene.empty()) {
            continue; // 地面 / 起点不播台词
        }
        QVERIFY2(lines.hasScene(scene),
                 qPrintable(QStringLiteral("物体 %1 的台词场景 %2 在 kitten.txt 中缺失")
                                .arg(QString::fromStdString(def.id),
                                     QString::fromStdString(scene))));
    }

    // 通用播报场景同样必须齐全
    for (const char *key : {"kitten.start", "kitten.scene", "kitten.blocked", "kitten.found",
                            "kitten.win", "kitten.lose", "kitten.item", "kitten.junk"}) {
        QVERIFY2(lines.hasScene(key), key);
    }
}

QTEST_GUILESS_MAIN(TestKitten)
#include "test_kitten.moc"
