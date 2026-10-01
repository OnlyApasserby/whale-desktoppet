#pragma once

// P4 内容层：周签到（7 格 + 1/3/7 里程碑）—— docs/ROADMAP-P4.md 任务 3、docs/GAMEPLAY.md §4。
//
// 与 P3 的「每日签到」的关系：
//   * 签到行为只有一个（菜单/状态面板上的「今日签到」），由组合根一次操作里同时驱动两件事：
//       1) GrowthService::signIn()      → 连续天数 streak_days（P3 已实现，跨天幂等）
//       2) SigninService::markToday()   → 点亮本周签到板的今天这一格、发里程碑奖励
//   * 因此不存在两个按钮、两套幂等键；任何一边认为「今天已签」都不会重复发奖。
//
// 跨周：weekKey（本周周一日期）变化时整表重置（保留 7 行，清空 signed 与奖励位图）。

#include "model/SigninRepo.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace whalepet::model {
class Database;
class DiaryRepo;
} // namespace whalepet::model

namespace whalepet::viewmodel {

class SigninService : public QObject {
    Q_OBJECT

public:
    explicit SigninService(model::Database *db, QObject *parent = nullptr);
    ~SigninService() override;

    // 载入签到板并对齐到本周（本周不一致才重置，不会清掉本周已有记录）
    bool load(qint64 nowMs = 0);

    // 运行中跨周检测（跨周一仍在使用时由交互上报触发）
    void syncWeek(qint64 nowMs = 0);

    QString weekKey() const { return m_weekKey; }
    const QList<model::SigninRow> &rows() const { return m_rows; }
    int todayIndex() const { return m_todayIndex; }
    int signedCount() const;
    bool isTodaySigned() const;

    // 点亮今天（应在 GrowthService::signIn 成功后调用）；今天已签返回 false
    bool markToday(qint64 nowMs = 0);

signals:
    void weekChanged();
    void boardChanged();
    void todaySigned(int dayIndex, int signedCount);
    void rewardGranted(int mood, int affinity, int milestoneDays);

private:
    void checkMilestones(qint64 nowMs);

    model::Database *m_db = nullptr;
    std::unique_ptr<model::SigninRepo> m_repo;
    std::unique_ptr<model::DiaryRepo> m_diary;

    QList<model::SigninRow> m_rows;
    QString m_weekKey;
    int m_todayIndex = 0;
};

} // namespace whalepet::viewmodel
