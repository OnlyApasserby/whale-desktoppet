#pragma once

// P4 内容面板：日常（每日任务 + 周签到）/ 成就墙 / 成长日记（标签页）。
// 见 docs/ROADMAP-P4.md 任务 6、docs/GAMEPLAY.md §3-6、docs/SETTINGS.md §2。
//
// 边界：本类**只渲染 + 发出请求**，不做任何数值判定，也不直接改库；
// 由 PetWindow 把三个 Service 的当前状态喂进来，并处理来自本类的请求。
// 外观仅依赖 resources/qt-ui/default.qss 的全局样式（黑底白字），
// 「未解锁 / 未达成」用控件的 disabled 状态表达灰显，不自行设计样式。
//
// 待 P6 实现独立 SettingsDialog 时，本面板的三个标签页可直接内嵌其中。

#include "core/SigninRules.h"

#include <QDialog>
#include <QString>

#include <cstdint>

class QGroupBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace whalepet {

namespace model {
class Database;
} // namespace model

namespace viewmodel {
class AchievementService;
class QuestService;
class SigninService;
} // namespace viewmodel

class ContentPanel : public QDialog {
    Q_OBJECT
public:
    ContentPanel(viewmodel::AchievementService *achievement, viewmodel::QuestService *quest,
                 viewmodel::SigninService *signin, model::Database *db, QWidget *parent = nullptr);

    // 从三个 Service 的当前状态重建三个标签页
    void refreshAll();

    // 相对时间文案（日记用）：刚刚 / N 分钟前 / N 小时前 / N 天前 / 日期
    static QString formatRelative(qint64 tsMs, qint64 nowMs);

signals:
    void signInRequested();
    void questClaimRequested(int slotIndex);

private:
    QWidget *buildDailyTab();
    QWidget *buildAchievementTab();
    QWidget *buildDiaryTab();

    void refreshDaily();
    void refreshAchievements();
    void refreshDiary();

    viewmodel::AchievementService *m_achievement = nullptr;
    viewmodel::QuestService *m_quest = nullptr;
    viewmodel::SigninService *m_signin = nullptr;
    model::Database *m_db = nullptr;

    // 日常
    QLabel *m_signinSummary = nullptr;
    QLabel *m_signinCells[core::kSigninWeekDays] = {};
    QPushButton *m_signInButton = nullptr;
    QVBoxLayout *m_questLayout = nullptr;

    // 成就墙 / 日记（内容每次刷新时重建）
    QLabel *m_achievementSummary = nullptr;
    QVBoxLayout *m_achievementLayout = nullptr;
    QVBoxLayout *m_diaryLayout = nullptr;
};

} // namespace whalepet
