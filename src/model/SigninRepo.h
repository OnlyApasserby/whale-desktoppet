#pragma once

// signin 表 CRUD —— docs/DATA-MODEL.md §3.6、docs/GAMEPLAY.md §4「周签到」。
//
// 表恒为 7 行（day_index 0~6 表示周一~周日）。跨周时**整表重置**（保留行、清空 signed 与
// reward_claimed 并换 week_key），不需要新增表或迁移。
//
// reward_claimed 是**位图**而不是布尔：0x1=集满 1 天、0x2=集满 3 天、0x4=集满 7 天的里程碑
// 奖励是否已发放（表结构在 v1 已定型，用位图即可表达 3 个里程碑，避免迁移）。

#include <QList>
#include <QString>

namespace whalepet::model {

class Database;

struct SigninRow {
    int dayIndex = 0;   // 0~6 = 周一~周日
    QString weekKey;    // 本周周一日期（Y-M-D）
    bool signedDay = false;
    int rewardMask = 0; // 位图见文件头注释
};

enum SigninRewardBit {
    kSigninReward1Day = 0x1,
    kSigninReward3Day = 0x2,
    kSigninReward7Day = 0x4,
};

class SigninRepo {
public:
    explicit SigninRepo(Database *db) : m_db(db) {}

    // 返回 7 行（缺失的行由调用方按默认值处理；首次使用前应先 resetWeek）
    QList<SigninRow> loadAll() const;

    // 整表重置为指定周（7 行、全部未签、无奖励）
    bool resetWeek(const QString &weekKey);

    bool saveRow(const SigninRow &row);

    bool markReward(int dayIndex, int rewardBit);

    bool clear();

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
