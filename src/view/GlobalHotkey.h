#pragma once

// 全局热键（P6）：系统级快捷键 → 唤起「热词录入」（docs/CHAT.md §4、docs/SETTINGS.md §2）。
//
// 平台策略（docs/BUILD.md：目标平台**仅 Windows**）：
//   - Windows：`RegisterHotKey(nullptr, ...)` 注册到**当前线程消息队列**（不需要 HWND），
//     再由 QAbstractNativeEventFilter 截获 `WM_HOTKEY` → `emit activated()`。
//     线程消息同样会经过原生事件过滤器（eventType == "windows_generic_MSG"）。
//   - 其它平台 / 注册失败（键位被别的程序占用）：`registerShortcut()` 返回 false
//     并写明原因，调用方降级为「只保留菜单入口」——不崩溃、不静默、不影响主流程。
//
// 本头文件**刻意不包含 windows.h**（避免宏污染），Win32 细节全部关在 .cpp 内。

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <QString>

namespace whalepet {

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    // 注册系统级热键；成功返回 true。
    // 失败返回 false，并在 errorOut（若非空）写入可直接展示给用户的原因。
    bool registerShortcut(const QKeySequence &sequence, QString *errorOut = nullptr);
    void unregisterShortcut();
    bool isRegistered() const { return m_registered; }

    // 「Ctrl+Alt+K」形式的本地化文案，用于菜单显示
    QString shortcutText() const { return m_sequence.toString(QKeySequence::NativeText); }

signals:
    void activated();

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QKeySequence m_sequence;
    bool m_registered = false;
};

} // namespace whalepet
