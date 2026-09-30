#pragma once

// 状态面板：只读展示养成数值（docs/ROADMAP-P3 任务 8、docs/GAMEPLAY.md §1）。
//
// 边界：本类**不做任何数值计算**，也不直接访问数据库——
// 由 PetWindow 把 GrowthService 的当前状态喂进 updateFrom()。
// 外观仅依赖 resources/qt-ui/default.qss 的全局样式（黑底白字），不自行设计样式。

#include "core/GrowthRules.h"
#include "model/PetStateData.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace whalepet {

class StatusPanel : public QDialog {
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);

    // state：当前养成状态；storageInfo：数据库位置/模式（诊断用，可为空）
    void updateFrom(const model::PetStateData &state, const core::BondUnlocks &unlocks,
                    const QString &storageInfo);

    static QString formatDuration(qint64 ms);

signals:
    void signInRequested();

private:
    QLabel *m_level = nullptr;
    QLabel *m_bond = nullptr;
    QLabel *m_mood = nullptr;
    QLabel *m_affinity = nullptr;
    QLabel *m_satiety = nullptr;
    QLabel *m_companion = nullptr;
    QLabel *m_streak = nullptr;
    QLabel *m_storage = nullptr;
    QProgressBar *m_expBar = nullptr;
    QPushButton *m_signInButton = nullptr;
};

} // namespace whalepet
