#pragma once

// hotwords 表 CRUD —— docs/DATA-MODEL.md §3.9、docs/CHAT.md §4。
//
// 只负责**存取**「用户录入的热词 → 关键词 id」；匹配优先级与命中规则在
// whalepet::core::ChatRules.h 的 matchKeyword(text, custom)（纯逻辑、可单测），
// 本类不做任何匹配判定。
//
// 归一化（normalizeWord）**在写入侧完成**：trim + toLower。
// 于是 UNIQUE(word) 能同时拦住 "OMG" / "omg" 这类大小写重复，
// 读取侧无需再做归一化（display 即存储值）。

#include <QString>
#include <QVector>

namespace whalepet::model {

class Database;

// 一条热词（word 已归一化）
struct Hotword
{
    QString word;
    QString keywordId;
};

class HotwordRepo {
public:
    explicit HotwordRepo(Database *db) : m_db(db) {}

    // 按 id 升序返回（= 录入顺序 = 匹配优先级顺序）；表空或库不可用返回空表
    QVector<Hotword> loadAll() const;

    // 写入（同 word 已存在则覆盖其 keywordId，保留原 id 顺序）。
    // word 会先归一化；归一化后为空、或 keywordId 非 kKeywordRules 合法 id → 返回 false。
    bool upsert(const QString &word, const QString &keywordId, qint64 nowMs);

    bool remove(const QString &word);

    bool clear();

    // trim + toLower；录入与删除共用同一口径，避免「存进去删不掉」
    static QString normalizeWord(const QString &raw);

private:
    Database *m_db = nullptr;
};

} // namespace whalepet::model
