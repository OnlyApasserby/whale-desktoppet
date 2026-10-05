# 关键词立绘不能靠 `"meme-" + id` 拼接

> **原编号**：`TRAP-P5-001`　**阶段**：P5　**来源**：原按阶段聚合的 `traps-P5.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

**类别**：逻辑错误 / 映射 ｜ **影响**：21 项关键词梗中有 **11 项**命中后不会切换表情，
而是静默退化成通用 `curious`，用户看到的是「关键词像没生效」。

### 现象

P2 遗留的 `PetStateMachine::handleKeywordHit`（及 `EventType::KeywordHit` 分支）用**字符串拼接**
推断立绘名：

```cpp
std::string pose = "meme-omg";
const std::string candidate = "meme-" + event.keyword;  // ← 拼接
if (poseExists(candidate.c_str())) { pose = candidate; }
applyOneShot(pose, "meme." + event.keyword, ...);
```

对照参考项目的 `KEYWORD_POSES`（`references/dsh-whale-musume/assets/dsh-whale-moe.js` L2169–2177），
只有 10 项真的落在 `meme-*` 命名空间：

```
kyun→meme-kyun  omg→meme-omg  doge→meme-doge  sike→meme-sike
worship→meme-worship  peace→meme-peace  doubt→meme-doubt
wakuwaku→meme-wakuwaku  smilepain→meme-smile-pain  ojisan→meme-ojisan
```

其余 11 项的目标立绘**不在** `meme-*` 下，拼接必然落空：

| 关键词 | 正确立绘 | `"meme-"+id` 拼出的（不存在） |
|---|---|---|
| `smilepain` | `meme-smile-pain` | `meme-smilepain` |
| `bugtalk` | `work-debug` | `meme-bugtalk` |
| `ddl` | `work-deadline` | `meme-ddl` |
| `cake` | `work-boss` | `meme-cake` |
| `slack` | `work-slack-phone` | `meme-slack` |
| `deploy` | `work-deploy` | `meme-deploy` |
| `meeting` | `work-meeting` | `meme-meeting` |
| `review` | `work-review` | `meme-review` |
| `crazy` | `abstract` | `meme-crazy` |
| `cheer` / `flag` | `bold` | `meme-cheer` / `meme-flag` |
| `tired` | `work-sleep` | `meme-tired` |

> `smilepain` 是**最容易误判**的一条：它看着「就差一个连字符」，拼接**不报错**、
> `poseExists` 只是返回 `false` → 静默退化成 `curious`，没有任何日志。

### 根因

立绘名与关键词 id 之间**不是** `前缀 + id` 的机械关系，而是一张**显式映射表**
（`KEYWORD_POSES`）；用拼接替代查表，等于把「表」重写成了「猜」。

### 解决

P5 把映射表照搬进纯逻辑层 `src/core/ChatRules.h`：

- `kKeywordPoses[]`（21 项）+ `keywordPose(id)`（查表，未命中返回 `nullptr`）；
- `keywordSceneKey(id)` → `"meme." + id`（**台词**场景 key 才是拼接关系，与立绘名无关）；
- `PetController::handleKeywordHit` 改为查表：

```cpp
const char *pose = core::keywordPose(id);
if (pose == nullptr) { return; }              // hug / cute / morning 无立绘 → 优雅跳过
presentSpeak(pose, core::keywordSceneKey(id), ...);
```

并新增单测 `tests/test_chat.cpp::keywordPosesAllExist`：断言 21 项映射的**目标立绘在
`PoseNames.h` 中全部存在**，把「映射错到不存在的立绘」变成编译期之外的**测试期**失败。

### 影响与关联文档

- `core/ChatRules.h`（映射表唯一真源）、`docs/CHAT.md` §4、`docs/ROADMAP-P5-Fin.md` 任务 7。
- 通用原则：**「id → 资源名」存在多命名空间时，一律用显式映射表 + 覆盖性单测，
  不要用字符串拼接（更不要用「拼不出来就退化」当作正确路径）。**

---
