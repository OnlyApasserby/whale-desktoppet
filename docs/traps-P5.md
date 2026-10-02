# traps · P5 — 梗聊天（真实踩坑记录）

> 对应 `ROADMAP-P5-Fin.md`。按 `README.md` §二.5 约定，**仅记录 P5 实施过程中真实复现**的问题。
> 记录格式：现象（含报错原文 / 可复现步骤）→ 根因 → 解决或规避 → 影响与关联文档。
>
> **另按 `README.md` §六 约定**：崩溃类问题由**用户**使用 Qt Creator / WinDbg 调试，AI 不自行排查；
> 凡未经用户调试确认的根因，一律标注为「未定位 / 暂缓」，不得美化或凭推测写成已解决。

环境基线：Qt **6.8.4**（`D:/Qt-debug`）+ MSVC（VS 18 2026）+ CMake **4.4.2**，详见 `BUILD.md`。

---

## 记录索引

| 编号 | 一句话 | 类别 | 状态 |
|---|---|---|---|
| `TRAP-P5-001` | 关键词立绘名不能由 `"meme-" + id` 拼接：21 项中 11 项会退化成通用 `curious` | 逻辑/映射 | 已解决 |
| `TRAP-P5-002` | 语料按场景分文件后，`test_line_table` 只加载 `lines.txt` → 一致性检查对 `meme.*` 静默失效并报红 | 测试/漏检 | 已解决 |
| `TRAP-P5-003` | `CHAT.md` 写「13 种梗」，实际源文件 `KEYWORD_POSES` 为 21 项 | 文档/与源不符 | 已解决 |

---

## TRAP-P5-001 — 关键词立绘不能靠 `"meme-" + id` 拼接

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

对照参考项目的 `KEYWORD_POSES`（`referances/dsh-whale-musume/assets/dsh-whale-moe.js` L2169–2177），
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

## TRAP-P5-002 — 语料分文件后，一致性检查只加载单文件导致漏检

**类别**：测试 / 漏检 ｜ **影响**：`test_line_table` 直接报红（真实的漏加载），
同时这也是一类**危险信号**——检查本身覆盖面变窄却仍显示「通过」时会掩盖回归。

### 现象

P5 把台词库按场景拆成 `lines.txt` / `greet.txt` / `bond.txt` / `meme.txt`
（`meme.*` 从 `lines.txt` 迁出）。`ctest -C Debug` 原文：

```
3/7 Test #3: test_line_table ..................***Failed
...
FAIL!  : LineTableTest::bundledLinesCoverStateMachineScenes() 'table.hasScene(r.lineKey)' returned FALSE.
(keyword-omg: 状态机给出的场景 key 'meme.omg' 在台词资源中没有候选)
```

### 根因

`test_line_table` 的语料只从**单个文件**读取（编译期宏 `WHALEPET_LINES_FILE=.../lines.txt`），
而 `PoseFile` 加载逻辑（`PosePresenter::loadBundledLines`）已改成**多文件**。
测试的「加载口径」与产品的「加载口径」不一致：

- 产品：加载 4 个文件 → `meme.*` 有候选；
- 测试：只加载 `lines.txt` → `meme.*` 无候选 → 误报「状态机场景缺台词」。

换句话说，**分文件本身没坏，坏的是测试没跟着分文件走**。

### 解决

把测试的语料加载口径对齐到产品：

- `CMakeLists.txt`：`test_line_table` 的宏由单文件 `WHALEPET_LINES_FILE` 改为目录
  `WHALEPET_LINES_DIR`；`test_chat` 同样使用该宏。
- `tests/test_line_table.cpp`：遍历 `lines.txt/greet.txt/bond.txt/meme.txt` 逐份加载，
  任一份打不开即 `QVERIFY2` 失败（不静默跳过）。

```
Debug  : 7/7 passed ；Release: 7/7 passed
```

### 影响与关联文档

- `docs/CHAT.md` §1（语料组织）、`docs/TESTING.md`、`docs/BUILD.md`。
- **教训**：资源从「单文件」演进为「多文件/多来源」时，**所有加载该资源的地方
  （产品 + 测试 + 工具）都要同步**；否则测试会因为「只看到一部分资源」而给出**假阴（漏检）**。
  一致性类检查尤其危险：覆盖变窄但它仍然绿。

---

## TRAP-P5-003 — `CHAT.md` 写「13 种梗」，源文件实为 21 项

**类别**：文档与源不符 ｜ **影响**：按文档实现会漏掉 8 项关键词立绘映射；
若同时采用拼接方案（见 `TRAP-P5-001`），漏掉的部分会静默退化成 `curious`。

### 现象

`docs/CHAT.md` §4 标题为「关键词表情感知（13 种梗）」，`ROADMAP-P5-Fin.md` 交付物/任务也写「13 关键词」。
而参考项目源文件 `KEYWORD_POSES` **实测为 21 项**。

### 根因

文档在早期估算时写了 13，未与源文件逐条核对（源文件才是唯一真源）。

### 解决

- 以源文件为准，`ChatRules.h` 收录全部 21 项，并在文件头注释里**写明**「文档原写 13，实测 21」；
- 同步修正 `docs/CHAT.md` §4 与 `docs/ROADMAP-P5-Fin.md` 的条目数为 **21**。

### 影响与关联文档

- `docs/CHAT.md` §4、`docs/ROADMAP-P5-Fin.md`。
- 通用原则：**「数量」这类可机械核对的事实，一律以源文件/资产计数为准，并在迁移时脚本化核对
  （如 `kKeywordPoseCount` 与目标立绘存在性单测）。**
