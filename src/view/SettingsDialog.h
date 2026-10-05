#pragma once

// P6 设置面板（docs/SETTINGS.md）：
//   陪伴表现 / 日常·成就·日记（复用 ContentPanel）/ 小游戏（插件化）/ 数据与重置。
//
// 边界：
//   - 本类负责「控件 ↔ settings 表」的读写与**即时落库**；
//   - 「应用」（改立绘尺寸 / 气泡 / 深夜静默 / 桌宠显隐等）交给 PetWindow（emit settingsChanged）；
//   - 重置类动作涉及 GrowthService 与窗口位置，同样以信号请求 PetWindow 执行；
//   - 小游戏页**不硬编码任何游戏**：按 MiniGameRegistry 的已注册插件动态生成列表，
//     新增插件无需改动本类。
// 外观仅依赖全局样式表（resources/qt-ui/default.qss），不自行设计样式。

#include "model/SettingsData.h"
#include "plugin/process/ProcessServerSpec.h" // P9-B：外部插件状态（只读展示）

#include <QDialog>
#include <QHash>
#include <QList>

class QCheckBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QShowEvent;
class QSpinBox;
class QTableWidget;

namespace whalepet {

namespace model {
class Database;
} // namespace model

namespace viewmodel {
class AchievementService;
class QuestService;
class SigninService;
} // namespace viewmodel

class ContentPanel;
class MiniGameRegistry;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(model::Database *db, viewmodel::AchievementService *achievement,
                   viewmodel::QuestService *quest, viewmodel::SigninService *signin,
                   MiniGameRegistry *miniGames, QWidget *parent = nullptr);

    // 从库刷新控件（打开前调用；不触发落库与 settingsChanged）
    void reload();

    // 刷新内嵌的「日常 / 成就墙 / 成长日记」三页。
    // 内嵌页与独立 ContentPanel 是**两个实例**，签到等状态变化时需一并刷新才能保持同步。
    void refreshContent();

    // P9-B：「外部插件」页的只读状态快照（由 PetWindow 在打开面板前注入；不触发落库）。
    void setProcessPluginStatuses(const QList<plugin::ProcessPluginStatus> &statuses);

signals:
    // 任一设置变化（已落库）→ PetWindow 应用到界面
    void settingsChanged(const model::SettingsData &data);
    // 面板显隐 → PetWindow 据此抑制/恢复主动说话
    void visibleChanged(bool visible);

    void signInRequested();
    void questClaimRequested(int slotIndex);
    void resetPositionRequested();
    void resetGrowthRequested();
    void openDataDirRequested();
    void openMiniGameRequested(const QString &pluginId); // 「开始××」→ PetWindow 打开对应插件窗口

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *buildAppearanceTab();
    QWidget *buildMiniGameTab();
    QWidget *buildProcessPluginTab(); // P9-B：外部插件（只读状态展示）
    QWidget *buildDataTab();

    // 从控件读回并落库；m_loading 期间为空操作
    void persist();

    model::Database *m_db = nullptr;
    ContentPanel *m_content = nullptr;
    MiniGameRegistry *m_miniGames = nullptr; // 已注册插件（只读；不接管所有权）

    // 陪伴表现
    QCheckBox *m_petEnabled = nullptr;
    QCheckBox *m_bubbleEnabled = nullptr;
    QCheckBox *m_particlesEnabled = nullptr;
    QCheckBox *m_keywordAware = nullptr;
    QCheckBox *m_nightQuiet = nullptr;
    QCheckBox *m_dragInertia = nullptr;
    QCheckBox *m_recycleBinReminder = nullptr; // 立绘激活 18：回收站清理提醒
    QSpinBox *m_poseSize = nullptr;

    // P8：预设对话 + 彩云天气（docs/DIALOGUE.md §4）
    QCheckBox *m_dialogueEnabled = nullptr;
    QLineEdit *m_weatherKey = nullptr;
    QLineEdit *m_weatherLocation = nullptr;

    // EX 彩蛋（experiment/easter-egg1）：代码彩蛋（开关 + 目标工作区目录）
    QCheckBox *m_codeEggEnabled = nullptr;
    QLineEdit *m_codeEggWorkspace = nullptr;

    // 小游戏（插件化）
    QCheckBox *m_minigameEnabled = nullptr;              // 全局开关（门控所有插件入口）
    QHash<QString, QLabel *> m_miniGameConfigLabels;     // 按插件 id 显示上次配置摘要

    // P9-B：外部进程插件（只读状态快照 + 表格）
    QTableWidget *m_processTable = nullptr;
    QList<plugin::ProcessPluginStatus> m_processStatuses;

    bool m_loading = false; // 刷新控件期间抑制 persist / settingsChanged
};

} // namespace whalepet
