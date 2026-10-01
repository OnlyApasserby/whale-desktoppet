#include "view/GlobalHotkey.h"

#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#endif

namespace whalepet {

namespace {

// 本程序只注册一个热键，固定 id（'WH' = 0x5748），避免与其它注册项串号
constexpr int kHotkeyId = 0x5748;

#ifdef Q_OS_WIN
// Qt::Key → Win32 虚拟键码：只支持常见键位，其余返回 0（调用方报「不支持的按键」）
UINT toVirtualKey(Qt::Key key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>('A' + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>('0' + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    }
    switch (key) {
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Home:
        return VK_HOME;
    case Qt::Key_End:
        return VK_END;
    default:
        return 0;
    }
}
#endif // Q_OS_WIN

} // namespace

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->installNativeEventFilter(this);
    }
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterShortcut();
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->removeNativeEventFilter(this);
    }
}

bool GlobalHotkey::registerShortcut(const QKeySequence &sequence, QString *errorOut)
{
    unregisterShortcut();
    m_sequence = sequence;

    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        qWarning() << "[GlobalHotkey] 全局热键不可用:" << reason;
        return false;
    };

#ifndef Q_OS_WIN
    Q_UNUSED(sequence)
    return fail(QStringLiteral("全局热键仅支持 Windows，已降级为仅菜单入口"));
#else
    if (sequence.isEmpty()) {
        return fail(QStringLiteral("热键为空"));
    }

    const QKeyCombination combo = sequence[0];
    const Qt::KeyboardModifiers qmods = combo.keyboardModifiers();
    UINT mods = 0;
    if (qmods.testFlag(Qt::ControlModifier)) {
        mods |= MOD_CONTROL;
    }
    if (qmods.testFlag(Qt::AltModifier)) {
        mods |= MOD_ALT;
    }
    if (qmods.testFlag(Qt::ShiftModifier)) {
        mods |= MOD_SHIFT;
    }
    if (qmods.testFlag(Qt::MetaModifier)) {
        mods |= MOD_WIN;
    }
    if (mods == 0) {
        return fail(QStringLiteral("需要至少一个修饰键（Ctrl / Alt / Shift / Win）"));
    }

    const UINT vk = toVirtualKey(combo.key());
    if (vk == 0) {
        return fail(QStringLiteral("不支持的按键（仅支持字母 / 数字 / F1–F24 / 空格等）"));
    }

    if (::RegisterHotKey(nullptr, kHotkeyId, mods | MOD_NOREPEAT, vk)) {
        m_registered = true;
        return true;
    }

    const DWORD err = ::GetLastError();
    return fail(err == ERROR_HOTKEY_ALREADY_REGISTERED
                    ? QStringLiteral("热键已被其它程序占用")
                    : QStringLiteral("RegisterHotKey 失败（错误码 %1）").arg(err));
#endif // Q_OS_WIN
}

void GlobalHotkey::unregisterShortcut()
{
    if (!m_registered) {
        return;
    }
#ifdef Q_OS_WIN
    ::UnregisterHotKey(nullptr, kHotkeyId);
#endif
    m_registered = false;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    // 线程消息（RegisterHotKey(nullptr, ...) 的 WM_HOTKEY 走这里，无 hwnd）
    if (eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }
    MSG *msg = static_cast<MSG *>(message);
    if (msg != nullptr && msg->message == WM_HOTKEY
        && static_cast<int>(msg->wParam) == kHotkeyId) {
        emit activated();
        return true;
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

} // namespace whalepet
