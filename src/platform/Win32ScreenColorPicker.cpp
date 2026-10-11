#include "platform/Win32ScreenColorPicker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <magnification.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace whalepet::platform {

namespace {

constexpr wchar_t kMagnifierHostClass[] = L"WhalePetMagnifierHost";
// 放大镜与光标之间必须留出的空隙（物理像素）：放大镜窗口若盖住光标点，
// 1×1 BitBlt 采到的就是放大镜自身的像素（取色错误），故摆位时强制避开。
constexpr int kMagnifierCursorGap = 22;
constexpr int kMagnifierMinSize = 40;
constexpr int kMagnifierMaxSize = 400;
constexpr float kMagnifierMinFactor = 1.0f;
constexpr float kMagnifierMaxFactor = 8.0f;

HINSTANCE moduleInstance()
{
    return ::GetModuleHandleW(nullptr);
}

// 放大镜宿主窗口过程：点击穿透 + 不激活（它只是「看得见的读数」，交互一律交给覆盖层）
LRESULT CALLBACK magnifierHostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (message == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

bool ensureHostClassRegistered()
{
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = magnifierHostProc;
    wc.hInstance = moduleInstance();
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMagnifierHostClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

// 虚拟桌面矩形（物理像素；多显示器时原点可为负）
bool virtualDesktopRect(RECT *out)
{
    if (out == nullptr) {
        return false;
    }
    out->left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    out->top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    out->right = out->left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    out->bottom = out->top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return out->right > out->left && out->bottom > out->top;
}

// GDI 资源 RAII：DISPLAY 内存 DC + 兼容位图（沿用 P11 调研时核对的 WinEase「ScreenDcGuard」思路，
// 只保留本项目需要的部分；析构顺序与创建顺序相反）
class ScreenDcGuard {
public:
    ScreenDcGuard(int width, int height)
    {
        m_screen = ::CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
        if (m_screen == nullptr) {
            return;
        }
        m_memory = ::CreateCompatibleDC(m_screen);
        if (m_memory == nullptr) {
            return;
        }
        m_bitmap = ::CreateCompatibleBitmap(m_screen, width, height);
        if (m_bitmap == nullptr) {
            return;
        }
        m_oldBitmap = ::SelectObject(m_memory, m_bitmap);
    }

    ~ScreenDcGuard()
    {
        if (m_memory != nullptr && m_oldBitmap != nullptr && m_oldBitmap != HGDI_ERROR) {
            ::SelectObject(m_memory, m_oldBitmap);
        }
        if (m_bitmap != nullptr) {
            ::DeleteObject(m_bitmap);
        }
        if (m_memory != nullptr) {
            ::DeleteDC(m_memory);
        }
        if (m_screen != nullptr) {
            ::DeleteDC(m_screen);
        }
    }

    ScreenDcGuard(const ScreenDcGuard &) = delete;
    ScreenDcGuard &operator=(const ScreenDcGuard &) = delete;

    bool ready() const { return m_screen != nullptr && m_memory != nullptr && m_bitmap != nullptr; }
    HDC memory() const { return m_memory; }
    HBITMAP bitmap() const { return m_bitmap; }

    // 从屏幕 (x, y) 起拷贝 width × height 到内存 DC。
    // CAPTUREBLT：把分层窗口（半透明覆盖层 / 桌宠立绘）一并合成进来，与用户所见一致。
    bool blit(int x, int y, int width, int height)
    {
        if (!ready()) {
            return false;
        }
        return ::BitBlt(m_memory, 0, 0, width, height, m_screen, x, y, SRCCOPY | CAPTUREBLT) != FALSE;
    }

private:
    HDC m_screen = nullptr;
    HDC m_memory = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
};

int clampInt(int value, int low, int high)
{
    if (high < low) {
        return low;
    }
    return std::max(low, std::min(high, value));
}

} // namespace

ScreenPatch captureScreenPatch(int physicalX, int physicalY, int width, int height)
{
    ScreenPatch patch;
    if (width <= 0 || height <= 0) {
        patch.error = "抓取尺寸非法（必须为正）";
        return patch;
    }

    RECT desktop{};
    if (!virtualDesktopRect(&desktop)) {
        patch.error = "取不到虚拟桌面尺寸";
        return patch;
    }
    if (physicalX < desktop.left || physicalY < desktop.top
        || physicalX + width > desktop.right || physicalY + height > desktop.bottom) {
        patch.error = "抓取区域超出虚拟桌面";
        return patch;
    }

    ScreenDcGuard guard(width, height);
    if (!guard.ready()) {
        patch.error = "创建屏幕 DC / 位图失败";
        return patch;
    }
    if (!guard.blit(physicalX, physicalY, width, height)) {
        patch.error = "BitBlt 取屏失败";
        return patch;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // 负值 = 自上而下，避免逐行翻转
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> buffer(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                                      * 4u);
    const int rows = ::GetDIBits(guard.memory(), guard.bitmap(), 0, static_cast<UINT>(height),
                                 buffer.data(), &info, DIB_RGB_COLORS);
    if (rows == 0) {
        patch.error = "GetDIBits 读取像素失败";
        return patch;
    }

    // BGRA（GDI 顺序）→ RGBA；alpha 由 BitBlt 留下的是 0，统一补 255（屏幕像素不透明）
    for (std::size_t i = 0; i + 3 < buffer.size(); i += 4) {
        std::swap(buffer[i], buffer[i + 2]);
        buffer[i + 3] = 255;
    }

    patch.valid = true;
    patch.width = width;
    patch.height = height;
    patch.rgba = std::move(buffer);
    return patch;
}

ScreenPixel sampleScreenPixel(int physicalX, int physicalY)
{
    ScreenPixel pixel;
    const ScreenPatch patch = captureScreenPatch(physicalX, physicalY, 1, 1);
    if (!patch.valid || patch.rgba.size() < 4) {
        pixel.error = patch.error.empty() ? std::string("取色失败") : patch.error;
        return pixel;
    }
    pixel.valid = true;
    pixel.r = static_cast<int>(patch.rgba[0]);
    pixel.g = static_cast<int>(patch.rgba[1]);
    pixel.b = static_cast<int>(patch.rgba[2]);
    return pixel;
}

bool physicalCursorPos(int *x, int *y)
{
    POINT cursor{};
    if (::GetCursorPos(&cursor) == FALSE) {
        return false;
    }
    if (x != nullptr) {
        *x = static_cast<int>(cursor.x);
    }
    if (y != nullptr) {
        *y = static_cast<int>(cursor.y);
    }
    return true;
}

bool rectContainsPoint(int rectX, int rectY, int rectWidth, int rectHeight, int x, int y)
{
    return x >= rectX && x < rectX + rectWidth && y >= rectY && y < rectY + rectHeight;
}

bool virtualDesktopBounds(int *x, int *y, int *width, int *height)
{
    RECT desktop{};
    if (!virtualDesktopRect(&desktop)) {
        return false;
    }
    if (x != nullptr) {
        *x = static_cast<int>(desktop.left);
    }
    if (y != nullptr) {
        *y = static_cast<int>(desktop.top);
    }
    if (width != nullptr) {
        *width = static_cast<int>(desktop.right - desktop.left);
    }
    if (height != nullptr) {
        *height = static_cast<int>(desktop.bottom - desktop.top);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 系统放大镜
// ---------------------------------------------------------------------------
MagnifierWindow::MagnifierWindow() = default;

MagnifierWindow::~MagnifierWindow()
{
    destroy();
}

bool MagnifierWindow::available()
{
    // 探测一次并缓存（MagInitialize 会创建内部资源；探测后立刻配对释放）
    static const bool ok = [] {
        if (::MagInitialize() == FALSE) {
            return false;
        }
        ::MagUninitialize();
        return true;
    }();
    return ok;
}

bool MagnifierWindow::create(int sizePx, float factor, std::string *error)
{
    if (m_created) {
        return true;
    }
    if (!ensureHostClassRegistered()) {
        if (error != nullptr) {
            *error = "放大镜宿主窗口类注册失败";
        }
        return false;
    }
    if (::MagInitialize() == FALSE) {
        if (error != nullptr) {
            *error = "Magnification API 不可用（MagInitialize 失败）";
        }
        return false;
    }

    m_sizePx = clampInt(sizePx, kMagnifierMinSize, kMagnifierMaxSize);
    m_factor = std::max(kMagnifierMinFactor, std::min(kMagnifierMaxFactor, factor));

    HWND host = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  kMagnifierHostClass, L"WhalePet 取色放大镜", WS_POPUP, 0, 0,
                                  m_sizePx, m_sizePx, nullptr, nullptr, moduleInstance(), nullptr);
    if (host == nullptr) {
        ::MagUninitialize();
        if (error != nullptr) {
            *error = "放大镜宿主窗口创建失败";
        }
        return false;
    }

    HWND child = ::CreateWindowExW(0, WC_MAGNIFIER, L"", WS_CHILD | WS_VISIBLE, 0, 0, m_sizePx,
                                   m_sizePx, host, nullptr, moduleInstance(), nullptr);
    if (child == nullptr) {
        ::DestroyWindow(host);
        ::MagUninitialize();
        if (error != nullptr) {
            *error = "WC_MAGNIFIER 子窗口创建失败";
        }
        return false;
    }

    // 把自身从放大内容里排除（否则会出现「镜中镜」）；并显示系统光标（与系统放大镜一致）
    HWND exclude = host;
    ::MagSetWindowFilterList(child, MW_FILTERMODE_EXCLUDE, 1, &exclude);
    ::MagShowSystemCursor(TRUE);

    m_host = host;
    m_child = child;
    m_created = true;

    ::ShowWindow(host, SW_SHOWNOACTIVATE);
    followCursor();
    return true;
}

void MagnifierWindow::destroy()
{
    if (!m_created) {
        return;
    }
    m_created = false;
    if (m_child != nullptr) {
        ::DestroyWindow(static_cast<HWND>(m_child));
        m_child = nullptr;
    }
    if (m_host != nullptr) {
        ::DestroyWindow(static_cast<HWND>(m_host));
        m_host = nullptr;
    }
    ::MagUninitialize(); // 与 create() 的 MagInitialize 成对
}

void MagnifierWindow::followCursor()
{
    if (!m_created) {
        return;
    }
    int cursorX = 0;
    int cursorY = 0;
    if (!physicalCursorPos(&cursorX, &cursorY)) {
        return;
    }
    if (cursorX == m_lastX && cursorY == m_lastY) {
        return; // 光标没动：不重摆、不重设源（参考项目同样只在源变化时调用 MagSetWindowSource）
    }
    m_lastX = cursorX;
    m_lastY = cursorY;

    RECT desktop{};
    if (!virtualDesktopRect(&desktop)) {
        return;
    }

    const int size = m_sizePx;
    // 默认放在光标右下；右侧 / 下侧放不下就翻到另一侧（与系统放大镜的靠边翻转一致）
    int x = cursorX + kMagnifierCursorGap;
    int y = cursorY + kMagnifierCursorGap;
    if (x + size > desktop.right) {
        x = cursorX - kMagnifierCursorGap - size;
    }
    if (y + size > desktop.bottom) {
        y = cursorY - kMagnifierCursorGap - size;
    }
    x = clampInt(x, desktop.left, desktop.right - size);
    y = clampInt(y, desktop.top, desktop.bottom - size);

    // 兜底：任何情况下都不能覆盖光标点（否则 1×1 BitBlt 会采到放大镜自身）
    if (rectContainsPoint(x, y, size, size, cursorX, cursorY)) {
        x = clampInt(cursorX + kMagnifierCursorGap, desktop.left, desktop.right - size);
        y = clampInt(cursorY - kMagnifierCursorGap - size, desktop.top, desktop.bottom - size);
        if (rectContainsPoint(x, y, size, size, cursorX, cursorY)) {
            return; // 屏幕实在太小：本轮不摆位（宁可暂时不跟随，也不污染取色）
        }
    }

    ::SetWindowPos(static_cast<HWND>(m_host), HWND_TOPMOST, x, y, size, size,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // 源矩形 = 窗口尺寸 / 倍率，以光标为中心，并夹取在虚拟桌面内（越界时 MagSetWindowSource 直接失败）
    const int sourceSize = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) / m_factor)));
    int sourceX = cursorX - sourceSize / 2;
    int sourceY = cursorY - sourceSize / 2;
    sourceX = clampInt(sourceX, desktop.left, std::max(desktop.left, desktop.right - sourceSize));
    sourceY = clampInt(sourceY, desktop.top, std::max(desktop.top, desktop.bottom - sourceSize));

    RECT source{};
    source.left = sourceX;
    source.top = sourceY;
    source.right = sourceX + sourceSize;
    source.bottom = sourceY + sourceSize;
    if (::MagSetWindowSource(static_cast<HWND>(m_child), source) != FALSE) {
        ::InvalidateRect(static_cast<HWND>(m_child), nullptr, TRUE);
    }
}

bool MagnifierWindow::windowRect(int *x, int *y, int *width, int *height) const
{
    if (!m_created || m_host == nullptr) {
        return false;
    }
    RECT rect{};
    if (::GetWindowRect(static_cast<HWND>(m_host), &rect) == FALSE) {
        return false;
    }
    if (x != nullptr) {
        *x = static_cast<int>(rect.left);
    }
    if (y != nullptr) {
        *y = static_cast<int>(rect.top);
    }
    if (width != nullptr) {
        *width = static_cast<int>(rect.right - rect.left);
    }
    if (height != nullptr) {
        *height = static_cast<int>(rect.bottom - rect.top);
    }
    return true;
}

} // namespace whalepet::platform
