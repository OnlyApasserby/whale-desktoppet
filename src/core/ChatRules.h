#pragma once
// 聊天规则（P5）：分时问候 / 心情分层 / 羁绊专属 / 关键词表情感知。
//
// 本文件是纯逻辑、零 Qt 依赖，便于单测（与 LineTable、PetStateMachine 同层）。
// 规则与阈值**照搬**参考项目 referances/dsh-whale-musume，未自行发明：
//   greetBucket()  → assets/whale-moe-core.js   greetBucket(hour)        1652–1660 行
//   moodTier()     → assets/whale-moe-core.js   moodTier(mood)           726–731 行
//   KEYWORDS       → assets/whale-moe-core.js   KEYWORDS                 744–775 行
//   matchKeyword() → assets/whale-moe-core.js   matchKeyword(text, on)   777–787 行
//   KEYWORD_POSES  → assets/dsh-whale-moe.js    KEYWORD_POSES            2169–2177 行
//
// 说明：docs/CHAT.md §4 原写「13 种梗」，实测源文件 KEYWORD_POSES 共 **21 项**
// （其中 10 项落在 meme-* 立绘、11 项落在 work-*/abstract/bold 立绘），
// 且这 21 项对应的立绘在 assets/poses/ 中全部存在。此处以源文件为准，CHAT.md 已同步修正。

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace whalepet::core {

// ---------------------------------------------------------------------------
// 分时问候
// ---------------------------------------------------------------------------

enum class GreetSlot { Night, Morning, Forenoon, Noon, Afternoon, Evening };

// 照搬 greetBucket()：23:00–05:59 为深夜
inline GreetSlot greetSlot(int hour)
{
    if (hour < 0 || hour > 23) {
        return GreetSlot::Night; // 非法输入保守处理：不主动发言
    }
    if (hour >= 23 || hour < 6) {
        return GreetSlot::Night;
    }
    if (hour < 9) {
        return GreetSlot::Morning;
    }
    if (hour < 12) {
        return GreetSlot::Forenoon;
    }
    if (hour < 14) {
        return GreetSlot::Noon;
    }
    if (hour < 18) {
        return GreetSlot::Afternoon;
    }
    return GreetSlot::Evening;
}

// 深夜返回 nullptr：本项目深夜静默，不主动问候（CHAT.md §2、§5）
inline const char *greetSceneKey(GreetSlot slot)
{
    switch (slot) {
    case GreetSlot::Night:
        return nullptr;
    case GreetSlot::Morning:
        return "greet.morning";
    case GreetSlot::Forenoon:
        return "greet.forenoon";
    case GreetSlot::Noon:
        return "greet.noon";
    case GreetSlot::Afternoon:
        return "greet.afternoon";
    case GreetSlot::Evening:
        return "greet.evening";
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 心情分层（阈值照搬 moodTier：< 40 低落；40~69 中性；>= 70 高涨）
// ---------------------------------------------------------------------------

enum class MoodTier { Low, Mid, High };

inline MoodTier moodTier(int mood)
{
    if (mood < 40) {
        return MoodTier::Low;
    }
    if (mood < 70) {
        return MoodTier::Mid;
    }
    return MoodTier::High;
}

// 中性心情不做台词替换，返回 nullptr
inline const char *moodSceneKey(MoodTier tier)
{
    switch (tier) {
    case MoodTier::Low:
        return "bond.low-mood";
    case MoodTier::High:
        return "bond.high-mood";
    case MoodTier::Mid:
        return nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 羁绊专属（Lv3 解锁动作 / Lv5 解锁称号 / Lv7 解锁彩蛋，对应源 BOND.lv3Action 等）
// ---------------------------------------------------------------------------

inline const char *bondSceneKey(int level)
{
    if (level >= 7) {
        return "bond.l7";
    }
    if (level >= 5) {
        return "bond.l5";
    }
    if (level >= 3) {
        return "bond.l3";
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 关键词匹配
// ---------------------------------------------------------------------------

struct KeywordRule
{
    const char *id;
    const char *const *words;
    std::size_t wordCount;
};

// 各关键词的触发词（照搬 KEYWORDS，含大小写与顺序；顺序即优先级）
inline constexpr const char *kThanksWords[] = { "谢谢", "感谢", "多谢", "thank" };
inline constexpr const char *kTiredWords[] = { "好累", "累了", "困了", "疲惫", "好困" };
inline constexpr const char *kHungryWords[] = { "饿了", "好饿", "吃饭", "夜宵" };
inline constexpr const char *kGoodnightWords[] = { "晚安", "睡了", "去睡" };
inline constexpr const char *kCheerWords[] = { "加油", "冲鸭", "冲呀" };
inline constexpr const char *kHelpWords[] = { "救命", "帮我", "求助", "完蛋" };
inline constexpr const char *kPraiseWords[] = { "太强了", "厉害", "牛", "真棒", "天才" };
inline constexpr const char *kHugWords[] = { "抱抱", "贴贴", "摸摸" };
inline constexpr const char *kCuteWords[] = { "可爱", "萌", "好萌" };
inline constexpr const char *kMorningWords[] = { "早安", "早上好" };
inline constexpr const char *kWorkerWords[] = { "打工人", "打工", "搬砖", "社畜", "上班", "加班" };
inline constexpr const char *kSlackWords[] = { "摸鱼",  "摆烂",   "躺平", "不想上班",
                                              "不想写", "懒得" };
inline constexpr const char *kDdlWords[] = { "ddl", "deadline", "截止", "赶不完", "来不及",
                                             "最后期限" };
inline constexpr const char *kCakeWords[] = { "画饼", "大饼", "pua", "老板", "画大饼" };
inline constexpr const char *kCrazyWords[] = { "发疯",  "破防", "绷不住", "已老实",
                                               "求放过", "啊啊啊", "疯了" };
inline constexpr const char *kFlagWords[] = { "立个 flag", "立 flag", "立flag",
                                              "这把我",    "干完这单", "flag" };
inline constexpr const char *kBugtalkWords[] = { "bug 好玄学", "bug好玄学", "玄学", "改一行",
                                                 "回滚",       "代码坏" };
inline constexpr const char *kKyunWords[] = { "心动", "好可爱", "太可爱", "aww", "心动了",
                                              "可爱死" };
inline constexpr const char *kOmgWords[] = { "我的天", "天哪", "omg", "离谱",  "震惊",
                                             "我靠",   "卧槽", "不是吧" };
inline constexpr const char *kDogeWords[] = { "就这", "呵呵", "笑死", "难蚌", "绷不住笑" };
inline constexpr const char *kSikeWords[] = { "拿下", "搞定", "轻松", "so easy", "稳了",
                                              "小意思" };
inline constexpr const char *kWorshipWords[] = { "大佬", "膜拜", "膝盖", "牛批", "nb", "大神" };
inline constexpr const char *kPeaceWords[] = { "佛系", "随缘", "淡定", "算了算了", "无所谓" };
inline constexpr const char *kDoubtWords[] = { "真的假的", "不会吧", "确定吗", "怀疑", "是吗" };
inline constexpr const char *kWakuwakuWords[] = { "期待", "兴奋",   "冲了",
                                                  "开始吧", "wow", "等不及" };
inline constexpr const char *kSmilepainWords[] = { "无语", "累了累了", "麻了", "已黑化",
                                                   "微笑" };
inline constexpr const char *kOjisanWords[] = { "无聊", "好闲", "没意思", "就这？" };
inline constexpr const char *kDeployWords[] = { "部署", "上线", "发布", "deploy", "release" };
inline constexpr const char *kMeetingWords[] = { "开会", "会议", "例会", "评审会" };
inline constexpr const char *kReviewWords[] = { "review", "评审", "代码审查", "cr" };

// 注意：hug / cute / morning 三项在源文件中有触发词但**没有**对应台词与立绘
// （keyword 分组无这三个 id），仍保留在表中以便与源行为一致；
// 命中后若无候选台词，ChatService 会优雅跳过，不会切表情。
inline constexpr KeywordRule kKeywordRules[] = {
    { "thanks", kThanksWords, 4 },
    { "tired", kTiredWords, 5 },
    { "hungry", kHungryWords, 4 },
    { "goodnight", kGoodnightWords, 3 },
    { "cheer", kCheerWords, 3 },
    { "help", kHelpWords, 4 },
    { "praise", kPraiseWords, 5 },
    { "hug", kHugWords, 3 },
    { "cute", kCuteWords, 3 },
    { "morning", kMorningWords, 2 },
    { "worker", kWorkerWords, 6 },
    { "slack", kSlackWords, 6 },
    { "ddl", kDdlWords, 6 },
    { "cake", kCakeWords, 5 },
    { "crazy", kCrazyWords, 7 },
    { "flag", kFlagWords, 6 },
    { "bugtalk", kBugtalkWords, 6 },
    { "kyun", kKyunWords, 6 },
    { "omg", kOmgWords, 8 },
    { "doge", kDogeWords, 5 },
    { "sike", kSikeWords, 6 },
    { "worship", kWorshipWords, 6 },
    { "peace", kPeaceWords, 5 },
    { "doubt", kDoubtWords, 5 },
    { "wakuwaku", kWakuwakuWords, 6 },
    { "smilepain", kSmilepainWords, 5 },
    { "ojisan", kOjisanWords, 4 },
    { "deploy", kDeployWords, 5 },
    { "meeting", kMeetingWords, 4 },
    { "review", kReviewWords, 4 },
};

inline constexpr std::size_t kKeywordRuleCount = sizeof(kKeywordRules) / sizeof(kKeywordRules[0]);

// 关键词 → 表情立绘（照搬 KEYWORD_POSES 全部 21 项，立绘名见 core/PoseNames.h）
struct KeywordPose
{
    const char *id;
    const char *pose;
};

inline constexpr KeywordPose kKeywordPoses[] = {
    { "kyun", "meme-kyun" },         { "omg", "meme-omg" },
    { "doge", "meme-doge" },         { "sike", "meme-sike" },
    { "worship", "meme-worship" },   { "peace", "meme-peace" },
    { "doubt", "meme-doubt" },       { "wakuwaku", "meme-wakuwaku" },
    { "smilepain", "meme-smile-pain" }, { "ojisan", "meme-ojisan" },
    { "deploy", "work-deploy" },     { "meeting", "work-meeting" },
    { "review", "work-review" },     { "bugtalk", "work-debug" },
    { "ddl", "work-deadline" },      { "cake", "work-boss" },
    { "slack", "work-slack-phone" }, { "crazy", "abstract" },
    { "cheer", "bold" },             { "flag", "bold" },
    { "tired", "work-sleep" },
};

inline constexpr std::size_t kKeywordPoseCount = sizeof(kKeywordPoses) / sizeof(kKeywordPoses[0]);

// 关键词对应立绘名；无立绘（hug / cute / morning 等）返回 nullptr
inline const char *keywordPose(const std::string &id)
{
    for (std::size_t i = 0; i < kKeywordPoseCount; ++i) {
        if (id == kKeywordPoses[i].id) {
            return kKeywordPoses[i].pose;
        }
    }
    return nullptr;
}

// 关键词对应台词场景 key：meme.<id>（语料见 assets/lines/meme.txt）
inline std::string keywordSceneKey(const std::string &id)
{
    return "meme." + id;
}

// 关键词 id 是否合法（录入校验 + UI 下拉用）：必须存在于 kKeywordRules 中。
// 注意：hug / cute / morning 也在表中（有触发词、无立绘），故同样是合法 id。
inline bool keywordIdValid(const std::string &id)
{
    if (id.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < kKeywordRuleCount; ++i) {
        if (id == kKeywordRules[i].id) {
            return true;
        }
    }
    return false;
}

// ASCII 小写化。中文为 UTF-8 多字节，逐字节 tolower 不改变非 ASCII 字节，
// 故行为与 JS 的 String.prototype.toLowerCase() 等价（与源实现一致）。
inline std::string lowerAscii(const std::string &text)
{
    std::string lower;
    lower.reserve(text.size());
    for (const char ch : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower;
}

// ---------------------------------------------------------------------------
// 自定义热词（P6 追加：用户录入「词 → 关键词 id」）
// ---------------------------------------------------------------------------
//
// 语义（docs/CHAT.md §4）：
//   - **优先级高于内置触发词**：按列表顺序（= 录入顺序）首个命中即停；
//   - 匹配方式与内置一致：小写子串包含；
//   - word 为空、或 keywordId 非法（不在 kKeywordRules 中）的条目会被**跳过**，
//     避免一条脏数据把后续所有热词挤掉。

struct CustomHotword
{
    std::string word;
    std::string keywordId;
};

// 文本扫描（照搬 matchKeyword）：**先自定义热词**（按列表顺序），再按 kKeywordRules
// 顺序逐组、组内按 words 顺序做**小写子串**匹配，首个命中即返回该组 id；
// 无命中返回 nullptr。
inline const char *matchKeyword(const std::string &text, const std::vector<CustomHotword> &custom)
{
    if (text.empty()) {
        return nullptr;
    }

    const std::string lowerText = lowerAscii(text);

    for (const CustomHotword &hotword : custom) {
        if (hotword.word.empty() || !keywordIdValid(hotword.keywordId)) {
            continue; // 脏数据跳过，不影响其它热词
        }
        if (lowerText.find(lowerAscii(hotword.word)) != std::string::npos) {
            return hotword.keywordId.c_str();
        }
    }

    for (std::size_t i = 0; i < kKeywordRuleCount; ++i) {
        const KeywordRule &rule = kKeywordRules[i];
        for (std::size_t j = 0; j < rule.wordCount; ++j) {
            if (lowerText.find(lowerAscii(rule.words[j])) != std::string::npos) {
                return rule.id;
            }
        }
    }
    return nullptr;
}

// 兼容重载：无自定义热词时等价于仅内置规则。
inline const char *matchKeyword(const std::string &text)
{
    return matchKeyword(text, std::vector<CustomHotword>{});
}

// ---------------------------------------------------------------------------
// 节流与问候间隔
// ---------------------------------------------------------------------------

// 说话最小间隔（与 core/PetTypes.h 的 kSpeechGapMs 同值，独立常量便于纯逻辑单测）
inline constexpr std::int64_t kChatMinGapMs = 6000;

// 同一天内同一时段只主动问候一次
inline constexpr std::int64_t kGreetRepeatWindowMs = 6 * 60 * 60 * 1000;

} // namespace whalepet::core
