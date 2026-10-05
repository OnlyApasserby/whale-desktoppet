# 语料分文件后，一致性检查只加载单文件导致漏检

> **原编号**：`TRAP-P5-002`　**阶段**：P5　**来源**：原按阶段聚合的 `traps-P5.md`（已拆分，正文未改动）
> **字段规范**：现象（可复现步骤 / 报错原文）→ 根因 → 解决或规避 → 影响与关联文档，以 `debug` 技能第 4 节为准。
> **索引**：`docs/README.md`「踩坑记录」区。

---

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
