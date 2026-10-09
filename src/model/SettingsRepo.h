#pragma once

// settings（单例）CRUD —— docs/DATA-MODEL.md §3.3、docs/SETTINGS.md §2。
//
// 本类同时承接 P1 遗留的窗口位置持久化（原 QSettings），见 ROADMAP-P3 任务 3。

#include "model/SettingsData.h"

namespace whalepet::model {

class Database;

class SettingsRepo {
public:
    explicit SettingsRepo(Database *db) : m_db(db) {}

    // 返回 false 表示尚无记录（out 保持默认值不变）
    bool load(SettingsData &out) const;

    bool save(const SettingsData &in);

    // 位置置空（恢复默认位置的语义）
    bool clearPosition();

    // EX3：一次性清理已移除的「外部游戏陪玩」旧设置键
    //   （game_companion_enabled / game_profile_path）；返回是否真的发生了清理。
    bool purgeLegacyGameCompanionKeys();

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
