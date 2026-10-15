# P-113 · 学习页清单：坏文本被当成「空清单」静默清空（缺解析失败守卫）

| 字段 | 取值 |
|---|---|
| 阶段 | EX8（工作台「学习」栏；`viewmodel/StudyService`） |
| 原编号 | —（EX8 新增） |
| 模块 | `src/viewmodel/StudyService.cpp`（`loadFromJson`） |
| 报错分层 | test（用例先红；缺陷在实现，属数据安全口径缺口） |
| 严重度 | major（若键被改坏，用户已导入清单会被静默清空） |

## 一、现象（可复现步骤 / 报错原文）

`test_study` 首次运行时失败一条：

```text
FAIL!  : StudyTest::externalItemsAndJsonRoundTrip() '!restored.loadFromJson(QStringLiteral("{not json"))' returned FALSE. ()
F:\develop\desktoppet\tests\test_study.cpp(269) : failure location
Totals: 11 passed, 3 failed, 0 skipped, 0 blacklisted, 34ms
```

复现：构造 `StudyService` 并导入 2 条 → `loadFromJson(QStringLiteral("{not json"))`
→ 期望返回 `false`（保持现状），实得 `true`（清单被换成空）。

## 二、根因

实现里 `loadFromJson` 只做了「解析 → 与当前比较 → 不同则整体替换」三步：

```cpp
const QVector<StudyItem> loaded = parseItemsJson(json, &nextExternal);  // 坏文本 → 空清单
if (itemsToJson(loaded) == itemsToJson(m_items)) { return false; }
m_items = loaded;   // ← 坏文本也会走到这里：把用户清单清空
emit changed();
```

`docs/STUDY.md` §5 的既定容错口径是「**非空但解析失败 / 不是数组 → 保持现状**（不因一份坏文本
静默清空用户的清单）；**空文本**才是『尚无清单』的默认值（清空）」。实现漏了这道守卫，
于是把「解析失败」与「本来就是空」混为一谈。用例是按设计口径写的，因此它先红 —— 这正是用例的价值。

## 三、解决或规避

在解析之前加显式守卫（空文本才允许代表「清空」）：

```cpp
const QString text = json.trimmed();
if (!text.isEmpty()) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return false; // 非空但解析不了 / 不是数组：保持现状
    }
}
```

用例补齐两个方向的断言：坏文本后**清单不变**（2 条），空文本才**清空**（0 条）。

**规避口径**：凡是「以持久化文本覆盖内存状态」的载入口，先区分**三态** ——
「无数据 / 空数据 / 坏数据」；只有明确的「空」才允许落到默认值，坏数据一律保持现状并如实返回 `false`，
不得静默改写用户数据（同口径见 `SettingsRepo::parseExtObject` 对 `json_ext` 的处理）。

## 四、影响与关联文档

- 影响面：学习页清单；若 `study_imported` 被外部改坏，旧实现会在下次 `applySettings` 时把清单清空。
- 关联：`docs/STUDY.md` §3.1 / §5（条目模型与持久化容错口径）、
  `src/model/SettingsRepo.cpp`（`json_ext` 原样文本的读写方）、
  `docs/pitfalls/ex8/P-114-*.md`（同轮另一条用例侧口径问题）。
