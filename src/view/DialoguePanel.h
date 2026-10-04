#pragma once

// DialoguePanel（P8 问答系统）：「主人的问题」面板 —— docs/DIALOGUE.md §3。
//
// 形态：**无边框工具窗口**（与 SpeechBubble 同一套定位/置顶策略），紧贴桌宠显示；
// 窗口标题固定为 **「主人的问题」**（`kPanelTitle`，同时作为面板内可见的标题行）。
//
// 内容：**五选一** —— 固定 1 个天气问题（未配置天气 API 时禁用）、
// 固定 1 个敏感 / 私密问题（好感度未达 5000 或当日次数用尽时禁用）、
// 其余 3 个问题每次打开面板时随机刷新。禁用项给出原因（tooltip）。
//
// 职责边界：本类**只渲染 + 发出选择请求**，不持有选项池、不判配额、不做天气判定
// （由 viewmodel::DialogueService 负责，与 HotwordDialog / ContentPanel 同一套路）。
// 外观仅依赖 resources/qt-ui 的全局样式表（黑底白字），本类不写任何颜色字面量。

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace whalepet {

class DialoguePanel : public QDialog {
    Q_OBJECT
public:
    // 面板标题（需求：提问界面的窗口标题固定为「主人的问题」）
    static constexpr const char *kPanelTitle = "主人的问题";
    // 无人应答时的自动关闭时限（超时视为「这次不问」，不消耗任何配额）
    static constexpr int kAnswerTimeoutMs = 60'000;

    explicit DialoguePanel(QWidget *parent = nullptr);

    // 跟随目标（桌宠窗口）；面板显示在其上方（空间不足时改挂下方）
    void attachTo(QWidget *anchor);

    // 显示一批可选问题（五选一）。enabled 与 hints 与 questions 等长；
    // 不可用项按钮禁用，并以 hints 作为提示（tooltip）。
    void showOptions(const QStringList &questions, const QList<bool> &enabled,
                     const QStringList &hints);
    void closePanel();

    bool optionsVisible() const { return isVisible(); }
    int optionCount() const;
    QString optionText(int index) const;

signals:
    // 用户选择第 index 个问题（该问题必然可用：禁用按钮不发信号）
    void chosen(int index);
    // 用户关闭面板 / 超时（调用方应 cancel 本次选项，不消耗配额）
    void dismissed();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void reposition();
    void rebuildOptions(const QStringList &questions, const QList<bool> &enabled,
                        const QStringList &hints);
    void onButtonClicked(int index);

    QWidget *m_anchor = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QVector<QPushButton *> m_buttons;
    QTimer *m_timeout = nullptr;
    bool m_dismissNotified = false;
};

} // namespace whalepet
