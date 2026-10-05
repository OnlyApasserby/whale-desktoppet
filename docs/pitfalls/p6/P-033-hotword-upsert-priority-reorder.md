# `upsert` 先删后插，重绑关键词会重排热词优先级

> **原编号**：`TRAP-P6-002`　**阶段**：P6　**来源**：原按阶段聚合的 `traps-P6.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：逻辑 / 持久化 ｜ **影响**：用户修改某条热词绑定的关键词后，该热词的**优先级位置**
被无声挪到列表末尾，与「按录入顺序（`id` 升序）决定优先级」的产品语义不符；表现为「改完关键词后
这条热词不生效了」（被后面的热词抢先匹配）。

### 现象

`test_hotword` 用例 `repoKeepsInsertOrderAndRemoves()` 断言失败（Debug）：

```
FAIL!  : TestHotword::repoKeepsInsertOrderAndRemoves() Compared values are not the same
   Actual   (items[0].word)        : "\u4E59"      ← 乙
   Expected (QStringLiteral("甲")): "\u7532"       ← 甲
F:\develop\desktoppet\tests\test_hotword.cpp(197) : failure location
Totals: 11 passed, 1 failed
```

用例：依次录入 甲 / 乙 / 丙，再把「甲」重绑为 `sike`（第 194 行），
期望 `loadAll()[0]` 仍是「甲」（覆盖不改变位置），实际变成「乙」。

### 根因

原实现为规避 `ON CONFLICT` 兼容问题，采用**先删后插**：

```cpp
DELETE FROM hotwords WHERE word = :w;   // 删掉旧「甲」（id=1）
INSERT INTO hotwords(...) VALUES(...);  // 重新插入「甲」→ AUTOINCREMENT 分到新 id=4
```

`hotwords.id` 是 `INTEGER PRIMARY KEY AUTOINCREMENT`，删除后再插入必然拿到**更大的新 id**；
而 `loadAll()` 以 `ORDER BY id ASC` 表达优先级，于是「甲」被排到「乙」「丙」之后。
即：把「更新」实现成了「删除 + 追加」，主键身份没保住。

### 解决

`upsert` 改为**先 UPDATE、命中即返回**，未命中才 INSERT，从而保住原 `id`：

```cpp
UPDATE hotwords SET keyword_id = :k, created_ms = :ms WHERE word = :w;
if (upd.numRowsAffected() > 0) { return true; }   // 既有记录：id 不变
// 否则 INSERT ...
```

同样不使用 `UPSERT` / `ON CONFLICT`，对老 SQLite 驱动仍兼容；归一化与 `keywordIdValid`
校验逻辑不变。

### 影响与关联文档

- 关联：`docs/CHAT.md` §4（自定义热词，优先级=录入顺序）、`docs/DATA-MODEL.md` §3.9（`hotwords` 表）。
- 教训：凡用「自增主键顺序」承载业务次序（优先级 / 排序）的表，**更新**语义必须原地 `UPDATE`，
  不得用「删 + 插」代替，否则主键（= 次序）会被悄悄改写。

---
