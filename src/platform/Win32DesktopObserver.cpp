#include "platform/Win32DesktopObserver.h"

#include "platform/Win32TextUtil.h"

#include <QDebug>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <utility>

namespace whalepet::platform {

namespace {

// ---------------------------------------------------------------------------
// 低层输入钩子（只计数）
//   * WH_KEYBOARD_LL / WH_MOUSE_LL 的钩子过程在**安装线程**（本进程 GUI 线程）上下文中被调用，
//     不做 DLL 注入，也不需要管理员权限；回调里只做一次原子自增，不拖慢系统；
//   * 只计「按下」类事件（键盘 keydown / 鼠标键与滚轮）；**不计鼠标移动**；
//   * 进程内单例计数：宿主只会装配一个活动采样器（PetWindow 唯一），故用文件级静态量。
// ---------------------------------------------------------------------------
std::atomic<long long> g_inputEvents{0};
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;

LRESULT CALLBACK lowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        g_inputEvents.fetch_add(1, std::memory_order_relaxed);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK lowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            g_inputEvents.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break; // WM_MOUSEMOVE 等一律不计（隐私 + 计数准确性）
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool installInputHooks()
{
    if (g_keyboardHook != nullptr && g_mouseHook != nullptr) {
        return true;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (g_keyboardHook == nullptr) {
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc, instance, 0);
    }
    if (g_mouseHook == nullptr) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, lowLevelMouseProc, instance, 0);
    }
    if (g_keyboardHook == nullptr || g_mouseHook == nullptr) {
        // 任一失败 → 全部收回，避免「半个钩子」造成计数语义不一致
        if (g_keyboardHook != nullptr) {
            UnhookWindowsHookEx(g_keyboardHook);
            g_keyboardHook = nullptr;
        }
        if (g_mouseHook != nullptr) {
            UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = nullptr;
        }
        return false;
    }
    return true;
}

void uninstallInputHooks()
{
    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    if (g_mouseHook != nullptr) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    g_inputEvents.store(0, std::memory_order_relaxed);
}

long long takeInputEvents()
{
    return g_inputEvents.exchange(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// 默认真实读数
// ---------------------------------------------------------------------------
std::wstring windowTitleOf(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
    if (copied <= 0) {
        return std::wstring();
    }
    title.resize(static_cast<std::size_t>(copied));
    return title;
}

std::wstring processPathOf(DWORD pid)
{
    if (pid == 0) {
        return std::wstring();
    }
    // PROCESS_QUERY_LIMITED_INFORMATION：受保护/提权进程通常仍可查询，且不需要 SeDebugPrivilege
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return std::wstring(); // 无权限 → 无数据（不伪造进程名）
    }
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
            path.resize(size);
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || path.size() >= 32768) {
            path.clear();
            break;
        }
        path.resize(path.size() * 2);
    }
    CloseHandle(process);
    return path;
}

Win32ForegroundRaw readForegroundRaw()
{
    Win32ForegroundRaw raw;
    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) {
        return raw; // 锁屏 / 无前台窗口 → 无数据
    }
    raw.valid = true;
    raw.windowTitle = windowTitleOf(hwnd);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    raw.processPath = processPathOf(pid);
    return raw;
}

Win32ActivityRaw readActivityRaw()
{
    Win32ActivityRaw raw;
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) {
        return raw;
    }
    raw.valid = true;
    raw.lastInputTickMs = info.dwTime;
    raw.nowTickMs = GetTickCount();
    return raw;
}

Win32SystemRaw readSystemRaw()
{
    Win32SystemRaw raw;
    raw.valid = true;

    BOOL screensaver = FALSE;
    if (SystemParametersInfoW(SPI_GETSCREENSAVERRUNNING, 0, &screensaver, 0)) {
        raw.screensaverRunning = (screensaver != FALSE);
    }

    // 锁屏后输入桌面切到安全桌面（Winlogon），普通进程拿不到句柄 → 判定为「已离开」
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (desktop == nullptr) {
        raw.sessionLocked = true;
    } else {
        CloseDesktop(desktop);
    }
    return raw;
}

} // namespace

// ---------------------------------------------------------------------------
// Win32ForegroundSampler
// ---------------------------------------------------------------------------
Win32ForegroundSampler::Win32ForegroundSampler(Win32ForegroundReader reader)
    : m_reader(std::move(reader))
{
}

bool Win32ForegroundSampler::sampleForeground(core::EnvSample &out)
{
    const Win32ForegroundRaw raw = m_reader ? m_reader() : readForegroundRaw();
    if (!raw.valid) {
        return false; // 组合层据此清空前台字段（不保留上一次的陈旧值）
    }
    out.windowTitle = win32::toUtf8(raw.windowTitle);
    // 只暴露进程名（去路径），避免把安装路径写进对外快照
    out.appId = win32::fileNameFromPath(win32::toUtf8(raw.processPath));
    return true;
}

// ---------------------------------------------------------------------------
// Win32ActivitySampler
// ---------------------------------------------------------------------------
Win32ActivitySampler::Win32ActivitySampler() = default;

