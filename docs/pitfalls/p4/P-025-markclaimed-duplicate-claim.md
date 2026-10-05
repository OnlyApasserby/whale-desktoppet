# `markClaimed` 只判 `done`，SQLite 重复领取仍返回成功

> **原编号**：`TRAP-P4-003`　**阶段**：P4　**来源**：原按阶段聚合的 `traps-P4.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：逻辑错误 / 幂等 ｜ **影响**：`test_content::questRepoConditionalUpdate` 失败；
若上层不加内存态兜底，**会重复发放任务奖励**

### 现象

```
FAIL!  : TestContent::questRepoConditionalUpdate() '!repo.markClaimed(QStringLiteral("pat-3"))' returned FALSE. ()
tests/test_content.cpp(441) : failure location
```

即第二次调用 `markClaimed()` 仍返回 `true`。

### 根因

原实现把「幂等」寄托在 `done = 1` 这个条件上：

```cpp
q.prepare(QStringLiteral("UPDATE quests SET claimed = 1 WHERE quest_id = :id AND done = 1"));
...
return q.numRowsAffected() > 0;
```

但 SQLite 的 `numRowsAffected()`（即 `changes()`）统计的是 **WHERE 匹配到的行数**，
**不比较 SET 后的值是否真的变化**。已领取的行仍然匹配 `done = 1`，所以第二次执行
依旧「影响 1 行」→ 返回 `true`。

（`numRowsAffected()` 返回 `> 0` 只是「语句执行成功且匹配到行」的判据，
不能当作「状态发生了迁移」的判据。）

### 解决

把目标状态写进 WHERE，让**已达成目标态的行不再匹配**：

```cpp
q.prepare(QStringLiteral(
    "UPDATE quests SET claimed = 1 WHERE quest_id = :id AND done = 1 AND claimed = 0"));
```

修复后：未 `done` → `false`；首次领取 → `true`；重复领取 → `false`。

### 影响与关联文档

- 同类的 `SigninRepo::markReward` 用 `(reward_claimed & bit) = 0` 做条件，**本来就是对的**，
  可作对照写法。
- 通用原则：**幂等的条件更新 = 「目标状态」写进 WHERE**，不要用 `numRowsAffected()` 代替状态判断。
- `QuestService::claim()` 另有内存态 `claimed` 兜底，所以服务层用例此前是通过的——
  这次是仓储层的单测把它兜出来了（见 `tests/test_content.cpp::questRepoConditionalUpdate`）。

---
