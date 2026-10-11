#pragma once

// P10 工作台面板（工作能力插件的视图）——docs/ARCHITECTURE.md 附录 D.4。
//
// P11（docs/DRAWING.md §2）改造：面板由**单页哑面板**升级为**页签容器**。
//   * 第 1 页仍是 P10 的「干活」页（勾选功能块 / 时长 / 产出目录 / 进度）；
//   * 其余页由插件经 `plugin::ContributionKind::WorkbenchPage` 贡献点声明，
//     宿主在创建面板时按 order 追加（`FillPagesFn` 由宿主 / 插件注入）。
//   * 本类**不认识任何具体页面**（不放绘画 / 状态等业务分支），保持「哑面板」定位。
//
// 窗口仍为**顶层工具窗**（无 QObject 父），所有权归插件
// （与 P9-C 的 StatusPanelUiPlugin 同范式，见 src/view/ui/StatusPanelUiPlugin.h）。

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTabWidget;

namespace whalepet::viewmodel {
class WorkService;
}

namespace whalepet::ui {

class WorkBenchPanel : public QWidget {
    Q_OBJECT
public:
    // 追加工作台页的回调（宿主注入）：参数即页签容器，实现方按贡献点 addTab。
    using FillPagesFn = std::function<void(QTabWidget *)>;

    explicit WorkBenchPanel(viewmodel::WorkService *service, FillPagesFn fillPages = {},
                            QWidget *parent = nullptr);

    // 面板改选产出目录时的回调（插件转交宿主持久化到 work_output_dir）；空 = 不持久化
    std::function<void(const QString &)> outputDirChosen;

    // 页签容器（供测试核对页数与页标题；P11）
    QTabWidget *tabs() const { return m_tabs; }

private:
    void reloadBlocks();
    void refreshState();
    void applyOutputDir(const QString &dir);
    QStringList checkedBlockIds() const;

    void onStart();
    void onProduceNow();
    void onCancel();
    void onBrowseDir();

    viewmodel::WorkService *m_service = nullptr; // 非拥有（插件 / 宿主持有）
    QTabWidget *m_tabs = nullptr;                // P11：页签容器（第 1 页 = 干活页）
    QWidget *m_workPage = nullptr;               // P11：P10 原有界面的承载页
    QListWidget *m_blocks = nullptr;
    QComboBox *m_duration = nullptr;
    QLineEdit *m_outputDir = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_produceNow = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_openDir = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace whalepet::ui
