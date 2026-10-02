#include "core/WorkState.h"

#include <algorithm>
#include <cstring>

namespace whalepet::core {

namespace {

std::string lowerAscii(const std::string &s)
{
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// 取进程名基名并去掉 .exe 后缀（"C:\...\Code.exe" → "code"）
std::string normalizeAppId(const std::string &appId)
{
    std::string base = lowerAscii(appId);
    const std::size_t slash = base.find_last_of("\\/");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".exe") == 0) {
        base.erase(base.size() - 4);
    }
    return base;
}

bool matches(const std::string &normalizedAppId, const char *const *table, std::size_t count)
{
    if (normalizedAppId.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (normalizedAppId == table[i]) {
            return true;
        }
    }
    return false;
}

// ---- 进程名表（已小写、去 .exe）----
const char *const kEditorApps[] = {
    "code", "code-insiders", "codium", "devenv", "qtcreator", "clion", "clion64", "idea", "idea64",
    "pycharm", "pycharm64", "webstorm", "webstorm64", "goland", "goland64", "rider", "rider64",
    "sublime_text", "notepad++", "notepad", "vim", "nvim", "gvim", "emacs", "cursor", "windsurf",
    "zed", "helix",
};
const char *const kTerminalApps[] = {
    "windowsterminal", "wt", "powershell", "pwsh", "cmd", "conhost", "bash", "wsl", "git-bash",
    "mintty", "alacritty", "wezterm", "terminus", "cmder", "conemu64",
};
const char *const kBrowserApps[] = {
    "chrome", "msedge", "firefox", "brave", "opera", "vivaldi", "iexplore", "arc", "360se",
    "360chrome", "qqbrowser", "sogouexplorer",
};
const char *const kMeetingApps[] = {
    "zoom", "teams", "ms-teams", "wemeetapp", "dingtalk", "feishu", "lark", "discord", "skype",
    "slack", "wechat", "weixin", "qq", "telegram",
};
const char *const kGameApps[] = {
    "steam", "steamwebhelper", "epicgameslauncher", "battle.net", "genshinimpact", "yuanshen",
    "leagueclient", "valorant", "wow", "dota2", "cs2", "minecraft",
};
const char *const kOfficeApps[] = {
    "winword", "excel", "powerpnt", "onenote", "outlook", "wps", "et", "wpp", "acrobat",
    "acrord32", "sumatrapdf", "notion", "obsidian", "typora",
};

// ---- 标题关键词表（已小写）----
const char *const kEditorTitleKeywords[] = {
    "visual studio code", "visual studio", "qt creator", "intellij", "pycharm", "clion",
    "sublime text", "notepad++", "neovim", "vim", "cursor", "windsurf",
};
const char *const kTerminalTitleKeywords[] = {
    "windows terminal", "powershell", "命令提示符", "cmd.exe", "终端",
};
const char *const kBrowserTitleKeywords[] = {
    "google chrome", "microsoft edge", "mozilla firefox", "浏览器",
};
const char *const kMeetingTitleKeywords[] = {
    "zoom meeting", "腾讯会议", "飞书", "钉钉", "microsoft teams",
};

template <std::size_t N>
bool matchesAny(const std::string &normalizedAppId, const char *const (&table)[N])
{
    return matches(normalizedAppId, table, N);
}

} // namespace

const char *const kDebugKeywords[] = {
    "debug", "调试", "断点", "breakpoint", "gdb", "lldb", "stack trace", "test explorer",
    "单元测试", "运行测试",
};
const std::size_t kDebugKeywordCount = sizeof(kDebugKeywords) / sizeof(kDebugKeywords[0]);

const char *appCategoryId(AppCategory category)
{
    switch (category) {
    case AppCategory::Unknown:
        return "unknown";
    case AppCategory::Editor:
        return "editor";
    case AppCategory::Terminal:
        return "terminal";
    case AppCategory::Browser:
        return "browser";
    case AppCategory::Meeting:
        return "meeting";
    case AppCategory::Game:
        return "game";
    case AppCategory::Office:
        return "office";
    case AppCategory::Other:
        return "other";
    }
    return "unknown";
}

AppCategory appCategoryFromId(const std::string &id)
{
    for (int i = 0; i <= static_cast<int>(AppCategory::Other); ++i) {
        const AppCategory category = static_cast<AppCategory>(i);
        if (id == appCategoryId(category)) {
            return category;
        }
    }
    return AppCategory::Unknown;
}

