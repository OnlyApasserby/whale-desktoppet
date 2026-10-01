#pragma once

// 热词录入面板（P6）—— docs/CHAT.md §4、docs/SETTINGS.md §2。
//
// 全局热键（默认 Ctrl+Alt+K）或右键/托盘菜单打开，用于：
//   1) **立即触发**：把输入文本按「自定义热词 → 内置触发词」匹配一次，命中即切表情 + 说梗台词；
//   2) **录入热词**：把「词 → 关键词 id」存进 hotwords 表，之后被动（剪贴板）与主动（录入）都优先用它；
//   3) **管理**：列出已录入热词并可删除。
//
// 边界：本类**只渲染 + 发出请求**，不碰数据库、不做匹配判定
// （与 ContentPanel 同一套路：由 PetWindow 喂数据、处理请求）。
// 外观仅依赖 resources/qt-ui 的全局样式表（黑底白字），不自行设计样式。

#include "model/HotwordRepo.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace whalepet {

class HotwordDialog : public QDialog {
    Q_OBJECT
public:
    explicit HotwordDialog(QWidget *parent = nullptr);

    // 关键词下拉的可选项（来自 core::kKeywordRules 的 id 列表）
    void setKeywordChoices(const QStringList &keywordIds);

    // 已录入热词列表（按录入顺序 = 匹配优先级）
    void setHotwords(const QVector<model::Hotword> &items);

    // 状态提示（成功/失败）；isError 只影响是否加「失败」前缀文案，不改样式
    void showHint(const QString &text, bool isError = false);

    QString inputText() const;

signals:
    void triggerRequested(const QString &text);                              // 立即触发
    void saveRequested(const QString &word, const QString &keywordId);        // 录入热词
    void removeRequested(const QString &word);                               // 删除热词

private:
    void onTrigger();
    void onSave();
    void onRemove();

    QLineEdit *m_input = nullptr;
    QComboBox *m_keywords = nullptr;
    QPushButton *m_trigger = nullptr;
    QPushButton *m_save = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_remove = nullptr;
    QLabel *m_hint = nullptr;
};

} // namespace whalepet
