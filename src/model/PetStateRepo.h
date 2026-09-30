#pragma once

// pet_state（单例）CRUD —— docs/DATA-MODEL.md §3.2/§4。

#include "model/PetStateData.h"

namespace whalepet::model {

class Database;

class PetStateRepo {
public:
    explicit PetStateRepo(Database *db) : m_db(db) {}

    // 读取单例行（id = 1）；返回 false 表示尚无记录（out 保持默认值不变）
    bool load(PetStateData &out) const;

    // INSERT OR REPLACE 单例行（幂等）
    bool save(const PetStateData &in);

    // 删除单例行（测试/重置用）
    bool clear();

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