bool windowTitleHasAny(const std::string &title, const char *const *keywords, std::size_t count)
{
    if (title.empty() || keywords == nullptr) {
        return false;
    }
    const std::string lowered = lowerAscii(title);
    for (std::size_t i = 0; i < count; ++i) {
        if (lowered.find(keywords[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

AppCategory classifyApp(const std::string &appId, const std::string &windowTitle)
{
    if (appId.empty() && windowTitle.empty()) {
        return AppCategory::Unknown; // 「无数据」≠「未知应用」
    }

    const std::string normalized = normalizeAppId(appId);
    if (matchesAny(normalized, kEditorApps)) {
        return AppCategory::Editor;
    }
    if (matchesAny(normalized, kTerminalApps)) {
        return AppCategory::Terminal;
    }
    if (matchesAny(normalized, kBrowserApps)) {
        return AppCategory::Browser;
    }
    if (matchesAny(normalized, kMeetingApps)) {
        return AppCategory::Meeting;
    }
    if (matchesAny(normalized, kGameApps)) {
        return AppCategory::Game;
    }
    if (matchesAny(normalized, kOfficeApps)) {
        return AppCategory::Office;
    }

    if (windowTitleHasAny(windowTitle, kEditorTitleKeywords,
                          sizeof(kEditorTitleKeywords) / sizeof(kEditorTitleKeywords[0]))) {
        return AppCategory::Editor;
    }
    if (windowTitleHasAny(windowTitle, kTerminalTitleKeywords,
                          sizeof(kTerminalTitleKeywords) / sizeof(kTerminalTitleKeywords[0]))) {
        return AppCategory::Terminal;
    }
    if (windowTitleHasAny(windowTitle, kBrowserTitleKeywords,
                          sizeof(kBrowserTitleKeywords) / sizeof(kBrowserTitleKeywords[0]))) {
        return AppCategory::Browser;
    }
    if (windowTitleHasAny(windowTitle, kMeetingTitleKeywords,
                          sizeof(kMeetingTitleKeywords) / sizeof(kMeetingTitleKeywords[0]))) {
        return AppCategory::Meeting;
    }
    if (windowTitleHasAny(windowTitle, kDebugKeywords, kDebugKeywordCount)) {
        // 标题带调试语义但进程身份不明：仍按「编辑器族」候选处理（见 WorkStateRules）
        return AppCategory::Editor;
    }
    return AppCategory::Other;
}

const char *workStateId(WorkState state)
{
    switch (state) {
    case WorkState::Unknown:
        return "unknown";
    case WorkState::Idle:
        return "idle";
    case WorkState::Reading:
        return "reading";
    case WorkState::Coding:
        return "coding";
    case WorkState::VibeCoding:
        return "vibe-coding";
    case WorkState::Debugging:
        return "debugging";
    case WorkState::Browsing:
        return "browsing";
    case WorkState::Meeting:
        return "meeting";
    case WorkState::Game:
        return "game";
    case WorkState::Afk:
        return "afk";
    }
    return "unknown";
}

WorkState workStateFromId(const std::string &id)
{
    for (int i = 0; i <= static_cast<int>(WorkState::Afk); ++i) {
        const WorkState state = static_cast<WorkState>(i);
        if (id == workStateId(state)) {
            return state;
        }
    }
    return WorkState::Unknown;
}

bool workStateIsFocus(WorkState state)
{
    switch (state) {
    case WorkState::Coding:
    case WorkState::VibeCoding:
    case WorkState::Debugging:
    case WorkState::Meeting:
        return true;
    default:
        return false;
    }
}

const char *workStatePose(WorkState state)
{
    // 全部复用既有 93 张立绘（docs/STATE-MACHINE.md §1 的 pose 名）
    switch (state) {
    case WorkState::Unknown:
        return nullptr; // 不改变立绘
    case WorkState::Idle:
        return "waiting";
    case WorkState::Reading:
        return "thinking";
    case WorkState::Coding:
        return "work-ram";
    case WorkState::VibeCoding:
        return "meme-wakuwaku";
    case WorkState::Debugging:
        return "work-debug";
    case WorkState::Browsing:
        return "curious";
    case WorkState::Meeting:
        return "work-meeting";
    case WorkState::Game:
        return "daily-gaming";
    case WorkState::Afk:
        return "afk";
    }
    return nullptr;
}

const char *workStateScene(WorkState state)
{
    switch (state) {
    case WorkState::Unknown:
        return nullptr; // 不播报
    case WorkState::Idle:
        return "work.idle";
    case WorkState::Reading:
        return "work.reading";
    case WorkState::Coding:
        return "work.coding";
    case WorkState::VibeCoding:
        return "work.vibecoding";
    case WorkState::Debugging:
        return "work.debugging";
    case WorkState::Browsing:
        return "work.browsing";
    case WorkState::Meeting:
        return "work.meeting";
    case WorkState::Game:
        return "work.game";
    case WorkState::Afk:
        return "work.afk";
    }
    return nullptr;
}

} // namespace whalepet::core
