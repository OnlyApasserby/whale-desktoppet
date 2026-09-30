#pragma once

// 建表与 schema_version 迁移 —— 与 docs/DATA-MODEL.md §2/§3 严格对应。
//
// 约定：所有 DDL 都写 IF NOT EXISTS，迁移只做「版本号推进 + 缺表补齐」，
// 不做无谓的表重建（P3 尚无历史库需要升级，v1 即初始版本）。

#include <QString>
#include <QStringList>

class QSqlDatabase;

namespace whalepet::model {

class Schema {
public:
    static constexpr int kVersion = 1;

    static QString versionKey() { return QStringLiteral("schema_version"); }

    // 迁移前置：meta 表必须最先存在
    static bool ensureMeta(QSqlDatabase &db);

    // v1 全部表的 DDL（meta 之后的表）
    static QStringList statementsV1();

    // 建表（幂等）+ 版本号推进；返回 false 表示脚本执行失败
    static bool migrate(QSqlDatabase &db);

    // 读取当前库内版本号；无记录返回 0
    static int readVersion(QSqlDatabase &db);

    static bool writeVersion(QSqlDatabase &db, int version);
};

} // namespace whalepet::model
