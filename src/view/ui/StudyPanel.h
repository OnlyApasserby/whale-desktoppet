#pragma once

// EX8 学习页（工作台「学习」栏）的视图 —— docs/STUDY.md §2。
//
// 只读呈现 `viewmodel::StudyService` 的清单与代码：
//   * 左：可导入的「干活」条目（内容层唯一索引）→「导入选中」/「导入全部」；
//   * 右：已导入的学习条目 →「移出选中」/「清空」/「导入外部代码…」；
//   * 下：只读代码视图 —— 选中条目显示「条目信息 + 代码块正文（原样）」；
//     「拼接整脚本」按导入顺序拼接功能块（import 归位到文件头并去重）。
//
// 外观：**不自行设计样式** —— 全部沿用全局样式表 resources/qt-ui/default.qss
// （无任何局部 setStyleSheet / 颜色字面量，与 P11 绘画页同口径）。

#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace whalepet::viewmodel {
class StudyService;
}

namespace whalepet::ui {

class StudyPanel : public QWidget {
    Q_OBJECT
public:
    explicit StudyPanel(viewmodel::StudyService *service, QWidget *parent = nullptr);

    // 测试 / 诊断用访问器（objectName 同步设置，见 .cpp）
    QListWidget *availableList() const { return m_available; }
    QListWidget *importedList() const { return m_imported; }
    QPlainTextEdit *codeView() const { return m_code; }
    QLabel *messageLabel() const { return m_message; }

    // 重新填充两个列表并保持「候选区选中项」与「代码视图所显示的条目」（不改变清单）
    void refresh();

private:
    void reloadAvailable();
    void reloadImported();
    void showItemCode(const QString &itemId);
    void onImportSelected();
    void onImportAll();
    void onRemoveSelected();
    void onClearAll();
    void onImportExternal();
    void onPreview();

    viewmodel::StudyService *m_service = nullptr; // 非拥有（插件持有）
    QString m_shownId;                            // 代码视图当前展示的条目 id
    QListWidget *m_available = nullptr;
    QListWidget *m_imported = nullptr;
    QPushButton *m_importSelected = nullptr;
    QPushButton *m_importAll = nullptr;
    QPushButton *m_removeSelected = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_importExternal = nullptr;
    QPushButton *m_preview = nullptr;
    QPlainTextEdit *m_code = nullptr;
    QLabel *m_message = nullptr;
};

} // namespace whalepet::ui
