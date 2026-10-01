#pragma once

// P6 设置面板（docs/SETTINGS.md）：
//   陪伴表现 / 日常·成就·日记（复用 ContentPanel）/ 小游戏（预留）/ 数据与重置。
//
// 边界：
//   - 本类负责「控件 ↔ settings 表」的读写与**即时落库**；
//   - 「应用」（改立绘尺寸 / 气泡 / 深夜静默 / 桌宠显隐等）交给 PetWindow（emit settingsChanged）；
//   - 重置类动作涉及 GrowthService 与窗口位置，同样以信号请求 PetWindow 执行。
// 外观仅依赖全局样式表（resources/qt-ui/default.qss），不自行设计样式。

#include "model/SettingsData.h"

#include <QDialog>

class QCheckBox;
class QHideEvent;
class QShowEvent;
class QSpinBox;

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

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(model::Database *db, viewmodel::AchievementService *achievement,
                   viewmodel::QuestService *quest, viewmodel::SigninService *signin,
                   QWidget *parent = nullptr);

    // 从库刷新控件（打开前调用；不触发落库与 settingsChanged）
    void reload();

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

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *buildAppearanceTab();
    QWidget *buildMiniGameTab();
    QWidget *buildDataTab();

    // 从控件读回并落库；m_loading 期间为空操作
    void persist();

    model::Database *m_db = nullptr;
    ContentPanel *m_content = nullptr;

    // 陪伴表现
    QCheckBox *m_petEnabled = nullptr;
    QCheckBox *m_bubbleEnabled = nullptr;
    QCheckBox *m_particlesEnabled = nullptr;
    QCheckBox *m_keywordAware = nullptr;
    QCheckBox *m_nightQuiet = nullptr;
    QCheckBox *m_dragInertia = nullptr;
    QSpinBox *m_poseSize = nullptr;

    // 小游戏（预留）
    QCheckBox *m_minigameEnabled = nullptr;

    bool m_loading = false; // 刷新控件期间抑制 persist / settingsChanged
};

} // namespace whalepet