Win32ActivitySampler::Win32ActivitySampler(Win32ActivityReader reader)
    : m_reader(std::move(reader))
{
    // 注入替身读数 → 不安装系统钩子（单测进程不受影响）；默认真实读数 → 倾向低层钩子。
    // 注意不能写成常量初始化：`Win32DesktopObserver()` 会委托到 reader 形参构造函数
    // （三个 reader 皆为空 = 真实读数），若此处恒为 false，生产路径会永久退化为差分降级。
    m_useHooks = !m_reader;
}

Win32ActivitySampler::~Win32ActivitySampler()
{
    // 可靠卸载：宿主即便忘记 stop()，析构也必须收回钩子
    if (m_hooksActive) {
        uninstallInputHooks();
        m_hooksActive = false;
    }
}

void Win32ActivitySampler::setObserving(bool active)
{
    if (m_reader) {
        m_hasPrevTick = false; // 替身读数：无系统资源可管，只复位差分基准
        return;
    }

    if (!active) {
        if (m_hooksActive) {
            uninstallInputHooks();
            m_hooksActive = false;
            qInfo() << "[Win32DesktopObserver] 低层输入钩子已卸载";
        }
        m_hasPrevTick = false;
        return;
    }

    // 重新开始观察：丢弃上一轮的空闲基准与残留计数（不把「停止期间」算成输入）
    m_hasPrevTick = false;
    if (m_useHooks && !m_hooksActive) {
        m_hooksActive = installInputHooks();
        takeInputEvents();
        if (!m_hooksActive) {
            qWarning() << "[Win32DesktopObserver] 低层输入钩子安装失败 → 降级为 GetLastInputInfo 差分"
                          "（inputEvents 语义退化为「窗口内有输入的秒数」，Vibe Coding 判据可能不可达）";
        }
    }
    qInfo() << "[Win32DesktopObserver] 输入活跃度采集已启用，计数来源 =" << sourceName();
}

bool Win32ActivitySampler::sampleActivity(core::EnvSample &out)
{
    const Win32ActivityRaw raw = m_reader ? m_reader() : readActivityRaw();
    if (!raw.valid) {
        return false;
    }

    const std::int64_t idleMs = (raw.nowTickMs >= raw.lastInputTickMs)
        ? static_cast<std::int64_t>(raw.nowTickMs - raw.lastInputTickMs)
        : 0; // GetTickCount 回绕等异常输入：不产出负值

    int delta = 0;
    if (m_hooksActive) {
        delta = static_cast<int>(takeInputEvents());
    } else if (m_hasPrevTick && raw.lastInputTickMs != m_prevLastInputTickMs) {
        // 差分只能回答「上次采样以来有没有输入」，故每次至多计 1 —— 诚实计数，不放大
        delta = 1;
    }
    m_prevLastInputTickMs = raw.lastInputTickMs;
    m_hasPrevTick = true;

    out.idleMs = idleMs;
    out.inputEvents = delta;
    out.hasInput = (delta > 0) || (idleMs < kWin32InputActiveMs);
    return true;
}

const char *Win32ActivitySampler::sourceName() const
{
    return m_hooksActive ? "low-level hooks" : "GetLastInputInfo differential";
}

// ---------------------------------------------------------------------------
// Win32SystemStatusSampler
// ---------------------------------------------------------------------------
Win32SystemStatusSampler::Win32SystemStatusSampler(Win32SystemReader reader)
    : m_reader(std::move(reader))
{
}

bool Win32SystemStatusSampler::sampleSystemStatus(core::EnvSample &out)
{
    const Win32SystemRaw raw = m_reader ? m_reader() : readSystemRaw();
    if (!raw.valid) {
        return false;
    }
    out.systemPaused = raw.sessionLocked || raw.screensaverRunning;
    return true;
}

// ---------------------------------------------------------------------------
// Win32DesktopObserver
// ---------------------------------------------------------------------------
Win32DesktopObserver::Win32DesktopObserver()
    : Win32DesktopObserver(nullptr, nullptr, nullptr)
{
}

Win32DesktopObserver::Win32DesktopObserver(Win32ForegroundReader foreground,
                                           Win32ActivityReader activity,
                                           Win32SystemReader system)
    : m_foreground(std::move(foreground))
    , m_activity(std::move(activity))
    , m_system(std::move(system))
{
    m_composite.setForegroundSampler(&m_foreground);
    m_composite.setActivitySampler(&m_activity);
    m_composite.setSystemStatusSampler(&m_system);
}

Win32DesktopObserver::~Win32DesktopObserver()
{
    // 钩子卸载的第二道保险（第一道是 EnvironmentService::stop）
    m_activity.setObserving(false);
}

bool Win32DesktopObserver::available() const
{
    return m_composite.available();
}

core::EnvSample Win32DesktopObserver::sample(std::int64_t nowMs)
{
    return m_composite.sample(nowMs);
}

void Win32DesktopObserver::setObserving(bool active)
{
    m_composite.setObserving(active);
}

} // namespace whalepet::platform
